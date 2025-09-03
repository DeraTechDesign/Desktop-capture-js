(() => {
  const info = document.getElementById('info');
  const canvas = document.getElementById('c');
  const ctx = canvas.getContext('2d');
  let connected = false;

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
          log(`Init: ${msg.width}x${msg.height}`);
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

    // Convert BGRA -> RGBA
    // pixels length is w*h*4, tightly packed
    const img = ctx.createImageData(w, h);
    const out = img.data;
    for (let i = 0, j = 0; i < pixels.length; i += 4, j += 4) {
      const b = pixels[i], g = pixels[i+1], r = pixels[i+2], a = pixels[i+3];
      out[j] = r; out[j+1] = g; out[j+2] = b; out[j+3] = a;
    }
    ctx.putImageData(img, x, y);
  };
})();

