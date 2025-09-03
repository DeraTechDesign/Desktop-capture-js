Desktop Capture JS (Windows)

Native, efficient Windows screen sharing for Node.js using the DXGI Desktop Duplication API. Instead of encoding video, it streams only dirty regions (rect updates) similar to RDP.

Features
- Native C++ addon via N-API (node-addon-api)
- Uses Desktop Duplication for dirty rects and low CPU overhead
- Sends raw BGRA pixel updates for changed regions
- Example WebSocket server + browser client that paints updates on a canvas

Requirements (Windows)
- Node.js 18+
- Visual Studio 2019/2022 Build Tools with C++ workload
- Windows 10/11 with Desktop Duplication support

Install & Build
- npm install
- npm run build

Run Example
- npm run start:server
- Open http://localhost:8080 in a browser

API
const { DesktopCapturer } = require('desktop-capture-js');
const cap = new DesktopCapturer({ outputIndex: 0, maxFps: 30, withCursor: false });
cap.on('init', ({ width, height, pixelFormat, bytesPerPixel }) => { /* ... */ });
cap.on('update', ({ width, height, rects }) => {
  // rects: [{ x, y, w, h, data: Buffer(BGRA) }]
});
cap.start();
cap.stop();

Notes
- Pixel format is BGRA (32-bit). The example client converts to RGBA for Canvas.
- Dirty rects only; move rects and cursor shape are not yet applied.
- For production, consider packing multiple rects into a single frame and applying move rects for efficiency.

