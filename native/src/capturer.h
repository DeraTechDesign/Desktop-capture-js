#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

struct RectPixels {
  int x{0}, y{0}, w{0}, h{0};
  std::shared_ptr<std::vector<uint8_t>> data; // BGRA, tightly packed
};

struct FrameUpdate {
  int width{0};
  int height{0};
  std::vector<RectPixels> rects; // dirty rects with pixel data
};

struct CaptureOptions {
  uint32_t outputIndex{0};
  uint32_t maxFps{30};
  bool withCursor{false};
};

// Lightweight DXGI Desktop Duplication capturer that produces dirty rect updates
class DxgiCapturer {
 public:
  DxgiCapturer();
  ~DxgiCapturer();

  bool Initialize(const CaptureOptions& opts, std::string& err);
  void Start(std::function<void(const FrameUpdate&)> onUpdate);
  void Stop();
  void RequestFullFrame();

  int Width() const { return width_; }
  int Height() const { return height_; }

 private:
  void CaptureLoop();
  void CleanupDuplication();
  bool RecreateDuplication(std::string& err);

  CaptureOptions opts_{};
  std::function<void(const FrameUpdate&)> cb_;

  std::atomic<bool> running_{false};
  std::thread thread_;

  // DXGI/D3D
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory_;
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter_;
  Microsoft::WRL::ComPtr<IDXGIOutput> output_;
  Microsoft::WRL::ComPtr<IDXGIOutput1> output1_;
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_; // CPU readable latest frame

  int width_{0};
  int height_{0};
  bool first_frame_{true};
  std::atomic<bool> force_full_{false};
  std::vector<uint8_t> prev_; // previous frame in tight BGRA (width*height*4)
};
