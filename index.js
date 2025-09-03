/*
  Simple wrapper around the native addon providing a small EventEmitter API.
  Usage:
    const { DesktopCapturer } = require('./');
    const cap = new DesktopCapturer({ outputIndex: 0, maxFps: 30 });
    cap.on('init', info => console.log(info));
    cap.on('update', evt => console.log(evt.rects.length));
    cap.start();
*/
const { EventEmitter } = require('events');
const path = require('path');

function loadNative() {
  try {
    return require('./build/Release/desktop_capture.node');
  } catch (_) {}
  try {
    return require('./build/Debug/desktop_capture.node');
  } catch (e) {
    throw new Error('Failed to load native module. Build with "npm run build". ' + e.message);
  }
}

const native = loadNative();

class DesktopCapturer extends EventEmitter {
  constructor(opts = {}) {
    super();
    const options = {
      outputIndex: opts.outputIndex >>> 0 || 0,
      maxFps: opts.maxFps >>> 0 || 30,
      withCursor: !!opts.withCursor,
    };
    this._impl = new native.DesktopCapturer(options);
    this._started = false;
    this._onNative = (evt) => {
      // Forward events unchanged
      this.emit(evt.type, evt);
    };
  }

  start() {
    if (this._started) return;
    this._started = true;
    this._impl.start(this._onNative);
  }

  stop() {
    if (!this._started) return;
    this._started = false;
    this._impl.stop();
  }

  close() {
    this.stop();
    this._impl.close && this._impl.close();
  }

  requestFull() {
    this._impl.requestFull && this._impl.requestFull();
  }
}

module.exports = { DesktopCapturer };
