(() => {
  const info = document.getElementById('info');
  const canvas = document.getElementById('c');
  const ctx = canvas.getContext('2d');
  let connected = false;
  let pixelFormat = 'BGRA'; // or 'RGB565'

  function log(s) { info.textContent = s; }

  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => { connected = true; log('Connected'); };
  ws.onclose = () => { connected = false; log('Disconnected'); };
  ws.onerror = (e) => { console.error(e); };

  ws.onmessage = (ev) => {
    if (typeof ev.data === 'string') {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'init') {
          canvas.width = msg.width;
          canvas.height = msg.height;
          pixelFormat = msg.pixelFormat || 'BGRA';
          log(`Init: ${msg.width}x${msg.height} (${pixelFormat})`);
        }
      } catch (e) { /* ignore */ }
      return;
    }

    const buf = ev.data; // ArrayBuffer
    const dv = new DataView(buf);
    const type = dv.getUint8(0);
    if (type !== 1) return;
    const x = dv.getUint32(1, true);
    const y = dv.getUint32(5, true);
    const w = dv.getUint32(9, true);
    const h = dv.getUint32(13, true);
    const stride = dv.getUint32(17, true);
    const pixels = new Uint8Array(buf, 1 + 4 * 5);

    const img = ctx.createImageData(w, h);
    const out = img.data;

    if (pixelFormat === 'RGB565') {
      // Convert RGB565 -> RGBA
      // pixels length is w*h*2, tightly packed
      for (let i = 0, j = 0; i < pixels.length; i += 2, j += 4) {
        const lo = pixels[i];
        const hi = pixels[i + 1];
        const v = lo | (hi << 8);
        const r = (v >> 11) & 0x1f;
        const g = (v >> 5) & 0x3f;
        const b = v & 0x1f;
        out[j] = (r << 3) | (r >> 2);
        out[j + 1] = (g << 2) | (g >> 4);
        out[j + 2] = (b << 3) | (b >> 2);
        out[j + 3] = 255;
      }
      ctx.putImageData(img, x, y);
      return;
    }

    // Default: BGRA -> RGBA
    for (let i = 0, j = 0; i < pixels.length; i += 4, j += 4) {
      const b = pixels[i], g = pixels[i + 1], r = pixels[i + 2], a = pixels[i + 3];
      out[j] = r; out[j + 1] = g; out[j + 2] = b; out[j + 3] = a;
    }
    ctx.putImageData(img, x, y);
  };
})();
