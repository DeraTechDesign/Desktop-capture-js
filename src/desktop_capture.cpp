#include <napi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <vector>
#include <sstream>
#include <iomanip>
#pragma comment(lib,"d3d11.lib")
using namespace Microsoft::WRL;

class DesktopCapture : public Napi::ObjectWrap<DesktopCapture> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    DesktopCapture(const Napi::CallbackInfo& info);

private:
    Napi::Value GetFrame(const Napi::CallbackInfo& info);
    void Initialize();

    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIOutputDuplication> deskDupl;
    int width;
    int height;
};

Napi::Object DesktopCapture::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "DesktopCapture", {
        InstanceMethod("getFrame", &DesktopCapture::GetFrame),
    });

    exports.Set("DesktopCapture", func);
    return exports;
}

DesktopCapture::DesktopCapture(const Napi::CallbackInfo& info) : Napi::ObjectWrap<DesktopCapture>(info) {
    Initialize();
}

void DesktopCapture::Initialize() {
    HRESULT hr;

    // Create D3D11 device
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION,
        &d3dDevice, nullptr, &d3dContext);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to create D3D11 device. HRESULT: 0x" << std::hex << hr;
        Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
        return;
    }

    // Get DXGI device
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to get IDXGIDevice. HRESULT: 0x" << std::hex << hr;
        Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
        return;
    }

    // Get DXGI adapter
    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to get IDXGIAdapter. HRESULT: 0x" << std::hex << hr;
        Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
        return;
    }

    // Get output (monitor)
    ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to get IDXGIOutput. HRESULT: 0x" << std::hex << hr;
        Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
        return;
    }

    // QI for Output 1
    ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to get IDXGIOutput1. HRESULT: 0x" << std::hex << hr;
        Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
        return;
    }

    // Create desktop duplication
    hr = dxgiOutput1->DuplicateOutput(d3dDevice.Get(), &deskDupl);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to duplicate output. HRESULT: 0x" << std::hex << hr;

        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            ss << " (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)";
        } else if (hr == E_ACCESSDENIED) {
            ss << " (E_ACCESSDENIED)";
        }

        Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
        return;
    }

    // Get output desc to get screen dimensions
    DXGI_OUTPUT_DESC outputDesc;
    dxgiOutput->GetDesc(&outputDesc);
    width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
}

Napi::Value DesktopCapture::GetFrame(const Napi::CallbackInfo& info) {
    HRESULT hr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    ComPtr<IDXGIResource> desktopResource;
    ComPtr<ID3D11Texture2D> acquiredDesktopImage;

    while (true){
        hr = deskDupl->AcquireNextFrame(INFINITE, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                // No new frame available yet, continue waiting
                continue;
            } else if (hr == DXGI_ERROR_ACCESS_LOST) {
                // Reinitialize desktop duplication
                deskDupl.Reset();
                Initialize(); // Reinitialize duplication
                Napi::Error::New(Env(), "Access lost. Reinitializing desktop duplication.").ThrowAsJavaScriptException();
                return Env().Undefined();
            } else {
                std::stringstream ss;
                ss << "Failed to acquire next frame. HRESULT: 0x" << std::hex << hr;
                Napi::Error::New(Env(), ss.str()).ThrowAsJavaScriptException();
                return Env().Undefined();
            }
        }

        if (frameInfo.AccumulatedFrames == 0) {
            // No new frames accumulated, release and try again
            deskDupl->ReleaseFrame();
            continue;
        }

        // QI for ID3D11Texture2D
        hr = desktopResource.As(&acquiredDesktopImage);
        desktopResource.Reset();
        if (FAILED(hr)) {
            Napi::Error::New(Env(), "Failed to query ID3D11Texture2D").ThrowAsJavaScriptException();
            deskDupl->ReleaseFrame();
            return Env().Undefined();
        }

        // Copy image into CPU-accessible texture
        D3D11_TEXTURE2D_DESC desc;
        acquiredDesktopImage->GetDesc(&desc);

        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> cpuImage;
        hr = d3dDevice->CreateTexture2D(&desc, nullptr, &cpuImage);
        if (FAILED(hr)) {
            Napi::Error::New(Env(), "Failed to create CPU-accessible texture").ThrowAsJavaScriptException();
            deskDupl->ReleaseFrame();
            return Env().Undefined();
        }

        d3dContext->CopyResource(cpuImage.Get(), acquiredDesktopImage.Get());

        // Map the texture
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext->Map(cpuImage.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            Napi::Error::New(Env(), "Failed to map texture").ThrowAsJavaScriptException();
            deskDupl->ReleaseFrame();
            return Env().Undefined();
        }

        // Copy data to a buffer
        size_t dataSize = height * mapped.RowPitch;
        std::vector<uint8_t> buffer(dataSize);
        uint8_t* src = static_cast<uint8_t*>(mapped.pData);
        for (int i = 0; i < height; ++i) {
            memcpy(buffer.data() + i * mapped.RowPitch, src + i * mapped.RowPitch, mapped.RowPitch);
        }

        // Unmap and release frame
        d3dContext->Unmap(cpuImage.Get(), 0);
        deskDupl->ReleaseFrame();

        // Convert BGRA to RGBA
        for (size_t i = 0; i < buffer.size(); i += 4) {
            std::swap(buffer[i], buffer[i + 2]); // Swap blue and red channels
        }

        // Return data as Node.js Buffer
        // Create a JavaScript object to return
        Napi::Object result = Napi::Object::New(Env());
        result.Set("width", Napi::Number::New(Env(), width));
        result.Set("height", Napi::Number::New(Env(), height));
        result.Set("rowPitch", Napi::Number::New(Env(), mapped.RowPitch));
        result.Set("data", Napi::Buffer<uint8_t>::Copy(Env(), buffer.data(), buffer.size()));

        return result;
    }
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    return DesktopCapture::Init(env, exports);
}

NODE_API_MODULE(desktop_capture, InitAll)
