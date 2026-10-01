// Simulador de la placa: publica una corrida entera contra un broker local,
// con los mismos topics, las mismas claves y los mismos flags de retain que
// esp32/src/topics.hpp y MySystem::_publish*().
//
// Es lo que permite trabajar sin hardware encendido, y sobre todo lo que hace
// reproducible el caso dificil: un experimento cortado por alerta critica, que
// con la placa real hay que provocar de verdad.
//
//   node tools/simulador.js                 # corrida normal, 60 s comprimidos
//   node tools/simulador.js --reason critical
//   node tools/simulador.js --mode null      # grupo control
//   node tools/simulador.js --type normal    # marcada como experimento
//   node tools/simulador.js --type none      # sin marca TEST (firmware viejo)
//   node tools/simulador.js --url mqtt://localhost:1883 --step 500
//
// Contra el broker real (OJO: son datos falsos entrando al historico real; por
// eso salen marcados como prueba salvo que se pida otra cosa con --type):
//
//   node tools/simulador.js --url mqtts://host:8883 --user X --pass Y
//
// El usuario y la clave van por separado y NO dentro de la URL: ahi cualquier
// @ / : o # tendria que ir escapado, y una clave mal escapada no da error --
// el broker simplemente rechaza la conexion y parece que el host esta mal.
//
// OJO: usa un clientId propio. Dos clientes con el mismo clientId se
// desconectan mutuamente, asi que nunca el de la placa ni el del backend.

const mqtt = require('mqtt');

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(`--${name}`);
  return index >= 0 && args[index + 1] ? args[index + 1] : fallback;
}

const url = arg('url', 'mqtt://localhost:1883');
const stepMs = Number(arg('step', '1000'));   // cada cuanto una tanda periodica
const steps = Number(arg('steps', '20'));     // cuantas tandas dura la corrida
const reason = arg('reason', 'completed');    // completed | critical | stopped
// El MODO como lo publica la placa: el campo `value` de
// ConfigurationOptions::optionsFieldMode, que es "x" o "null" -- NO la etiqueta
// ("Campo X"). Este simulador mandaba la etiqueta, y por eso el monitor mostraba
// bien los datos falsos y habria mostrado mal los reales: un campo nulo se
// habria visto como campo X, que es la unica distincion que el experimento no
// puede perder.
const mode = arg('mode', 'x');                // x | null
// La marca TEST que la placa saca de `runType` en la tarjeta SD. Por defecto
// `test`: son datos inventados, y entrar al historico como experimento es
// justo lo que la marca existe para evitar.
const runType = arg('type', 'test');          // test | normal | none
const marca = runType === 'none' ? {} : { TEST: runType !== 'normal' };
const base = arg('base', 'biosoft/telemetry');

const T = {
  measures: `${base}/measures`,
  coils: `${base}/coils`,
  status: `${base}/status`,
  targets: `${base}/targets`,
  alerts: `${base}/alerts`,
  result: `${base}/result`,
};

// El clientId lleva la marca de tiempo para no chocar nunca con el de la placa
// ni con el del backend: dos clientes con el mismo id se desconectan
// mutuamente.
const client = mqtt.connect(url, {
  clientId: `biosoft-simulador-${Date.now()}`,
  username: arg('user', undefined),
  password: arg('pass', undefined),
});

function publish(topic, payload, retain) {
  const json = JSON.stringify(payload);
  client.publish(topic, json, { qos: 0, retain });
  console.log(`-> ${topic} ${retain ? '(retain) ' : ''}${json}`);
}

function hhmmss(totalSeconds) {
  const h = String(Math.floor(totalSeconds / 3600)).padStart(2, '0');
  const m = String(Math.floor((totalSeconds % 3600) / 60)).padStart(2, '0');
  const s = String(totalSeconds % 60).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Duracion "real" que dice el experimento, independiente de lo comprimido que
// corra el simulador: el backend no tiene que notar la diferencia.
const durationMinutes = 60;
const secondsPerStep = Math.round((durationMinutes * 60) / steps);

async function run() {
  publish(T.targets, {
    MODE: mode, CEM: 1.5, FREQ: 50, DUR: durationMinutes, TOL: 10,
    TNMIN: 20, TNMAX: 30, TCMIN: 15, TCMAX: 35, ...marca,
  }, true);

  let health = 'normal';
  let alerts = 0;

  for (let step = 1; step <= steps; step++) {
    await sleep(stepMs);
    const elapsed = step * secondsPerStep;
    const progress = Math.round((step / steps) * 100);

    publish(T.measures, {
      CEM1: Number((1.5 + (Math.random() - 0.5) * 0.12).toFixed(4)),
      TEMP1: Number((24 + step * 0.2 + Math.random()).toFixed(2)),
    }, true);

    publish(T.coils, {
      c1: Number((0.8 + Math.random() * 0.05).toFixed(2)),
      d1: Number((42 + Math.random() * 3).toFixed(1)),
      c2: Number((0.79 + Math.random() * 0.05).toFixed(2)),
      d2: Number((43 + Math.random() * 3).toFixed(1)),
    }, true);

    publish(T.status, {
      ESTADO: health, PROGRESS: progress, ELAPSED_TIME: hhmmss(elapsed),
      REMAINING: durationMinutes * 60 - elapsed, STATE: 'running', MEGA: true,
    }, true);

    // Una alerta a mitad de corrida, y si la corrida es "critical" se van
    // acumulando hasta el limite, que es lo que corta el experimento.
    if (reason === 'critical' && step >= steps / 2) {
      alerts++;
      health = 'critical';
      publish(T.alerts, { SRC: 'TEMP1', TYPE: 'critical', COUNT: alerts, LIMIT: 4 }, false);
      if (alerts >= 4) break;
    } else if (step === Math.floor(steps / 2)) {
      health = 'warning';
      publish(T.alerts, { SRC: 'CEM1', TYPE: 'streak', COUNT: 1, LIMIT: 3 }, false);
    }
  }

  const result =
    reason === 'critical'
      ? { REASON: 'critical', DESC: 'TEMP1: limite de alertas critical alcanzado (4/4)',
          SRC: 'TEMP1', TYPE: 'critical', COUNT: 4, LIMIT: 4 }
      : reason === 'stopped'
        ? { REASON: 'stopped', DESC: 'Detenido por el operador', emerg: false }
        : { REASON: 'completed', DESC: 'Duracion alcanzada' };

  publish(T.result, {
    ...result,
    PROGRESS: reason === 'completed' ? 100 : 55,
    ELAPSED: hhmmss(reason === 'completed' ? durationMinutes * 60 : Math.round(durationMinutes * 33)),
    MEAN: 1.49,
    ...marca,
  }, true);

  await sleep(300);
  client.end();
  console.log('\nCorrida simulada terminada.');
}

client.on('connect', () => {
  console.log(`[simulador] conectado a ${url} — corrida "${reason}", ${steps} tandas cada ${stepMs} ms\n`);
  run().catch((error) => { console.error(error); client.end(); });
});
client.on('error', (error) => { console.error('[simulador] error:', error.message); process.exit(1); });
