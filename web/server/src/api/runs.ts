import { Router } from 'express';
import { asincrono } from './asincrono.js';
import mongoose from 'mongoose';
import { requireAdmin } from '../auth/middleware.js';
import { valorAntesDeAlerta } from '../domain/alertas.js';
import { bucketAutomatico, parseBucket, truncarFecha, type Bucket } from '../domain/buckets.js';
import { getCurrentRunId } from '../domain/runtracker.js';
import { Alert, CoilSample, Measure, Run, StatusSample } from '../models/index.js';

// Historicos. Todo lo que devuelve sale de `runs` y de las series asociadas;
// nada se calcula sobre la marcha desde MQTT.
export const runsRouter = Router();

const MAX_PAGINA = 100;
// Tope de muestras sin agregar. A 2 s por muestra son ~5,5 h de corrida; mas
// largo que eso, la pantalla En curso pide la serie agregada.
const MAX_CRUDOS = 10_000;

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

  // Cuantas hay de cada tipo con los MISMOS filtros salvo el tipo: es lo que
  // muestran las pestañas Experimentos / Pruebas, para que las pruebas se
  // vean aunque no esten seleccionadas.
  const { runType: _tipo, ...sinTipo } = filtro;
  const [items, total, porTipo] = await Promise.all([
    Run.find(filtro).sort({ startedAt: -1 }).skip((pagina - 1) * limite).limit(limite).lean(),
    Run.countDocuments(filtro),
    Run.aggregate<{ _id: string | null; n: number }>([
      { $match: sinTipo },
      { $group: { _id: '$runType', n: { $sum: 1 } } },
    ]),
  ]);
  const byType = { normal: 0, test: 0, unknown: 0 };
  for (const { _id, n } of porTipo) {
    if (_id === 'normal' || _id === 'test') byType[_id] += n;
    else byType.unknown += n;
  }

  res.json({
    total, page: pagina, limit: limite, byType,
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
      durationMinutes: run.targets?.dur != null ? run.targets.dur / 60_000 : null,
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

  // `bucket=raw`: las muestras tal como llegaron, sin agregar. Es lo que pide
  // la pantalla En curso, que grafica la corrida mientras pasa y despues le
  // va sumando los puntos en vivo: si arrancara agregada, el tramo historico
  // y el vivo tendrian resoluciones distintas en el mismo dibujo.
  if (req.query.bucket === 'raw') {
    const muestras = await Measure.find({ 'meta.runId': id }).sort({ ts: 1 }).limit(MAX_CRUDOS).lean();
    res.json({
      bucket: 'raw',
      points: muestras.map((m) => ({
        ts: m.ts, field: m.magneticField ?? undefined, temp: m.temperature ?? undefined,
      })),
    });
    return;
  }

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

  if (req.query.bucket === 'raw') {
    const muestras = await CoilSample.find({ 'meta.runId': id }).sort({ ts: 1 }).limit(MAX_CRUDOS).lean();
    const presentes = new Set<number>();
    const puntos = muestras.map((m) => {
      const punto: Record<string, unknown> = { ts: m.ts };
      for (const coil of m.coils) {
        presentes.add(coil.n);
        punto[`c${coil.n}`] = coil.current ?? undefined;
        punto[`d${coil.n}`] = coil.duty ?? undefined;
      }
      return punto;
    });
    res.json({ bucket: 'raw', coils: [...presentes].sort((a, b) => a - b), points: puntos });
    return;
  }

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
  // Cada alerta lleva la ultima medicion de su fuente antes de levantarse
  // (ver domain/alertas.ts). Son pocas por corrida: una consulta por alerta.
  const items = await Promise.all(alertas.map(async (alerta) => ({
    ts: alerta.ts, source: alerta.source, type: alerta.type,
    count: alerta.count, limit: alerta.limit,
    ...(await valorAntesDeAlerta(id, alerta.source, alerta.ts)),
  })));
  res.json({ items });
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

/**
 * Borrado de corridas, con todas sus series y alertas. Solo administradores.
 *
 * La corrida EN CURSO no se borra: el ingestor la tiene abierta en memoria y
 * seguiria escribiendo muestras con ese runId, que quedarian huerfanas de una
 * corrida inexistente. Se puede borrar una vez que termine (o que el barrido
 * la cierre como huerfana).
 *
 * Es irreversible y a proposito no hay papelera: lo que se borra es casi
 * siempre una prueba de banco, y una papelera seria una segunda base que nadie
 * vacia. La confirmacion la pide la pantalla.
 */
async function borrarCorridas(ids: mongoose.Types.ObjectId[]): Promise<{ borradas: number; enCurso: boolean }> {
  const abierta = getCurrentRunId();
  const enCurso = abierta !== null && ids.some((id) => id.equals(abierta));
  const aBorrar = ids.filter((id) => !(abierta && id.equals(abierta)));
  if (aBorrar.length === 0) return { borradas: 0, enCurso };

  // Tambien se excluyen por estado, por si el barrido y el tracker no
  // coinciden en este instante.
  const corridas = await Run.find({ _id: { $in: aBorrar }, state: { $ne: 'running' } }).select('_id').lean();
  const confirmadas = corridas.map((run) => run._id as mongoose.Types.ObjectId);
  if (confirmadas.length === 0) return { borradas: 0, enCurso: true };

  // Primero las series y despues la corrida: si algo falla a mitad de camino,
  // queda una corrida con menos datos (visible, se puede reintentar) y no
  // muestras sueltas apuntando a una corrida que ya no existe.
  await Promise.all([
    Measure.deleteMany({ 'meta.runId': { $in: confirmadas } }),
    CoilSample.deleteMany({ 'meta.runId': { $in: confirmadas } }),
    StatusSample.deleteMany({ 'meta.runId': { $in: confirmadas } }),
    Alert.deleteMany({ runId: { $in: confirmadas } }),
  ]);
  const { deletedCount } = await Run.deleteMany({ _id: { $in: confirmadas } });
  return { borradas: deletedCount, enCurso: enCurso || confirmadas.length < aBorrar.length };
}

runsRouter.delete('/runs/:id', requireAdmin, asincrono(async (req, res) => {
  const id = objectId(req.params.id);
  if (!id) { res.status(400).json({ error: 'id invalido' }); return; }
  if (!(await Run.exists({ _id: id }))) { res.status(404).json({ error: 'corrida no encontrada' }); return; }

  const { borradas, enCurso } = await borrarCorridas([id]);
  if (borradas === 0 && enCurso) {
    res.status(409).json({ error: 'la corrida esta en curso: se puede borrar cuando termine' });
    return;
  }
  console.log(`[corridas] ${req.user?.username} borro la corrida ${id.toString()}`);
  res.json({ ok: true, deleted: borradas });
}));

/** Varias de una vez: lo comun es limpiar una tanda de pruebas de banco. */
runsRouter.post('/runs/delete', requireAdmin, asincrono(async (req, res) => {
  const crudos: unknown[] = Array.isArray(req.body?.ids) ? req.body.ids : [];
  const ids = crudos.map((v) => (typeof v === 'string' ? objectId(v) : null));
  if (ids.length === 0 || ids.length > MAX_PAGINA || ids.some((id) => id === null)) {
    res.status(400).json({ error: `ids invalidos (entre 1 y ${MAX_PAGINA})` });
    return;
  }
  const { borradas, enCurso } = await borrarCorridas(ids as mongoose.Types.ObjectId[]);
  console.log(`[corridas] ${req.user?.username} borro ${borradas} corrida(s)`);
  res.json({ ok: true, deleted: borradas, skippedRunning: enCurso });
}));
