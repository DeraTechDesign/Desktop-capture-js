#include <napi.h>
#include <string.h>
#include <vector>

class BitmapProcessor : public Napi::ObjectWrap<BitmapProcessor> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    BitmapProcessor(const Napi::CallbackInfo& info);

private:
    Napi::Value InitializeBitmap(const Napi::CallbackInfo& info);
    Napi::Value ApplyDirtyRegions(const Napi::CallbackInfo& info);
    Napi::Value ApplyMoveRegions(const Napi::CallbackInfo& info);
    Napi::Value GetBitmapBuffer(const Napi::CallbackInfo& info);

    std::vector<uint8_t> bitmapBuffer;
    int width;
    int height;
    int bytesPerPixel;
    int rowSize;
    int rowPadding;
    int bytesPerRow;
    int imageSize;
    int fileSize;
};

Napi::Object BitmapProcessor::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "BitmapProcessor", {
        InstanceMethod("initializeBitmap", &BitmapProcessor::InitializeBitmap),
        InstanceMethod("applyDirtyRegions", &BitmapProcessor::ApplyDirtyRegions),
        InstanceMethod("applyMoveRegions", &BitmapProcessor::ApplyMoveRegions),
        InstanceMethod("getBitmapBuffer", &BitmapProcessor::GetBitmapBuffer),
    });

    exports.Set("BitmapProcessor", func);
    return exports;
}

BitmapProcessor::BitmapProcessor(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<BitmapProcessor>(info) {
    // Constructor
    this->width = 0;
    this->height = 0;
    this->bytesPerPixel = 4; // 32 bits per pixel (BGRA)
}

Napi::Value BitmapProcessor::InitializeBitmap(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 ||
        !info[0].IsNumber() ||
        !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected width and height as arguments").ThrowAsJavaScriptException();
        return env.Null();
    }

    this->width = info[0].As<Napi::Number>().Int32Value();
    this->height = info[1].As<Napi::Number>().Int32Value();

    this->rowSize = this->width * this->bytesPerPixel;
    this->rowPadding = (4 - (this->rowSize % 4)) % 4;
    this->bytesPerRow = this->rowSize + this->rowPadding;
    this->imageSize = this->bytesPerRow * this->height;
    this->fileSize = 54 + this->imageSize; // 54 bytes for BMP header

    this->bitmapBuffer.resize(this->fileSize);

    // BMP Header
    memcpy(&this->bitmapBuffer[0], "BM", 2); // Signature
    uint32_t fileSizeLE = this->fileSize;
    memcpy(&this->bitmapBuffer[2], &fileSizeLE, 4); // File size
    uint32_t reserved = 0;
    memcpy(&this->bitmapBuffer[6], &reserved, 4); // Reserved
    uint32_t offset = 54;
    memcpy(&this->bitmapBuffer[10], &offset, 4); // Offset to pixel data

    // DIB Header (BITMAPINFOHEADER)
    uint32_t dibHeaderSize = 40;
    memcpy(&this->bitmapBuffer[14], &dibHeaderSize, 4); // DIB header size
    int32_t widthLE = this->width;
    memcpy(&this->bitmapBuffer[18], &widthLE, 4); // Width
    int32_t heightLE = this->height;
    memcpy(&this->bitmapBuffer[22], &heightLE, 4); // Height
    uint16_t planes = 1;
    memcpy(&this->bitmapBuffer[26], &planes, 2); // Color planes
    uint16_t bitsPerPixel = 32;
    memcpy(&this->bitmapBuffer[28], &bitsPerPixel, 2); // Bits per pixel
    uint32_t compression = 0;
    memcpy(&this->bitmapBuffer[30], &compression, 4); // Compression (0 = none)
    memcpy(&this->bitmapBuffer[34], &this->imageSize, 4); // Image size
    int32_t ppm = 2835; // 72 DPI * 39.3701 inches per meter
    memcpy(&this->bitmapBuffer[38], &ppm, 4); // Horizontal resolution
    memcpy(&this->bitmapBuffer[42], &ppm, 4); // Vertical resolution
    uint32_t colors = 0;
    memcpy(&this->bitmapBuffer[46], &colors, 4); // Number of colors
    memcpy(&this->bitmapBuffer[50], &colors, 4); // Important colors

    return env.Null();
}

