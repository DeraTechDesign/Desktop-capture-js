#include <napi.h>
#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

class DesktopDuplicator : public Napi::ObjectWrap<DesktopDuplicator> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    DesktopDuplicator(const Napi::CallbackInfo& info);

private:
    Napi::Value GetFrame(const Napi::CallbackInfo& info);
    void Initialize(Napi::Env env);

    // Desktop Duplication members
    IDXGIOutputDuplication* deskDupl = nullptr;
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    int width = 0;
    int height = 0;
};

Napi::Object DesktopDuplicator::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "DesktopDuplicator", {
        InstanceMethod("getFrame", &DesktopDuplicator::GetFrame),
    });

    exports.Set("DesktopDuplicator", func);
    return exports;
}

DesktopDuplicator::DesktopDuplicator(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<DesktopDuplicator>(info) {
    Napi::Env env = info.Env();
    Initialize(env);
}

void DesktopDuplicator::Initialize(Napi::Env env) {
    HRESULT hr;

    // Create D3D11 device
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &d3dDevice, nullptr, &d3dContext
    );
    if (FAILED(hr)) {
        Napi::Error::New(env, "Failed to create D3D11 device").ThrowAsJavaScriptException();
        return;
    }

    // Get DXGI device
    IDXGIDevice* dxgiDevice = nullptr;
    hr = d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) {
        Napi::Error::New(env, "Failed to get IDXGIDevice").ThrowAsJavaScriptException();
        return;
    }

    // Get DXGI adapter
    IDXGIAdapter* dxgiAdapter = nullptr;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    dxgiDevice->Release();
    if (FAILED(hr)) {
        Napi::Error::New(env, "Failed to get IDXGIAdapter").ThrowAsJavaScriptException();
        return;
    }

    // Get output (monitor)
    IDXGIOutput* dxgiOutput = nullptr;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    dxgiAdapter->Release();
    if (FAILED(hr)) {
        Napi::Error::New(env, "Failed to get IDXGIOutput").ThrowAsJavaScriptException();
        return;
    }

    // Get Output1 interface
    IDXGIOutput1* dxgiOutput1 = nullptr;
    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
    dxgiOutput->Release();
    if (FAILED(hr)) {
        Napi::Error::New(env, "Failed to get IDXGIOutput1").ThrowAsJavaScriptException();
        return;
    }

    // Create Desktop Duplication
    hr = dxgiOutput1->DuplicateOutput(d3dDevice, &deskDupl);
    dxgiOutput1->Release();
    if (FAILED(hr)) {
        Napi::Error::New(env, "Failed to duplicate output").ThrowAsJavaScriptException();
        return;
    }

    // Get output description to retrieve screen dimensions
    DXGI_OUTPUT_DESC outputDesc;
    dxgiOutput1->GetDesc(&outputDesc);
    width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
}

