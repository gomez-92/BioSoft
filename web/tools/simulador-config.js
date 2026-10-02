// Simula a la placa en la configuracion remota (tarjeta 24), con los mismos
// topics, formato de bloques y estados que esp32/src/remoteconfig.hpp:
//
//  - publica una configuracion vigente en bloques RETENIDOS
//    (biosoft/config/current/<i> y .../meta);
//  - escucha biosoft/config/set, confirma cada bloque en biosoft/config/status
//    y al completar responde "aceptada" con el id nuevo;
//  - contesta el ping del monitor (biosoft/ping -> biosoft/pong), como
//    esp32/src/remoteping.hpp.
//
//   node tools/simulador-config.js                       # broker local
//   node tools/simulador-config.js --ocupada             # rechaza: experimento en curso
//   node tools/simulador-config.js --perder 2            # no confirma el bloque 2
//   node tools/simulador-config.js --sin-pong            # ignora los pings (placa colgada)
//   node tools/simulador-config.js --url mqtts://host:8883 --user X --pass Y
//
// La configuracion vigente sale de docs/config.example.json (sin wifi ni
// broker, como la publica la placa). Queda corriendo hasta Ctrl+C.

const fs = require('fs');
const path = require('path');
const mqtt = require('mqtt');

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(`--${name}`);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
}
const flag = (name) => args.includes(`--${name}`);

const url = arg('url', 'mqtt://localhost:1883');
const ocupada = flag('ocupada');
const perder = Number(arg('perder', '-1'));
const sinPong = flag('sin-pong');
const arranque = Date.now();

// Mismo tope y misma regla que la placa (RemoteConfig::chunkLength).
const MAX = 340;
const escapado = (c) => (c === '"' || c === '\\' ? 2 : c.charCodeAt(0) < 0x20 ? 6 : 1);
function bloques(texto) {
  const out = [];
  let i = 0;
  while (i < texto.length) {
    let e = 0, j = i;
    while (j < texto.length && e + escapado(texto[j]) <= MAX) e += escapado(texto[j++]);
    out.push(texto.slice(i, j));
    i = j;
  }
  return out;
}

const TABLA = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(texto) {
  let crc = 0xFFFFFFFF;
  for (const b of Buffer.from(texto, 'utf8')) crc = TABLA[(crc ^ b) & 0xFF] ^ (crc >>> 8);
  return ((crc ^ 0xFFFFFFFF) >>> 0).toString(16).padStart(8, '0');
}

const ejemplo = path.join(__dirname, '..', '..', 'docs', 'config.example.json');
const { wifi: _w, broker: _b, ...sinCred } = JSON.parse(fs.readFileSync(ejemplo, 'utf8'));
const texto = JSON.stringify(sinCred);
const id = crc32(texto);

const client = mqtt.connect(url, {
  clientId: `biosoft-sim-config-${Math.random().toString(16).slice(2, 8)}`,
  username: arg('user'),
  password: arg('pass'),
});

client.on('connect', () => {
  console.log(`Conectado a ${url}. Configuracion vigente: ${id}`);
  const partes = bloques(texto);
  partes.forEach((d, i) => client.publish(
    `biosoft/config/current/${i}`, JSON.stringify({ id, i, n: partes.length, d }), { retain: true },
  ));
  client.publish('biosoft/config/current/meta', JSON.stringify({ id, n: partes.length, len: texto.length, src: 'ok' }), { retain: true });
  client.subscribe('biosoft/config/set');
  client.subscribe('biosoft/ping');
});

let recibido = [];
let pedido = null;
const estado = (payload) => client.publish('biosoft/config/status', JSON.stringify(payload));

client.on('message', (topic, raw) => {
  if (topic === 'biosoft/ping') {
    if (sinPong) return;
    const { r } = JSON.parse(raw.toString());
    client.publish('biosoft/pong', JSON.stringify({
      r, st: ocupada ? 'running' : 'ready', cfg: id, cfgst: 'ok', mega: true, up: Math.floor((Date.now() - arranque) / 1000),
    }));
    return;
  }
  const { r, i, n, d } = JSON.parse(raw.toString());
  if (ocupada) { estado({ r, st: 'ocupada', msg: 'hay un experimento en curso' }); return; }
  if (i === 0) { pedido = r; recibido = []; }
  if (r !== pedido) { estado({ r, st: 'incompleta', msg: 'bloque de un pedido que no esta en curso' }); return; }
  if (i === perder) { console.log(`(no confirmo el bloque ${i})`); return; }
  recibido[i] = d;
  console.log(`  ${r}: bloque ${i + 1}/${n}`);
  if (i + 1 < n) { estado({ r, st: 'parcial', i }); return; }

  const nuevo = recibido.join('');
  try {
    const cfg = JSON.parse(nuevo);
    if (cfg.schemaVersion !== 1) throw new Error('schemaVersion no soportada');
    const nuevoId = crc32(JSON.stringify(cfg));
    estado({ r, st: 'aceptada', id: nuevoId, msg: 'se aplica en el proximo reinicio' });
    console.log(`  ${r}: aceptada (${nuevoId})`);
  } catch (e) {
    estado({ r, st: 'invalida', msg: e.message });
  }
});
