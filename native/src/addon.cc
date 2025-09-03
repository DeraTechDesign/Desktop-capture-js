#include <napi.h>
#include "capturer.h"

#include <memory>
#include <string>

class DesktopCapturerWrap : public Napi::ObjectWrap<DesktopCapturerWrap> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "DesktopCapturer", {
      InstanceMethod<&DesktopCapturerWrap::Start>("start"),
      InstanceMethod<&DesktopCapturerWrap::Stop>("stop"),
      InstanceMethod<&DesktopCapturerWrap::Close>("close"),
      InstanceMethod<&DesktopCapturerWrap::RequestFull>("requestFull"),
    });
    constructor() = Napi::Persistent(func);
    constructor().SuppressDestruct();
    exports.Set("DesktopCapturer", func);
    return exports;
  }

  DesktopCapturerWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<DesktopCapturerWrap>(info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
      Napi::TypeError::New(env, "Options object required").ThrowAsJavaScriptException();
      return;
    }
    Napi::Object opts = info[0].As<Napi::Object>();
    CaptureOptions co{};
    if (opts.Has("outputIndex")) co.outputIndex = opts.Get("outputIndex").ToNumber().Uint32Value();
    if (opts.Has("maxFps")) co.maxFps = opts.Get("maxFps").ToNumber().Uint32Value();
    if (opts.Has("withCursor")) co.withCursor = opts.Get("withCursor").ToBoolean().Value();

    capturer_ = std::make_unique<DxgiCapturer>();
    std::string err;
    if (!capturer_->Initialize(co, err)) {
      Napi::Error::New(env, std::string("Initialize failed: ") + err).ThrowAsJavaScriptException();
      capturer_.reset();
      return;
    }
  }

  ~DesktopCapturerWrap() override { StopInternal(); }

  void Start(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsFunction()) {
      Napi::TypeError::New(env, "Callback required").ThrowAsJavaScriptException();
      return;
    }
    if (running_) return;
    running_ = true;
    Napi::Function jsCb = info[0].As<Napi::Function>();
    tsfn_ = Napi::ThreadSafeFunction::New(
      env,
      jsCb,                   // JavaScript function called asynchronously
      "DesktopCaptureCb",     // Resource name
      16,                     // Max queue size
      1                       // Initial thread count
    );

    // Send an init event immediately
    {
      auto callInit = [this](Napi::Env env, Napi::Function cb) {
        Napi::Object evt = Napi::Object::New(env);
        evt.Set("type", "init");
        evt.Set("width", Napi::Number::New(env, capturer_->Width()));
        evt.Set("height", Napi::Number::New(env, capturer_->Height()));
        evt.Set("pixelFormat", "BGRA");
        evt.Set("bytesPerPixel", 4);
        cb.Call({ evt });
      };
      tsfn_.BlockingCall([callInit](Napi::Env env, Napi::Function cb) { callInit(env, cb); });
    }

    // Start native capture
    capturer_->Start([this](const FrameUpdate& upd) {
      // Package data for JS on the fly within TSFN call to avoid extra copies
      if (!tsfn_) return;
      struct Pack {
        FrameUpdate update;
      };
      Pack* p = new Pack{ upd };
      auto jsCall = [](Napi::Env env, Napi::Function cb, Pack* pack) {
        Napi::Object evt = Napi::Object::New(env);
        evt.Set("type", "update");
        evt.Set("width", Napi::Number::New(env, pack->update.width));
        evt.Set("height", Napi::Number::New(env, pack->update.height));
        Napi::Array rects = Napi::Array::New(env, pack->update.rects.size());
        for (size_t i = 0; i < pack->update.rects.size(); ++i) {
          const RectPixels& rp = pack->update.rects[i];
          Napi::Object r = Napi::Object::New(env);
          r.Set("x", Napi::Number::New(env, rp.x));
          r.Set("y", Napi::Number::New(env, rp.y));
          r.Set("w", Napi::Number::New(env, rp.w));
          r.Set("h", Napi::Number::New(env, rp.h));
          // Create Buffer that owns its memory via external finalizer
          if (rp.data && !rp.data->empty()) {
            size_t len = rp.data->size();
            // Allocate a new heap block and copy once to detach from shared_ptr for JS lifetime
            uint8_t* heapBuf = (uint8_t*)::malloc(len);
            if (heapBuf && len > 0) {
              std::memcpy(heapBuf, rp.data->data(), len);
              Napi::Buffer<uint8_t> buf = Napi::Buffer<uint8_t>::New(env, heapBuf, len,
                [](Napi::Env, uint8_t* data) { ::free(data); });
              r.Set("data", buf);
            }
          }
          rects.Set((uint32_t)i, r);
        }
        evt.Set("rects", rects);
        cb.Call({ evt });
        delete pack;
      };
      napi_status st = tsfn_.BlockingCall(p, jsCall);
      if (st != napi_ok) {
        delete p;
      }
    });
  }

  void Stop(const Napi::CallbackInfo&) { StopInternal(); }
  void Close(const Napi::CallbackInfo&) { StopInternal(); }
  void RequestFull(const Napi::CallbackInfo&) {
    if (capturer_) capturer_->RequestFullFrame();
  }

private:
  void StopInternal() {
    if (!running_) return;
    running_ = false;
    if (capturer_) capturer_->Stop();
    if (tsfn_) {
      tsfn_.Release();
      tsfn_ = Napi::ThreadSafeFunction();
    }
  }

  static Napi::FunctionReference& constructor() { static Napi::FunctionReference ctor; return ctor; }

  std::unique_ptr<DxgiCapturer> capturer_;
  bool running_{false};
  Napi::ThreadSafeFunction tsfn_;
};

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return DesktopCapturerWrap::Init(env, exports);
}

NODE_API_MODULE(desktop_capture, InitAll)
