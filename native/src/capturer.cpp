#include "capturer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
static inline int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
}

DxgiCapturer::DxgiCapturer() {}

DxgiCapturer::~DxgiCapturer() { Stop(); }

bool DxgiCapturer::Initialize(const CaptureOptions& opts, std::string& err) {
  opts_ = opts;
  first_frame_ = true;

  HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) { err = "CreateDXGIFactory1 failed"; return false; }

  // Enumerate outputs across adapters
  UINT adapterIndex = 0;
  UINT outputFlatIndex = 0;
  ComPtr<IDXGIAdapter> chosenAdapter;
  ComPtr<IDXGIOutput> chosenOutput;

  while (true) {
    ComPtr<IDXGIAdapter> adapter;
    if (factory_->EnumAdapters(adapterIndex, adapter.ReleaseAndGetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;

    UINT outIndex = 0;
    while (true) {
      ComPtr<IDXGIOutput> output;
      if (adapter->EnumOutputs(outIndex, output.ReleaseAndGetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;

      if (outputFlatIndex == opts_.outputIndex) {
        chosenAdapter = adapter;
        chosenOutput = output;
        break;
      }
      outputFlatIndex++;
      outIndex++;
    }
    if (chosenOutput) break;
    adapterIndex++;
  }

  if (!chosenOutput) {
    err = "Output index not found";
    return false;
  }

  adapter_ = chosenAdapter;
  output_ = chosenOutput;
  output_->QueryInterface(IID_PPV_ARGS(output1_.ReleaseAndGetAddressOf()));
  if (!output1_) { err = "Output does not support duplication"; return false; }

  // Create D3D11 device on that adapter
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
  // flags |= D3D11_CREATE_DEVICE_DEBUG; // optional
#endif
  D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
  D3D_FEATURE_LEVEL outLevel;
  hr = D3D11CreateDevice(adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                         device_.ReleaseAndGetAddressOf(), &outLevel, context_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) { err = "D3D11CreateDevice failed"; return false; }

  DXGI_OUTPUT_DESC outDesc{};
  output_->GetDesc(&outDesc);
  width_ = outDesc.DesktopCoordinates.right - outDesc.DesktopCoordinates.left;
  height_ = outDesc.DesktopCoordinates.bottom - outDesc.DesktopCoordinates.top;
  if (width_ <= 0 || height_ <= 0) { err = "Invalid output size"; return false; }

  return RecreateDuplication(err);
}

bool DxgiCapturer::RecreateDuplication(std::string& err) {
  CleanupDuplication();
  HRESULT hr = output1_->DuplicateOutput(device_.Get(), duplication_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) { err = "DuplicateOutput failed"; return false; }

  // Ensure dimensions match duplication surface
  DXGI_OUTDUPL_DESC dupDesc{};
  duplication_->GetDesc(&dupDesc);
  width_ = static_cast<int>(dupDesc.ModeDesc.Width);
  height_ = static_cast<int>(dupDesc.ModeDesc.Height);

  // Create CPU-readable staging texture for full frame
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width_;
  desc.Height = height_;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  hr = device_->CreateTexture2D(&desc, nullptr, staging_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) { err = "CreateTexture2D staging failed"; return false; }

  // Reset previous frame storage
  try {
    prev_.assign((size_t)width_ * (size_t)height_ * 4u, 0);
  } catch (...) {
    prev_.clear();
  }

  return true;
}

void DxgiCapturer::CleanupDuplication() {
  if (duplication_) {
    duplication_->ReleaseFrame();
  }
  duplication_.Reset();
}

void DxgiCapturer::Start(std::function<void(const FrameUpdate&)> onUpdate) {
  cb_ = std::move(onUpdate);
  running_ = true;
  thread_ = std::thread(&DxgiCapturer::CaptureLoop, this);
}

void DxgiCapturer::Stop() {
  if (!running_) return;
  running_ = false;
  if (duplication_) {
    duplication_->ReleaseFrame();
  }
  if (thread_.joinable()) thread_.join();
}

void DxgiCapturer::RequestFullFrame() {
  force_full_.store(true, std::memory_order_relaxed);
}

void DxgiCapturer::CaptureLoop() {
  const UINT timeout = std::max(1u, 1000u / std::max(1u, opts_.maxFps));
  std::string err;

  while (running_) {
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = duplication_->AcquireNextFrame(timeout, &frameInfo, desktopResource.ReleaseAndGetAddressOf());
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
      continue; // no new frame within timeout
    }
    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
      if (!RecreateDuplication(err)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
      continue;
    }
    if (FAILED(hr)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    ComPtr<ID3D11Texture2D> desktopTex;
    hr = desktopResource.As(&desktopTex);
    if (FAILED(hr) || !desktopTex) {
      duplication_->ReleaseFrame();
      continue;
    }

    // Collect dirty rects, or full frame for the very first frame
    std::vector<RECT> dirty;
    if (first_frame_ || force_full_.load(std::memory_order_relaxed)) {
      RECT full{ 0, 0, width_, height_ };
      dirty.push_back(full);
      force_full_.store(false, std::memory_order_relaxed);
    } else {
      UINT dirtyBytes = 0;
      duplication_->GetFrameDirtyRects(0, nullptr, &dirtyBytes);
      UINT rectCount = dirtyBytes / sizeof(RECT);
      dirty.resize(rectCount);
      if (rectCount > 0) {
        hr = duplication_->GetFrameDirtyRects(dirtyBytes, dirty.data(), &dirtyBytes);
        if (FAILED(hr)) {
          dirty.clear();
        } else {
          rectCount = dirtyBytes / sizeof(RECT);
          dirty.resize(rectCount);
        }
      }
      // Also collect move rects and treat their destinations as dirty regions
      UINT moveBytes = 0;
      duplication_->GetFrameMoveRects(0, nullptr, &moveBytes);
      UINT moveCount = moveBytes / sizeof(DXGI_OUTDUPL_MOVE_RECT);
      std::vector<DXGI_OUTDUPL_MOVE_RECT> moves(moveCount);
      if (moveCount > 0) {
        hr = duplication_->GetFrameMoveRects(moveBytes, moves.data(), &moveBytes);
        if (SUCCEEDED(hr)) {
          moveCount = moveBytes / sizeof(DXGI_OUTDUPL_MOVE_RECT);
          moves.resize(moveCount);
          for (const auto& m : moves) {
            dirty.push_back(m.DestinationRect);
          }
        }
      }
    // If still empty, we will do a lightweight CPU diff after mapping.
    }

    // Copy full frame to staging once
    context_->CopyResource(staging_.Get(), desktopTex.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
      duplication_->ReleaseFrame();
      continue;
    }

    FrameUpdate update{};
    update.width = width_;
    update.height = height_;
    update.rects.reserve(dirty.size());

    const int maxX = width_;
    const int maxY = height_;
    if (!dirty.empty()) {
      for (const auto& r : dirty) {
        int x = clampi((int)r.left, 0, maxX);
        int y = clampi((int)r.top, 0, maxY);
        int rx = clampi((int)r.right, 0, maxX);
        int by = clampi((int)r.bottom, 0, maxY);
        int w = rx - x;
        int h = by - y;
        if (w <= 0 || h <= 0) continue;

        size_t rowBytes = (size_t)w * 4u;
        auto buf = std::make_shared<std::vector<uint8_t>>();
        buf->resize((size_t)w * (size_t)h * 4u);
        uint8_t* dst = buf->data();

        const uint8_t* base = static_cast<const uint8_t*>(mapped.pData);
        for (int row = 0; row < h; ++row) {
          const uint8_t* srcRow = base + (size_t)(y + row) * (size_t)mapped.RowPitch + (size_t)x * 4u;
          std::memcpy(dst + (size_t)row * rowBytes, srcRow, rowBytes);
        }

        RectPixels rp{};
        rp.x = x; rp.y = y; rp.w = w; rp.h = h; rp.data = std::move(buf);
        update.rects.emplace_back(std::move(rp));
      }
    } else {
      // CPU diff fallback: compute a single bounding rect of changed pixels
      if ((int)prev_.size() == width_ * height_ * 4) {
        int minX = width_, minY = height_, maxXc = -1, maxYc = -1;
        const uint8_t* base = static_cast<const uint8_t*>(mapped.pData);
        const size_t tightStride = (size_t)width_ * 4u;
        for (int y = 0; y < height_; ++y) {
          const uint8_t* srcRow = base + (size_t)y * (size_t)mapped.RowPitch;
          uint8_t* prevRow = prev_.data() + (size_t)y * tightStride;
          if (std::memcmp(prevRow, srcRow, tightStride) != 0) {
            // find leftmost and rightmost diff
            int lx = 0; int rx = width_ - 1;
            for (; lx < width_; ++lx) {
              const uint8_t* s = srcRow + (size_t)lx * 4u;
              const uint8_t* p = prevRow + (size_t)lx * 4u;
              if (s[0]!=p[0] || s[1]!=p[1] || s[2]!=p[2] || s[3]!=p[3]) break;
            }
            for (; rx >= lx; --rx) {
              const uint8_t* s = srcRow + (size_t)rx * 4u;
              const uint8_t* p = prevRow + (size_t)rx * 4u;
              if (s[0]!=p[0] || s[1]!=p[1] || s[2]!=p[2] || s[3]!=p[3]) break;
            }
            if (lx <= rx) {
              minX = std::min(minX, lx);
              maxXc = std::max(maxXc, rx);
              minY = std::min(minY, y);
              maxYc = std::max(maxYc, y);
            }
          }
        }
        if (maxXc >= 0) {
          int x = minX; int y = minY; int w = (maxXc - minX + 1); int h = (maxYc - minY + 1);
          size_t rowBytes = (size_t)w * 4u;
          auto buf = std::make_shared<std::vector<uint8_t>>();
          buf->resize((size_t)w * (size_t)h * 4u);
          uint8_t* dst = buf->data();
          const uint8_t* base2 = static_cast<const uint8_t*>(mapped.pData);
          for (int row = 0; row < h; ++row) {
            const uint8_t* srcRow = base2 + (size_t)(y + row) * (size_t)mapped.RowPitch + (size_t)x * 4u;
            std::memcpy(dst + (size_t)row * rowBytes, srcRow, rowBytes);
          }
          RectPixels rp{}; rp.x = x; rp.y = y; rp.w = w; rp.h = h; rp.data = std::move(buf);
          update.rects.emplace_back(std::move(rp));
        }
      }
    }

    // Update prev_ to current frame
    if ((int)prev_.size() == width_ * height_ * 4) {
      const uint8_t* base = static_cast<const uint8_t*>(mapped.pData);
      const size_t tightStride = (size_t)width_ * 4u;
      for (int y = 0; y < height_; ++y) {
        const uint8_t* srcRow = base + (size_t)y * (size_t)mapped.RowPitch;
        std::memcpy(prev_.data() + (size_t)y * tightStride, srcRow, tightStride);
      }
    }

    context_->Unmap(staging_.Get(), 0);

    duplication_->ReleaseFrame();

    if (!update.rects.empty() && cb_) {
      cb_(update);
    }
    if (first_frame_) first_frame_ = false;
  }
}
