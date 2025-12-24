// Basic test server: captures screen and streams dirty-rect updates to browsers via WebSocket
// Start: npm run build && node examples/server.js, then open http://localhost:8081

const http = require('http');
const path = require('path');
const fs = require('fs');
const WebSocket = require('ws');
const { DiodeConnection, PublishPort } = require('diodejs');
const { DesktopCapturer } = require('..');

const PORT = process.env.PORT ? parseInt(process.env.PORT, 10) : 80;

// --- DiodeJS: publish HTTP port ---
let diodeConnection = null;
let publishPort = null;

(async function initDiode() {
  try {
    const host = process.env.DIODE_HOST || 'us2.prenet.diode.io';
    const port = process.env.DIODE_PORT ? parseInt(process.env.DIODE_PORT, 10) : 41046;

    diodeConnection = new DiodeConnection(host, port);
    await diodeConnection.connect();

    // Print device's Diode/Ethereum address
    try {
      const deviceAddr = await diodeConnection.getEthereumAddress();
      console.log(`Diode device address: ${deviceAddr}`);
    } catch (e) {
      console.warn('Could not get Diode device address:', e);
    }

    // Publish current server port as public
    publishPort = new PublishPort(diodeConnection, { [PORT]: { mode: 'public' } });
    // Start listening for unsolicited messages (safe to call; minimal setup)
    publishPort.startListening();

    console.log(`Diode: published port ${PORT} (public)`);
  } catch (err) {
    console.error('Diode init failed:', err);
  }
})();

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

// Enable WebSocket permessage-deflate to compress large frames
const WS_COMPRESS = true;
const WS_COMPRESS_THRESHOLD = parseInt(process.env.WS_COMPRESS_THRESHOLD || '4096', 10);
const WS_COMPRESS_LEVEL = parseInt(process.env.WS_COMPRESS_LEVEL || '6', 10);

const wss = new WebSocket.Server({
  noServer: true,
  perMessageDeflate: WS_COMPRESS ? {
    threshold: WS_COMPRESS_THRESHOLD, // only compress messages larger than this (bytes)
    serverNoContextTakeover: true,
    clientNoContextTakeover: true,
    zlibDeflateOptions: { level: WS_COMPRESS_LEVEL },
  } : false,
});

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

// Small broadcast helper with simple backpressure guard
const WS_MAX_BUFFER = parseInt(process.env.WS_MAX_BUFFER || String(524288 /* 512KB */), 10);
function broadcast(msg, isBinary = false) {
  for (const ws of wss.clients) {
    if (ws.readyState !== WebSocket.OPEN) continue;
    if (typeof ws.bufferedAmount === 'number' && ws.bufferedAmount > WS_MAX_BUFFER) continue;
    ws.send(msg, { binary: isBinary });
  }
}

// Start capture
const CAP_FPS = parseInt(process.env.CAP_FPS || '15', 10);
const cap = new DesktopCapturer({ outputIndex: 0, maxFps: CAP_FPS });

// Optional packing to reduce bandwidth: set PACK=rgb565 to halve bytes
const PACK = (process.env.PACK || 'rgb565').toLowerCase();
const USE_RGB565 = PACK === 'rgb565';

function bgraToRgb565(bgra) {
  const out = Buffer.allocUnsafe((bgra.length / 4) * 2);
  let j = 0;
  for (let i = 0; i < bgra.length; i += 4) {
    const b = bgra[i];
    const g = bgra[i + 1];
    const r = bgra[i + 2];
    // pack to 5-6-5
    const r5 = r >>> 3;
    const g6 = g >>> 2;
    const b5 = b >>> 3;
    const v = (r5 << 11) | (g6 << 5) | b5;
    out[j++] = v & 0xff;       // little-endian
    out[j++] = (v >>> 8) & 0xff;
  }
  return out;
}
cap.on('init', (evt) => {
  latestInit = { type: 'init', width: evt.width, height: evt.height, pixelFormat: USE_RGB565 ? 'RGB565' : 'BGRA', bpp: USE_RGB565 ? 2 : 4 };
  broadcast(JSON.stringify(latestInit));
});
cap.on('update', (evt) => {
  for (const r of evt.rects) {
    const w = r.w >>> 0, h = r.h >>> 0;
    const is565 = USE_RGB565;
    const stride = w * (is565 ? 2 : 4);
    const header = Buffer.allocUnsafe(1 + 4 * 5);
    header.writeUInt8(1, 0); // message type 1 = rect update (format indicated by init)
    header.writeUInt32LE(r.x >>> 0, 1);
    header.writeUInt32LE(r.y >>> 0, 5);
    header.writeUInt32LE(w, 9);
    header.writeUInt32LE(h, 13);
    header.writeUInt32LE(stride, 17);
    const payload = is565 ? bgraToRgb565(r.data) : r.data;
    broadcast(Buffer.concat([header, payload]), true);
  }
});

cap.start();

server.listen(PORT, () => {
  console.log(`Server on http://localhost:${PORT}`);
});

process.on('SIGINT', () => {
  cap.stop();
  server.close(() => {
    try { if (diodeConnection) diodeConnection.close(); } catch {}
    process.exit(0);
  });
});
