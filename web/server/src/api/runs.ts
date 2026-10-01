import { Router } from 'express';
import { asincrono } from './asincrono.js';
import mongoose from 'mongoose';
import { bucketAutomatico, parseBucket, truncarFecha, type Bucket } from '../domain/buckets.js';
import { Alert, CoilSample, Measure, Run, StatusSample } from '../models/index.js';

// Historicos. Todo lo que devuelve sale de `runs` y de las series asociadas;
// nada se calcula sobre la marcha desde MQTT.
export const runsRouter = Router();

const MAX_PAGINA = 100;

function objectId(valor: string): mongoose.Types.ObjectId | null {
  return mongoose.isValidObjectId(valor) ? new mongoose.Types.ObjectId(valor) : null;
}

/** Lista paginada, de la mas reciente a la mas vieja. */
runsRouter.get('/runs', asincrono(async (req, res) => {
  const limite = Math.min(MAX_PAGINA, Math.max(1, Number(req.query.limit ?? 20)));
  const pagina = Math.max(1, Number(req.query.page ?? 1));

  const filtro: Record<string, unknown> = {};
  if (typeof req.query.reason === 'string') filtro['result.reason'] = req.query.reason;
  if (typeof req.query.mode === 'string') filtro['targets.mode'] = req.query.mode;
  if (typeof req.query.state === 'string') filtro.state = req.query.state;
  // Las corridas de prueba NO se mezclan con los experimentos salvo que se
  // pidan: un listado que las incluyera por defecto haria que un promedio o
  // una comparacion hecha desde aca arrastre datos de banco sin que nadie lo
  // note. `type=all` las trae todas; sin `type`, todo menos las de prueba
  // (las `unknown` si aparecen: no se sabe que no sean experimentos).
  const tipo = typeof req.query.type === 'string' ? req.query.type : undefined;
  // `unknown` incluye las corridas guardadas ANTES de que existiera el campo:
  // no lo tienen, y `null` en un filtro de Mongo tambien matchea "ausente".
  if (tipo === 'normal' || tipo === 'test') filtro.runType = tipo;
  else if (tipo === 'unknown') filtro.runType = { $in: ['unknown', null] };
  else if (tipo !== 'all') filtro.runType = { $ne: 'test' };
  if (typeof req.query.from === 'string' || typeof req.query.to === 'string') {
    const rango: Record<string, Date> = {};
    if (typeof req.query.from === 'string') rango.$gte = new Date(req.query.from);
    if (typeof req.query.to === 'string') rango.$lte = new Date(req.query.to);
    filtro.startedAt = rango;
  }

  const [items, total] = await Promise.all([
    Run.find(filtro).sort({ startedAt: -1 }).skip((pagina - 1) * limite).limit(limite).lean(),
    Run.countDocuments(filtro),
  ]);

  res.json({
    total, page: pagina, limit: limite,
    items: items.map((run) => ({
      id: run._id.toString(),
      startedAt: run.startedAt,
      endedAt: run.endedAt ?? null,
      state: run.state,
      runType: run.runType ?? 'unknown',
      // Las relajaciones van en la lista para poder marcar ahi mismo un
      // experimento que corrio con protecciones apagadas.
      relaxations: run.targets?.relaxations ?? null,
      configId: run.targets?.configId ?? null,
      // El modo y el motivo van en la LISTA, no solo en el detalle: son lo
      // que se busca al recorrerla (que corridas fueron control, cuales se
      // cortaron), y tener que abrir cada una para saberlo la volveria
      // inservible.
      mode: run.targets?.mode ?? null,
      reason: run.result?.reason ?? null,
      durationMinutes: run.targets?.dur ?? null,
      measureCount: run.stats?.measureCount ?? 0,
      alertCount: run.stats?.alertCount ?? 0,
    })),
  });
}));

/** Cabecera completa de una corrida: objetivos y resultado. */
runsRouter.get('/runs/:id', asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }

  const run = await Run.findById(id).lean();
  if (!run) { res.status(404).json({ error: 'corrida no encontrada' }); return; }

  res.json({
    id: run._id.toString(),
    deviceId: run.deviceId,
    startedAt: run.startedAt,
    endedAt: run.endedAt ?? null,
    state: run.state,
    runType: run.runType ?? 'unknown',
    targets: run.targets ?? null,
    result: run.result ?? null,
    stats: run.stats ?? null,
  });
}));

// Ventana de la corrida. Una corrida abierta o huerfana sin cierre se consulta
// hasta ahora; si no, la serie terminaria antes que los datos.
async function ventana(id: mongoose.Types.ObjectId) {
  const run = await Run.findById(id).lean();
  if (!run) return null;
  return { desde: run.startedAt, hasta: run.endedAt ?? new Date() };
}

function resolverBucket(req: { query: Record<string, unknown> }, desde: Date, hasta: Date): Bucket {
  return parseBucket(req.query.bucket) ?? bucketAutomatico(desde, hasta);
}

