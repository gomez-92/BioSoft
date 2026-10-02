import { Schema, model, type InferSchemaType } from 'mongoose';

// Modelo de datos del monitor. La coleccion central es `runs` (un
// experimento); el resto son muestras que apuntan a ella.
//
// Las tres series (measures, coils, statuses) son TIME-SERIES COLLECTIONS de
// Mongo: comprimen mucho mejor y consultan por rango, que es todo lo que se
// les pide. La contracara es que no se actualizan como documentos normales, y
// por eso el `runId` se escribe EN EL INSERT y nunca despues: es posible
// porque `targets` llega antes que cualquier muestra (MySystem lo publica al
// entrar en Running), asi que cuando la primera muestra aterriza ya se sabe a
// que corrida pertenece. Una muestra que llega sin corrida abierta se guarda
// con runId null y se queda asi.
//
// `alerts` y `runs` son colecciones normales: son pocas y se consultan de a
// una.

const { Types } = Schema;

/* ------------------------------ runs ------------------------------ */

const targetsSchema = new Schema({
  mode: String,    // "campo X" | "campo nulo" -- la diferencia entre grupo tratado y control
  cem: Number,     // mT
  freq: Number,    // Hz
  dur: Number,     // minutos
  tol: Number,     // %
  tnmin: Number, tnmax: Number,   // rango normal de temperatura
  tcmin: Number, tcmax: Number,   // rango critico
  test: Boolean,   // la marca TEST tal como llego (ausente en firmwares viejos)
  configId: String,     // CRC32 de la configuracion de la SD, o "default" (tarjeta 23)
  relaxations: Number,  // mascara de bits de relajaciones activas (ver client/src/lib/format.ts)
}, { _id: false });

const resultSchema = new Schema({
  reason: String,          // "completed" | "critical" | "stopped"
  description: String,     // la prosa que arma el Mega
  progressPercent: Number,
  elapsedSeconds: Number,
  elapsedText: String,     // "hh:mm:ss" tal como llego
  meanMagneticField: Number,
  // Los datos crudos del corte, presentes solo cuando aplican. Con esto el
  // detalle se puede reconstruir sin depender de la prosa.
  source: String, type: String, count: Number, limit: Number,
  emergency: Boolean,      // paro fisico vs. Detener en pantalla
  test: Boolean,
}, { _id: false });

const runSchema = new Schema({
  deviceId: { type: String, required: true, index: true },
  startedAt: { type: Date, required: true, index: true },
  endedAt: Date,
  // "running": en curso. "finished": llego su result. "orphan": quedo sin
  // cierre (backend caido, placa reiniciada) o aparecio un result sin corrida
  // abierta. Un huerfano NO es basura: sus muestras son datos reales.
  state: { type: String, enum: ['running', 'finished', 'orphan'], required: true, index: true },
  // "normal": experimento declarado como tal en la tarjeta SD (`runType`).
  // "test": corrida de banco. "unknown": llego sin la marca TEST (firmware
  // anterior a la marca o simulador viejo). Es lo que separa datos
  // cientificos de datos de prueba, por eso vive arriba y con indice, y no
  // solo dentro de targets: el historial filtra por esto en cada consulta.
  runType: { type: String, enum: ['normal', 'test', 'unknown'], default: 'unknown', index: true },
  targets: targetsSchema,
  result: resultSchema,
  stats: {
    measureCount: { type: Number, default: 0 },
    alertCount: { type: Number, default: 0 },
    lastSampleAt: Date,
  },
}, { timestamps: true });

// Solo puede haber UNA corrida abierta por equipo. El indice parcial lo
// garantiza en la base y no solo en la logica: dos `targets` casi simultaneos
// (una placa que reintenta) no pueden dejar dos corridas vivas.
runSchema.index(
  { deviceId: 1, state: 1 },
  { unique: true, partialFilterExpression: { state: 'running' } },
);

export const Run = model('Run', runSchema);
export type RunDoc = InferSchemaType<typeof runSchema>;

/* --------------------------- series de tiempo --------------------------- */

