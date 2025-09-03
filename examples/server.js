// Basic test server: captures screen and streams dirty-rect updates to browsers via WebSocket
// Start: npm run build && node examples/server.js, then open http://localhost:8080

const http = require('http');
const path = require('path');
const fs = require('fs');
const WebSocket = require('ws');
const { DesktopCapturer } = require('..');

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 8081;

const server = http.createServer((req, res) => {
  const url = req.url.split('?')[0];
  if (url === '/' || url === '/index.html') {
    const p = path.join(__dirname, 'public', 'index.html');
    res.writeHead(200, { 'Content-Type': 'text/html' });
    fs.createReadStream(p).pipe(res);
    return;
  }
  if (url === '/client.js') {
    const p = path.join(__dirname, 'public', 'client.js');
    res.writeHead(200, { 'Content-Type': 'application/javascript' });
    fs.createReadStream(p).pipe(res);
    return;
  }
  res.writeHead(404);
  res.end('Not found');
});

const wss = new WebSocket.Server({ noServer: true });

server.on('upgrade', (req, socket, head) => {
  if (req.url.startsWith('/ws')) {
    wss.handleUpgrade(req, socket, head, (ws) => {
      wss.emit('connection', ws, req);
    });
  } else {
    socket.destroy();
  }
});

let latestInit = null;

wss.on('connection', (ws) => {
  console.log('Client connected');
  if (latestInit) {
    ws.send(JSON.stringify(latestInit));
  }
  // Ensure the client gets a full frame soon after connect
  try { cap.requestFull(); } catch {}
});

// Small broadcast helper
function broadcast(msg, isBinary = false) {
  for (const ws of wss.clients) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(msg, { binary: isBinary });
    }
  }
}

// Start capture
const cap = new DesktopCapturer({ outputIndex: 0, maxFps: 30 });
cap.on('init', (evt) => {
  latestInit = { type: 'init', width: evt.width, height: evt.height, pixelFormat: 'BGRA', bpp: 4 };
  broadcast(JSON.stringify(latestInit));
});
cap.on('update', (evt) => {
  for (const r of evt.rects) {
    const w = r.w >>> 0, h = r.h >>> 0;
    const stride = w * 4;
    const header = Buffer.allocUnsafe(1 + 4 * 5);
    header.writeUInt8(1, 0); // message type 1 = rect update
    header.writeUInt32LE(r.x >>> 0, 1);
    header.writeUInt32LE(r.y >>> 0, 5);
    header.writeUInt32LE(w, 9);
    header.writeUInt32LE(h, 13);
    header.writeUInt32LE(stride, 17);
    broadcast(Buffer.concat([header, r.data]), true);
  }
});

cap.start();

server.listen(PORT, () => {
  console.log(`Server on http://localhost:${PORT}`);
});

process.on('SIGINT', () => {
  cap.stop();
  server.close(() => process.exit(0));
});
