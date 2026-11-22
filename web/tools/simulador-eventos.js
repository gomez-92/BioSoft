// Simulador de la bitacora de la placa (esp32/src/eventlog.hpp): publica en
// biosoft/log una tanda de eventos como los que saca la placa, para ver la
// pagina Bitacora y el detalle de una corrida sin hardware.
//
//   node tools/simulador-eventos.js                    # una tanda tipica
//   node tools/simulador-eventos.js --atrasados 120    # como si hubieran esperado
//                                                      # 120 s en el buffer
//   node tools/simulador-eventos.js --url mqtts://host:8883 --user X --pass Y
//
// Mismo formato que la placa: {n, ts, up, lv, c, m, d}, QoS 0, sin retain.

const mqtt = require('mqtt');

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(`--${name}`);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
}

const url = arg('url', 'mqtt://localhost:1883');
const topic = arg('topic', 'biosoft/log');
const atrasoMs = Number(arg('atrasados', '0')) * 1000;

// Lo que pasaria en una mala tarde: arranque, el broker que se cae y vuelve
// (con telemetria perdida), un reinicio del Mega en plena corrida y la
// corrida cerrada como perdida.
const TANDA = [
  { lv: 'I', c: 'arranque', m: 'arranque causa=encendido config=ok configId=1a2b3c4d heap_libre=187344 B' },
  { lv: 'I', c: 'enlace_ok', m: 'Mega conectado' },
  { lv: 'W', c: 'broker_caido', m: 'desconectado: conexion perdida (state=-3) tras 842 s conectado wifi=ok rssi=-71 dBm heap_libre=98120 B' },
  { lv: 'I', c: 'broker_ok', m: 'conectado tras 37 s sin broker heap_libre=97456 B' },
  { lv: 'W', c: 'cola_llena', m: 'cola de publicacion normal: se descartaron 14 mensajes' },
  { lv: 'E', c: 'mega_reinicio', m: 'el Mega se reinicio: up 1204 s -> 2 s etapa=6' },
  { lv: 'E', c: 'corrida_perdida', m: 'corrida perdida: el Mega se reinicio durante la corrida (etapa=6)' },
  { lv: 'W', c: 'mega_margen_bajo', m: 'margen heap/stack del Mega bajo minimo=412 B (libre 980 B)', d: 2 },
];

const client = mqtt.connect(url, {
  clientId: `biosoft-simulador-eventos-${Date.now()}`,
  username: arg('user', undefined),
  password: arg('pass', undefined),
});

client.on('error', (error) => { console.error('[sim-eventos]', error.message); process.exit(1); });
client.on('connect', async () => {
  const ahora = Date.now() - atrasoMs;
  for (const [i, evento] of TANDA.entries()) {
    // Un evento cada 20 s "de placa", hacia atras desde ahora.
    const ts = ahora - (TANDA.length - 1 - i) * 20_000;
    const payload = { n: i + 1, ts, up: 5 + i * 20, ...evento };
    await client.publishAsync(topic, JSON.stringify(payload), { qos: 0, retain: false });
    console.log(`[sim-eventos] ${evento.lv} ${evento.c}`);
  }
  await client.endAsync();
});