// El metaField agrupa fisicamente los documentos: Mongo guarda juntas las
// muestras que comparten meta, que es justo como se consultan (todas las de
// una corrida).
const timeseries = (metaField: string) => ({
  timeseries: { timeField: 'ts', metaField, granularity: 'seconds' as const },
  autoCreate: true,
  versionKey: false as const,
});

const measureSchema = new Schema({
  ts: { type: Date, required: true },
  meta: {
    runId: { type: Types.ObjectId, ref: 'Run', default: null },
    deviceId: String,
  },
  magneticField: Number,   // mT
  temperature: Number,     // grados C
}, timeseries('meta'));

export const Measure = model('Measure', measureSchema);

const coilSampleSchema = new Schema({
  ts: { type: Date, required: true },
  meta: {
    runId: { type: Types.ObjectId, ref: 'Run', default: null },
    deviceId: String,
  },
  // Solo las bobinas que reportaron. El largo de este arreglo dice cuantas
  // bobinas hay montadas; cuatro posiciones fijas no distinguirian una bobina
  // ausente de una que mide cero.
  coils: [new Schema({
    n: { type: Number, required: true },
    current: Number,   // A
    duty: Number,      // %
  }, { _id: false })],
}, timeseries('meta'));

export const CoilSample = model('CoilSample', coilSampleSchema);

const statusSchema = new Schema({
  ts: { type: Date, required: true },
  meta: {
    runId: { type: Types.ObjectId, ref: 'Run', default: null },
    deviceId: String,
  },
  health: String,            // "normal" | "warning" | "critical"
  progress: Number,          // %
  elapsedSeconds: Number,
  elapsedText: String,
  remainingSeconds: Number,
  state: String,             // idle | ready | starting | running | stopping
  megaOk: Boolean,           // enlace serie ESP32 <-> Mega
}, timeseries('meta'));

export const StatusSample = model('StatusSample', statusSchema);

/* ------------------------------ alerts ------------------------------ */

const alertSchema = new Schema({
  ts: { type: Date, required: true, index: true },
  runId: { type: Types.ObjectId, ref: 'Run', default: null, index: true },
  deviceId: String,
  source: { type: String, required: true },   // CEM1 | TEMP1
  type: { type: String, required: true },     // critical | streak | frequency
  count: { type: Number, required: true },
  limit: { type: Number, required: true },
}, { versionKey: false });

export const Alert = model('Alert', alertSchema);

/* --------------------- configuracion remota (tarjeta 24) --------------------- */

// La configuracion con la que corre (o corrio) la placa, indexada por el
// configId que la placa calcula (tarjeta 23). `text` es el JSON exacto que la
// placa hasheo -- sin wifi ni broker --, y su CRC32 se verifico contra el id
// al reensamblarlo. "default" = defaults compilados, sin texto.
const configSnapshotSchema = new Schema({
  configId: { type: String, required: true, unique: true },
  text: { type: String, default: '' },
  content: { type: Schema.Types.Mixed, default: null },
  lastReportedAt: { type: Date, index: true },
  // Por que corre esta configuracion (ConfigLoader::loadStatus en la placa):
  // ok / nosd / nofile / unreadable / invalid / schema. Importa sobre todo
  // en "default", que junta cinco causas. Es el del ULTIMO reporte; null si
  // la placa es anterior a este dato.
  loadStatus: { type: String, default: null },
}, { timestamps: true });

export const ConfigSnapshot = model('ConfigSnapshot', configSnapshotSchema);

// Un envio de configuracion desde el monitor a la placa, con su estado.
//   enviando / parcial -> en curso (parcial = la placa confirmo sentChunks)
//   aceptada           -> grabada en la SD; se aplica en el proximo reinicio
//   invalida / ocupada / incompleta / error / no_entregada -> no se toco nada
const configRequestSchema = new Schema({
  requestId: { type: String, required: true, unique: true },
  user: String,
  text: String,
  chunks: Number,
  sentChunks: { type: Number, default: 0 },
  state: { type: String, required: true, index: true },
  message: String,
  newConfigId: String,
}, { timestamps: true });

export const ConfigRequest = model('ConfigRequest', configRequestSchema);
