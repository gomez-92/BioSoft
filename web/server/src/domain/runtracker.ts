import mongoose from 'mongoose';
import { config } from '../config.js';
import { Run } from '../models/index.js';
import { runTypeFrom, type ParsedResult, type ParsedTargets } from './telemetry.js';

// La placa NO manda un identificador de corrida: publica `targets` al entrar
// en Running y `result` al cortar, y en el medio muestras sueltas. Que esas
// muestras pertenezcan a un experimento es una inferencia que se hace aca, y
// es lo unico que convierte un flujo de numeros en un historico consultable.
//
// Reglas, todas con su razon:
//
//  - `targets` abre una corrida. Si habia una abierta, esa quedo sin cierre
//    (backend caido, placa reiniciada, corte de luz): se marca `orphan` y se
//    cierra con la fecha de su ultimo dato, no con la de ahora, que seria
//    inventar un experimento mas largo de lo que fue.
//  - `result` cierra la corrida abierta. Si no hay ninguna, igual se crea una
//    `orphan` con el resultado: el motivo del corte es el unico dato que nadie
//    mas reporta, y perderlo hace que una corrida cortada por alerta critica
//    se vea desde afuera igual que una que termino bien.
//  - Una corrida sin datos por mas de `staleAfterSeconds` la cierra el barrido
//    como `orphan`. Sin eso, un experimento que murio sin `result` se queda
//    abierto para siempre y se traga las muestras de todos los que vengan.
//  - Un huerfano NO es basura: sus muestras son mediciones reales.
//  - El tipo de corrida (normal / prueba) sale de la marca TEST de `targets`.
//    Si no vino ahi, se toma la de `result`; las dos las publica la misma
//    placa con la misma configuracion, asi que no pueden contradecirse sin
//    un reinicio de por medio -- y un reinicio ya parte la corrida.
//
// El estado vive en memoria PERO se reconstruye de la base al arrancar
// (`initRunTracker`), asi que reiniciar el backend a mitad de un experimento
// retoma la misma corrida en vez de partirla en dos.

type ObjectId = mongoose.Types.ObjectId;

let currentRunId: ObjectId | null = null;

export function getCurrentRunId(): ObjectId | null {
  return currentRunId;
}

/**
 * Retoma la corrida abierta que haya quedado en la base. Se llama al arrancar,
 * antes de suscribirse al broker: si el backend se reinicio a mitad de un
 * experimento, las muestras que sigan llegando tienen que caer en la misma
 * corrida y no en una nueva.
 */
export async function initRunTracker(): Promise<void> {
  const open = await Run.findOne({ deviceId: config.deviceId, state: 'running' }).lean();
  currentRunId = (open?._id as ObjectId) ?? null;
  if (currentRunId) {
    console.log(`[corridas] se retoma la corrida abierta ${currentRunId.toString()}`);
  }
}

// Cierra una corrida que quedo sin `result`. La fecha de cierre es la de su
// ultimo dato: es lo ultimo que se sabe con certeza que ocurrio.
async function orphanRun(runId: ObjectId, motivo: string): Promise<void> {
  const run = await Run.findById(runId);
  if (!run || run.state !== 'running') return;
  run.state = 'orphan';
  run.endedAt = run.stats?.lastSampleAt ?? run.startedAt;
  await run.save();
  console.warn(`[corridas] ${runId.toString()} marcada huerfana (${motivo})`);
}

export async function openRun(targets: ParsedTargets, at: Date): Promise<ObjectId> {
  if (currentRunId) await orphanRun(currentRunId, 'llego un targets nuevo sin result previo');

  const run = await Run.create({
    deviceId: config.deviceId,
    startedAt: at,
    state: 'running',
    runType: runTypeFrom(targets.test),
    targets,
    stats: { measureCount: 0, alertCount: 0 },
  });
  currentRunId = run._id as ObjectId;
  console.log(
    `[corridas] abierta ${currentRunId.toString()} (modo: ${targets.mode ?? '?'}, tipo: ${run.runType})`,
  );
  return currentRunId;
}

export async function closeRun(result: ParsedResult, at: Date): Promise<ObjectId> {
  if (currentRunId) {
    await Run.updateOne(
      { _id: currentRunId },
      { $set: { state: 'finished', endedAt: at, result } },
    );
    // Solo completa un tipo que faltaba: nunca pisa el que dio `targets`.
    if (result.test !== undefined) {
      await Run.updateOne(
        { _id: currentRunId, runType: 'unknown' },
        { $set: { runType: runTypeFrom(result.test) } },
      );
    }
    const closed = currentRunId;
    currentRunId = null;
    console.log(`[corridas] cerrada ${closed.toString()} (${result.reason})`);
    return closed;
  }

  // Un result sin corrida abierta: el backend arranco a mitad del experimento,
  // o la corrida anterior se barrio por inactividad. El comienzo se reconstruye
  // del tiempo transcurrido que trae el propio mensaje, que es mejor
  // aproximacion que "ahora" -- si no viene, no hay nada mejor.
  const startedAt = result.elapsedSeconds !== undefined
    ? new Date(at.getTime() - result.elapsedSeconds * 1000)
    : at;

  const run = await Run.create({
    deviceId: config.deviceId,
    startedAt,
    endedAt: at,
    state: 'orphan',
    runType: runTypeFrom(result.test),
    result,
    stats: { measureCount: 0, alertCount: 0 },
  });
  console.warn(`[corridas] result sin corrida abierta: se crea huerfana ${run._id.toString()}`);
  return run._id as ObjectId;
}

/**
 * Cada muestra actualiza los contadores y la marca de ultimo dato de su
 * corrida. `lastSampleAt` no es adorno: es la fecha con la que se cierra una
 * corrida huerfana, y lo que el barrido mira para decidir si murio.
 */
export async function noteSample(at: Date, kind: 'measure' | 'alert' | 'other'): Promise<void> {
  if (!currentRunId) return;
  const inc: Record<string, number> = {};
  if (kind === 'measure') inc['stats.measureCount'] = 1;
  if (kind === 'alert') inc['stats.alertCount'] = 1;
  await Run.updateOne(
    { _id: currentRunId },
    { $set: { 'stats.lastSampleAt': at }, ...(Object.keys(inc).length ? { $inc: inc } : {}) },
  );
}

/**
 * Cierra como huerfanas las corridas que dejaron de dar señales. Corre en un
 * intervalo, no por evento, porque el caso que atiende es justamente que NO
 * llega ningun evento.
 */
export async function sweepStaleRuns(now: Date = new Date()): Promise<number> {
  const limite = new Date(now.getTime() - config.run.staleAfterSeconds * 1000);
  const abiertas = await Run.find({ state: 'running' });

  let cerradas = 0;
  for (const run of abiertas) {
    const ultimo = run.stats?.lastSampleAt ?? run.startedAt;
    if (ultimo > limite) continue;
    run.state = 'orphan';
    run.endedAt = ultimo;
    await run.save();
    cerradas++;
    console.warn(
      `[corridas] ${run._id.toString()} sin datos desde ${ultimo.toISOString()}: huerfana`,
    );
    if (currentRunId?.equals(run._id)) currentRunId = null;
  }
  return cerradas;
}

// Solo para los tests: reinicia el estado en memoria sin tocar la base.
export function _resetRunTracker(): void {
  currentRunId = null;
}