/**
 * Serie de mediciones agregada. Cada punto lleva promedio, minimo y maximo:
 * el promedio solo escondería los picos que disparan las alertas.
 */
runsRouter.get('/runs/:id/measures', asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }
  const rango = await ventana(id);
  if (!rango) { res.status(404).json({ error: 'corrida no encontrada' }); return; }

  const bucket = resolverBucket(req, rango.desde, rango.hasta);
  const puntos = await Measure.aggregate([
    { $match: { 'meta.runId': id } },
    { $group: {
      _id: truncarFecha(bucket),
      field: { $avg: '$magneticField' },
      fieldMin: { $min: '$magneticField' },
      fieldMax: { $max: '$magneticField' },
      temp: { $avg: '$temperature' },
      tempMin: { $min: '$temperature' },
      tempMax: { $max: '$temperature' },
      n: { $sum: 1 },
    } },
    { $sort: { _id: 1 } },
    { $project: { _id: 0, ts: '$_id', field: 1, fieldMin: 1, fieldMax: 1, temp: 1, tempMin: 1, tempMax: 1, n: 1 } },
  ]);

  res.json({ bucket: bucket.label, points: puntos });
}));

/** Corriente y duty por bobina, agregados igual que las mediciones. */
runsRouter.get('/runs/:id/coils', asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }
  const rango = await ventana(id);
  if (!rango) { res.status(404).json({ error: 'corrida no encontrada' }); return; }

  const bucket = resolverBucket(req, rango.desde, rango.hasta);
  const filas = await CoilSample.aggregate([
    { $match: { 'meta.runId': id } },
    { $unwind: '$coils' },
    { $group: {
      _id: { ts: truncarFecha(bucket), n: '$coils.n' },
      current: { $avg: '$coils.current' },
      duty: { $avg: '$coils.duty' },
    } },
    { $sort: { '_id.ts': 1, '_id.n': 1 } },
  ]);

  // Se devuelve un punto por instante con una clave por bobina, que es como lo
  // consume el grafico. Las bobinas presentes son las que reportaron: cuantas
  // hay montadas es un hecho de la placa, no un numero que el monitor fije.
  const porInstante = new Map<number, Record<string, unknown>>();
  const bobinas = new Set<number>();
  for (const fila of filas) {
    const ts = (fila._id.ts as Date).getTime();
    bobinas.add(fila._id.n);
    const punto: Record<string, unknown> = porInstante.get(ts) ?? { ts: fila._id.ts };
    punto[`c${fila._id.n}`] = fila.current;
    punto[`d${fila._id.n}`] = fila.duty;
    porInstante.set(ts, punto);
  }

  res.json({
    bucket: bucket.label,
    coils: [...bobinas].sort((a, b) => a - b),
    points: [...porInstante.entries()].sort(([a], [b]) => a - b).map(([, punto]) => punto),
  });
}));

/** Progreso y salud a lo largo de la corrida, sin agregar: son pocos. */
runsRouter.get('/runs/:id/statuses', asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }

  const puntos = await StatusSample.find({ 'meta.runId': id }).sort({ ts: 1 }).lean();
  res.json({
    points: puntos.map((punto) => ({
      ts: punto.ts, health: punto.health, progress: punto.progress,
      elapsedSeconds: punto.elapsedSeconds, remainingSeconds: punto.remainingSeconds,
      state: punto.state, megaOk: punto.megaOk,
    })),
  });
}));

/** Todas las alertas de la corrida, en orden cronologico. */
runsRouter.get('/runs/:id/alerts', asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }

  const alertas = await Alert.find({ runId: id }).sort({ ts: 1 }).lean();
  res.json({
    items: alertas.map((alerta) => ({
      ts: alerta.ts, source: alerta.source, type: alerta.type,
      count: alerta.count, limit: alerta.limit,
    })),
  });
}));

/**
 * Exportacion para analisis fuera del monitor. Va SIN AGREGAR: quien se lleva
 * los datos a una planilla quiere las mediciones como se tomaron, no el
 * promedio que eligio el grafico.
 */
runsRouter.get('/runs/:id/export.csv', asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }
  const run = await Run.findById(id).lean();
  if (!run) { res.status(404).json({ error: 'corrida no encontrada' }); return; }

  const muestras = await Measure.find({ 'meta.runId': id }).sort({ ts: 1 }).lean();

  const lineas = ['ts,campo_mT,temperatura_C'];
  for (const muestra of muestras) {
    lineas.push([
      muestra.ts.toISOString(),
      muestra.magneticField ?? '',
      muestra.temperature ?? '',
    ].join(','));
  }

  const nombre = `biosoft-${run.startedAt.toISOString().slice(0, 19).replace(/[:T]/g, '-')}.csv`;
  res.setHeader('Content-Type', 'text/csv; charset=utf-8');
  res.setHeader('Content-Disposition', `attachment; filename="${nombre}"`);
  res.send(lineas.join('\n'));
}));