Napi::Value BitmapProcessor::ApplyDirtyRegions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected an array of dirty regions").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array regions = info[0].As<Napi::Array>();

    for (uint32_t i = 0; i < regions.Length(); i++) {
        Napi::Value regionValue = regions[i];
        if (!regionValue.IsObject()) continue;

        Napi::Object region = regionValue.As<Napi::Object>();
        int left = region.Get("left").As<Napi::Number>().Int32Value();
        int top = region.Get("top").As<Napi::Number>().Int32Value();
        int width = region.Get("width").As<Napi::Number>().Int32Value();
        int height = region.Get("height").As<Napi::Number>().Int32Value();
        Napi::Buffer<uint8_t> pixelsBuffer = region.Get("pixels").As<Napi::Buffer<uint8_t>>();
        uint8_t* pixels = pixelsBuffer.Data();

        for (int y = 0; y < height; y++) {
            int pixelRowStart = y * width * this->bytesPerPixel;
            int bmpY = top + y;

            if (bmpY >= this->height) continue; // Out of bounds

            // BMP stores pixels bottom-up
            int bmpRow = this->height - 1 - bmpY;
            int bmpRowStart = 54 + (bmpRow * this->bytesPerRow);

            for (int x = 0; x < width; x++) {
                int pixelIndex = pixelRowStart + x * this->bytesPerPixel;
                int bmpX = left + x;

                if (bmpX >= this->width) continue; // Out of bounds

                int bmpOffset = bmpRowStart + bmpX * this->bytesPerPixel;

                // Assuming pixel data is in BGRA format
                this->bitmapBuffer[bmpOffset] = pixels[pixelIndex];       // B
                this->bitmapBuffer[bmpOffset + 1] = pixels[pixelIndex + 1]; // G
                this->bitmapBuffer[bmpOffset + 2] = pixels[pixelIndex + 2]; // R
                this->bitmapBuffer[bmpOffset + 3] = pixels[pixelIndex + 3]; // A
            }
        }
    }

    return env.Null();
}

Napi::Value BitmapProcessor::ApplyMoveRegions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected an array of move regions").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array moves = info[0].As<Napi::Array>();

    for (uint32_t i = 0; i < moves.Length(); i++) {
        Napi::Value moveValue = moves[i];
        if (!moveValue.IsObject()) continue;

        Napi::Object move = moveValue.As<Napi::Object>();
        Napi::Object sourcePointObj = move.Get("sourcePoint").As<Napi::Object>();
        Napi::Object destRectObj = move.Get("destinationRect").As<Napi::Object>();

        int srcX = sourcePointObj.Get("x").As<Napi::Number>().Int32Value();
        int srcY = sourcePointObj.Get("y").As<Napi::Number>().Int32Value();
        int left = destRectObj.Get("left").As<Napi::Number>().Int32Value();
        int top = destRectObj.Get("top").As<Napi::Number>().Int32Value();
        int right = destRectObj.Get("right").As<Napi::Number>().Int32Value();
        int bottom = destRectObj.Get("bottom").As<Napi::Number>().Int32Value();
        int width = right - left;
        int height = bottom - top;

        for (int y = 0; y < height; y++) {
            int srcYPos = srcY + y;
            int destYPos = top + y;

            if (srcYPos >= this->height || destYPos >= this->height) continue; // Out of bounds

            // BMP stores pixels bottom-up
            int srcBmpRow = this->height - 1 - srcYPos;
            int destBmpRow = this->height - 1 - destYPos;
            int srcRowStart = 54 + (srcBmpRow * this->bytesPerRow);
            int destRowStart = 54 + (destBmpRow * this->bytesPerRow);

            for (int x = 0; x < width; x++) {
                int srcXPos = srcX + x;
                int destXPos = left + x;

                if (srcXPos >= this->width || destXPos >= this->width) continue; // Out of bounds

                int srcOffset = srcRowStart + srcXPos * this->bytesPerPixel;
                int destOffset = destRowStart + destXPos * this->bytesPerPixel;

                // Copy BGRA from source to destination
                memcpy(&this->bitmapBuffer[destOffset], &this->bitmapBuffer[srcOffset], this->bytesPerPixel);
            }
        }
    }

    return env.Null();
}

Napi::Value BitmapProcessor::GetBitmapBuffer(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    return Napi::Buffer<uint8_t>::Copy(env, this->bitmapBuffer.data(), this->bitmapBuffer.size());
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
    return BitmapProcessor::Init(env, exports);
}

NODE_API_MODULE(bitmap_processor, InitAll)
