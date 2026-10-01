import mongoose from 'mongoose';
import { afterAll, afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';

// La correlacion de corridas es la parte que no se deduce del contrato: la
// placa no manda un id de experimento. Todo lo que se prueba aca falla EN
// SILENCIO si se rompe -- el historico queda con corridas partidas, fusionadas
// o fantasma, y nada lo avisa.
const emitted: Array<{ event: string; data: unknown }> = [];
vi.mock('../src/realtime/socket.js', () => ({
  emitTelemetry: (event: string, data: unknown) => { emitted.push({ event, data }); },
  realtimeStatus: () => ({ clients: 0 }),
  startRealtime: () => {},
}));

const { handleTelemetry } = await import('../src/mqtt/handlers.js');
const { Alert, CoilSample, Measure, Run, StatusSample } = await import('../src/models/index.js');
const {
  _resetRunTracker, getCurrentRunId, initRunTracker, sweepStaleRuns,
} = await import('../src/domain/runtracker.js');
type Group = 'measures' | 'coils' | 'status' | 'targets' | 'alerts' | 'result';

const hayMongo = await connectTestDb('corridas');

afterAll(async () => { await disconnectTestDb(hayMongo); });

beforeEach(async () => {
  emitted.length = 0;
  _resetRunTracker();
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  vi.spyOn(console, 'log').mockImplementation(() => {});
  await Promise.all([
    Run.deleteMany({}), Measure.deleteMany({}), CoilSample.deleteMany({}),
    StatusSample.deleteMany({}), Alert.deleteMany({}),
  ]);
});

afterEach(() => { vi.restoreAllMocks(); });

function message(group: Group, payload: unknown, retained = false, receivedAt = new Date()) {
  return { group, topic: `biosoft/telemetry/${group}`, payload, retained, receivedAt };
}

const TARGETS = { MODE: 'campo X', CEM: 1.5, FREQ: 50, DUR: 60, TOL: 10 };
const RESULT_OK = { REASON: 'completed', DESC: 'Duracion alcanzada', PROGRESS: 100, ELAPSED: '01:00:00' };

describe.skipIf(!hayMongo)('correlacion de corridas', () => {
  it('targets abre una corrida con sus objetivos', async () => {
    await handleTelemetry(message('targets', TARGETS));

    const runs = await Run.find().lean();
    expect(runs).toHaveLength(1);
    expect(runs[0].state).toBe('running');
    expect(runs[0].targets?.mode).toBe('campo X');
    expect(runs[0].targets?.cem).toBe(1.5);
    expect(getCurrentRunId()?.toString()).toBe(runs[0]._id.toString());
  });

  it('result la cierra con su motivo', async () => {
    await handleTelemetry(message('targets', TARGETS));
    await handleTelemetry(message('result', RESULT_OK));

    const runs = await Run.find().lean();
    expect(runs).toHaveLength(1);
    expect(runs[0].state).toBe('finished');
    expect(runs[0].result?.reason).toBe('completed');
    expect(runs[0].endedAt).toBeInstanceOf(Date);
    expect(getCurrentRunId()).toBeNull();
  });

  // Una corrida entera: es el criterio de aceptacion de la fase.
  it('un experimento completo queda como UNA corrida cerrada con sus muestras', async () => {
    await handleTelemetry(message('targets', TARGETS));
    for (let i = 0; i < 3; i++) {
      await handleTelemetry(message('measures', { CEM1: 1.5, TEMP1: 24 + i }));
      await handleTelemetry(message('coils', { c1: 0.8, d1: 42 }));
      await handleTelemetry(message('status', { PROGRESS: i * 33, STATE: 'running' }));
    }
    await handleTelemetry(message('alerts', { SRC: 'CEM1', TYPE: 'streak', COUNT: 1, LIMIT: 3 }));
    await handleTelemetry(message('result', RESULT_OK));

    const runs = await Run.find().lean();
    expect(runs).toHaveLength(1);
    const runId = runs[0]._id.toString();
    expect(runs[0].state).toBe('finished');
    expect(runs[0].stats?.measureCount).toBe(3);
    expect(runs[0].stats?.alertCount).toBe(1);

    // Toda muestra tiene que haber caido en ESA corrida.
    const measures = await Measure.find().lean();
    const coils = await CoilSample.find().lean();
    const statuses = await StatusSample.find().lean();
    const alerts = await Alert.find().lean();
    expect(measures.every((m) => m.meta.runId?.toString() === runId)).toBe(true);
    expect(coils.every((c) => c.meta.runId?.toString() === runId)).toBe(true);
    expect(statuses.every((s) => s.meta.runId?.toString() === runId)).toBe(true);
    expect(alerts.every((a) => a.runId?.toString() === runId)).toBe(true);
  });

  // El caso "se corto la luz a mitad del experimento anterior". La corrida
  // vieja no se puede perder ni fusionar con la nueva.
  it('dos targets seguidos dejan la primera huerfana, no perdida', async () => {
    const t0 = new Date('2026-09-23T10:00:00Z');
    await handleTelemetry(message('targets', TARGETS, false, t0));
    await handleTelemetry(message('measures', { CEM1: 1.5 }, false, new Date('2026-09-23T10:05:00Z')));
    await handleTelemetry(message('targets', { ...TARGETS, MODE: 'campo nulo' }, false,
      new Date('2026-09-23T11:00:00Z')));

    const runs = await Run.find().sort({ startedAt: 1 }).lean();
    expect(runs).toHaveLength(2);
    expect(runs[0].state).toBe('orphan');
    expect(runs[1].state).toBe('running');
    expect(runs[1].targets?.mode).toBe('campo nulo');

    // Se cierra con la fecha de su ultimo dato, no con la de ahora: inventar
    // una hora de cierre seria inventar un experimento mas largo del real.
    expect(runs[0].endedAt?.toISOString()).toBe('2026-09-23T10:05:00.000Z');
  });

  // El motivo del corte es el unico dato que nadie mas reporta: sin el, una
  // corrida cortada por alerta critica se ve igual que una que termino bien.
  it('un result sin corrida abierta crea una huerfana con el resultado', async () => {
    const at = new Date('2026-09-23T12:00:00Z');
    await handleTelemetry(message('result', {
      REASON: 'critical', DESC: 'TEMP1: limite alcanzado (4/4)',
      SRC: 'TEMP1', TYPE: 'critical', COUNT: 4, LIMIT: 4, ELAPSED: '00:33:00',
    }, false, at));

    const runs = await Run.find().lean();
    expect(runs).toHaveLength(1);
    expect(runs[0].state).toBe('orphan');
    expect(runs[0].result?.reason).toBe('critical');
    expect(runs[0].result?.source).toBe('TEMP1');
    // El comienzo se reconstruye del ELAPSED que trae el propio mensaje.
    expect(runs[0].startedAt.toISOString()).toBe('2026-09-23T11:27:00.000Z');
  });

  // EL CASO QUE MAS IMPORTA. Cinco de los seis grupos van con retain, asi que
  // el broker reentrega targets y result en CADA reconexion del backend.
  it('un targets retenido no abre una corrida fantasma', async () => {
    await handleTelemetry(message('targets', TARGETS, true));
    expect(await Run.countDocuments()).toBe(0);
    expect(getCurrentRunId()).toBeNull();
  });

  it('un result retenido no cierra la corrida en curso', async () => {
    await handleTelemetry(message('targets', TARGETS));
    const abierta = getCurrentRunId()?.toString();

    await handleTelemetry(message('result', RESULT_OK, true));

    expect(getCurrentRunId()?.toString()).toBe(abierta);
    expect((await Run.findById(abierta).lean())?.state).toBe('running');
  });

  // Reiniciar el backend a mitad de un experimento no puede partirlo en dos.
  it('al arrancar retoma la corrida que habia quedado abierta', async () => {
    await handleTelemetry(message('targets', TARGETS));
    const abierta = getCurrentRunId()?.toString();

    _resetRunTracker();                 // simula el reinicio del proceso
    expect(getCurrentRunId()).toBeNull();
    await initRunTracker();

    expect(getCurrentRunId()?.toString()).toBe(abierta);

    await handleTelemetry(message('measures', { CEM1: 1.5 }));
    const stored = await Measure.find().lean();
    expect(stored[0].meta.runId?.toString()).toBe(abierta);
  });

  // Un experimento que murio sin result se quedaria abierto para siempre y se
  // tragaria las muestras de todas las corridas siguientes.
  it('el barrido cierra una corrida sin datos recientes', async () => {
    const viejo = new Date(Date.now() - 3600 * 1000);
    await handleTelemetry(message('targets', TARGETS, false, viejo));
    await handleTelemetry(message('measures', { CEM1: 1.5 }, false, viejo));

    expect(await sweepStaleRuns()).toBe(1);

    const runs = await Run.find().lean();
    expect(runs[0].state).toBe('orphan');
    expect(runs[0].endedAt?.getTime()).toBe(viejo.getTime());
    expect(getCurrentRunId()).toBeNull();
  });

  it('el barrido no toca una corrida que sigue reportando', async () => {
    await handleTelemetry(message('targets', TARGETS));
    await handleTelemetry(message('measures', { CEM1: 1.5 }));

    expect(await sweepStaleRuns()).toBe(0);
    expect((await Run.findOne().lean())?.state).toBe('running');
    expect(getCurrentRunId()).not.toBeNull();
  });

  // Las muestras que llegan fuera de todo experimento son datos reales: se
  // guardan, pero explicitamente sin corrida.
  it('las muestras sin corrida abierta quedan con runId null', async () => {
    await handleTelemetry(message('measures', { CEM1: 1.5 }));
    const stored = await Measure.find().lean();
    expect(stored[0].meta.runId).toBeNull();
  });

  // El tipo de corrida separa datos cientificos de datos de banco: si se
  // pierde, una prueba entra al historico como experimento sin aviso.
  it('el tipo de corrida sale de la marca TEST de targets', async () => {
    await handleTelemetry(message('targets', { ...TARGETS, TEST: true }));
    await handleTelemetry(message('result', { ...RESULT_OK, TEST: true }));
    await handleTelemetry(message('targets', { ...TARGETS, TEST: false }));
    await handleTelemetry(message('result', { ...RESULT_OK, TEST: false }));
    await handleTelemetry(message('targets', TARGETS));

    const runs = await Run.find().sort({ _id: 1 }).lean();
    expect(runs.map((r) => r.runType)).toEqual(['test', 'normal', 'unknown']);
  });

  it('result completa un tipo que faltaba, pero nunca pisa el de targets', async () => {
    await handleTelemetry(message('targets', TARGETS));
    await handleTelemetry(message('result', { ...RESULT_OK, TEST: true }));
    await handleTelemetry(message('targets', { ...TARGETS, TEST: false }));
    await handleTelemetry(message('result', { ...RESULT_OK, TEST: true }));

    const runs = await Run.find().sort({ _id: 1 }).lean();
    expect(runs.map((r) => r.runType)).toEqual(['test', 'normal']);
  });

  it('la corrida guarda el id de configuracion y las relajaciones', async () => {
    await handleTelemetry(message('targets', { ...TARGETS, TEST: false, CFG: '1a2b3c4d', RLX: 5 }));
    const runs = await Run.find().lean();
    expect(runs[0].targets?.configId).toBe('1a2b3c4d');
    expect(runs[0].targets?.relaxations).toBe(5);
  });

  it('un result huerfano conserva su marca de prueba', async () => {
    await handleTelemetry(message('result', { ...RESULT_OK, TEST: true }));
    const runs = await Run.find().lean();
    expect(runs[0].state).toBe('orphan');
    expect(runs[0].runType).toBe('test');
  });

  // La base garantiza la invariante, no solo la logica en memoria.
  it('no admite dos corridas abiertas del mismo equipo', async () => {
    await Run.syncIndexes();
    await handleTelemetry(message('targets', TARGETS));

    await expect(Run.create({
      deviceId: 'biosoft-01', startedAt: new Date(), state: 'running',
    })).rejects.toThrow();
  });
});