Napi::Value DesktopDuplicator::GetFrame(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = { 0 };
    HRESULT hr;

    while (true) {
        hr = deskDupl->AcquireNextFrame(500, &frameInfo, &desktopResource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No new frame, continue
            continue;
        } else if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Reinitialize desktop duplication
            deskDupl->Release();
            deskDupl = nullptr;
            Initialize(env);
            return env.Null();
        } else if (FAILED(hr)) {
            Napi::Error::New(env, "Failed to acquire next frame").ThrowAsJavaScriptException();
            return env.Null();
        }

        // Break if frame acquired successfully
        break;
    }

    // Get the desktop image
    ID3D11Texture2D* desktopImage = nullptr;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&desktopImage);
    desktopResource->Release();
    if (FAILED(hr)) {
        deskDupl->ReleaseFrame();
        Napi::Error::New(env, "Failed to get ID3D11Texture2D").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Create CPU-accessible texture (staging texture)
    D3D11_TEXTURE2D_DESC desc;
    desktopImage->GetDesc(&desc);
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    ID3D11Texture2D* cpuTexture = nullptr;
    hr = d3dDevice->CreateTexture2D(&desc, nullptr, &cpuTexture);
    if (FAILED(hr)) {
        desktopImage->Release();
        deskDupl->ReleaseFrame();
        Napi::Error::New(env, "Failed to create CPU-accessible texture").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Copy resource into CPU-accessible texture
    d3dContext->CopyResource(cpuTexture, desktopImage);

    // Get the dirty rectangles
    UINT dirtyRectsBufferSize = frameInfo.TotalMetadataBufferSize;
    RECT* dirtyRects = new RECT[dirtyRectsBufferSize / sizeof(RECT)];
    UINT dirtyRectsSizeRequired = 0;

    hr = deskDupl->GetFrameDirtyRects(dirtyRectsBufferSize, dirtyRects, &dirtyRectsSizeRequired);
    if (FAILED(hr)) {
        delete[] dirtyRects;
        cpuTexture->Release();
        desktopImage->Release();
        deskDupl->ReleaseFrame();
        Napi::Error::New(env, "Failed to get frame dirty rects").ThrowAsJavaScriptException();
        return env.Null();
    }

    UINT dirtyRectsCount = dirtyRectsSizeRequired / sizeof(RECT);

    // Map the staging texture to access pixel data
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = d3dContext->Map(cpuTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        delete[] dirtyRects;
        cpuTexture->Release();
        desktopImage->Release();
        deskDupl->ReleaseFrame();
        Napi::Error::New(env, "Failed to map CPU-accessible texture").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Prepare the result object
    Napi::Object result = Napi::Object::New(env);
    Napi::Array dirtyRegionsArray = Napi::Array::New(env, dirtyRectsCount);

    for (UINT i = 0; i < dirtyRectsCount; i++) {
        RECT rect = dirtyRects[i];
        int rectWidth = rect.right - rect.left;
        int rectHeight = rect.bottom - rect.top;
        int bytesPerPixel = 4; // Assuming 32-bit color

        // Extract pixel data
        BYTE* pixels = new BYTE[rectWidth * rectHeight * bytesPerPixel];

        for (int y = 0; y < rectHeight; y++) {
            memcpy(
                pixels + y * rectWidth * bytesPerPixel,
                (BYTE*)mappedResource.pData + (rect.top + y) * mappedResource.RowPitch + rect.left * bytesPerPixel,
                rectWidth * bytesPerPixel
            );
        }

        // Create a buffer for the pixel data
        Napi::Buffer<BYTE> pixelBuffer = Napi::Buffer<BYTE>::Copy(env, pixels, rectWidth * rectHeight * bytesPerPixel);
        delete[] pixels;

        // Add to dirty regions array
        Napi::Object region = Napi::Object::New(env);
        region.Set("left", rect.left);
        region.Set("top", rect.top);
        region.Set("right", rect.right);
        region.Set("bottom", rect.bottom);
        region.Set("width", rectWidth);
        region.Set("height", rectHeight);
        region.Set("pixels", pixelBuffer);
        dirtyRegionsArray.Set(i, region);
    }

    // Get move regions
    UINT moveRectsBufferSize = frameInfo.TotalMetadataBufferSize - dirtyRectsSizeRequired;
    DXGI_OUTDUPL_MOVE_RECT* moveRects = new DXGI_OUTDUPL_MOVE_RECT[moveRectsBufferSize / sizeof(DXGI_OUTDUPL_MOVE_RECT)];
    UINT moveRectsSizeRequired = 0;

    hr = deskDupl->GetFrameMoveRects(moveRectsBufferSize, moveRects, &moveRectsSizeRequired);
    if (FAILED(hr)) {
        delete[] dirtyRects;
        delete[] moveRects;
        d3dContext->Unmap(cpuTexture, 0);
        cpuTexture->Release();
        desktopImage->Release();
        deskDupl->ReleaseFrame();
        Napi::Error::New(env, "Failed to get frame move rects").ThrowAsJavaScriptException();
        return env.Null();
    }

    UINT moveRectsCount = moveRectsSizeRequired / sizeof(DXGI_OUTDUPL_MOVE_RECT);

    Napi::Array moveRegionsArray = Napi::Array::New(env, moveRectsCount);

    for (UINT i = 0; i < moveRectsCount; i++) {
        DXGI_OUTDUPL_MOVE_RECT moveRect = moveRects[i];

        Napi::Object moveRegion = Napi::Object::New(env);
        moveRegion.Set("x", moveRect.SourcePoint.x);
        moveRegion.Set("y", moveRect.SourcePoint.y);
        moveRegion.Set("left", moveRect.DestinationRect.left);
        moveRegion.Set("top", moveRect.DestinationRect.top);
        moveRegion.Set("right", moveRect.DestinationRect.right);
        moveRegion.Set("bottom", moveRect.DestinationRect.bottom);

        moveRegionsArray.Set(i, moveRegion);
    }

    d3dContext->Unmap(cpuTexture, 0);
    cpuTexture->Release();
    desktopImage->Release();
    deskDupl->ReleaseFrame();
    delete[] dirtyRects;
    delete[] moveRects;

    // Set result properties
    result.Set("dirtyRegions", dirtyRegionsArray);
    result.Set("moveRegions", moveRegionsArray);
    result.Set("width", width);
    result.Set("height", height);

    return result;
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    return DesktopDuplicator::Init(env, exports);
}

NODE_API_MODULE(desktop_duplication, InitAll)
