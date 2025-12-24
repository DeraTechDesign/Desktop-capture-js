// Simple DiodeJS bind script
// Usage:
//   node examples/bind.js <deviceIdHex> [localPort=8081] [targetPort=8081] [protocol=tls]
// or env vars:
//   DIODE_DEVICE_ID, LOCAL_PORT, TARGET_PORT, DIODE_PROTOCOL, DIODE_HOST, DIODE_PORT

const { DiodeConnection, BindPort } = require('diodejs');

const args = process.argv.slice(2);

const deviceIdHex = (args[0] || process.env.DIODE_DEVICE_ID || process.env.DEVICE_ID || process.env.DIODE_DEVICE || '').trim();
const LOCAL_PORT = parseInt(args[1] || process.env.LOCAL_PORT || '8081', 10);
const TARGET_PORT = parseInt(args[2] || process.env.TARGET_PORT || String(LOCAL_PORT), 10);
const PROTOCOL = String(args[3] || process.env.DIODE_PROTOCOL || process.env.PROTOCOL || 'tls').toLowerCase();

const DIODE_HOST = process.env.DIODE_HOST || 'us2.prenet.diode.io';
const DIODE_PORT = parseInt(process.env.DIODE_PORT || '41046', 10);

if (!deviceIdHex) {
  console.error('Usage: node examples/bind.js <deviceIdHex> [localPort=8081] [targetPort=8081] [protocol=tls]');
  console.error('Or set env: DIODE_DEVICE_ID, LOCAL_PORT, TARGET_PORT, DIODE_PROTOCOL');
  process.exit(1);
}

let connection;
let portBinder;

async function main() {
  connection = new DiodeConnection(DIODE_HOST, DIODE_PORT);
  await connection.connect();

  try {
    const addr = await connection.getEthereumAddress();
    console.log(`Diode device address (this machine): ${addr}`);
  } catch (e) {
    console.warn('Could not fetch local Diode address:', e.message || e);
  }

  const portsConfig = {
    [LOCAL_PORT]: { targetPort: TARGET_PORT, deviceIdHex, protocol: PROTOCOL }
  };

  console.log(`Binding local ${LOCAL_PORT} -> ${deviceIdHex}:${TARGET_PORT} via ${PROTOCOL.toUpperCase()} on ${DIODE_HOST}:${DIODE_PORT}`);
  portBinder = new BindPort(connection, portsConfig);
  portBinder.bind();

  console.log(`Ready. Access remote service at http://localhost:${LOCAL_PORT} (if HTTP)`);
}

main().catch((err) => {
  console.error('Bind error:', err);
  try { if (connection) connection.close(); } catch {}
  process.exit(1);
});

process.on('SIGINT', () => {
  console.log('\nStopping...');
  try { if (portBinder) portBinder.closeAllServers(); } catch {}
  try { if (connection) connection.close(); } catch {}
  process.exit(0);
});

