import { afterAll, afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';

// El snapshot es lo primero que ve un navegador que se conecta: lo que diga
// el panel de alertas sale de aca. Una alerta que no es de la corrida en
// curso sugiere un problema actual que no existe.
vi.mock('../src/realtime/socket.js', () => ({
  emitTelemetry: () => {},
  realtimeStatus: () => ({ clients: 0 }),
  startRealtime: () => {},
}));

const { handleTelemetry } = await import('../src/mqtt/handlers.js');
const { Alert, CoilSample, Measure, Run, StatusSample } = await import('../src/models/index.js');
const { _resetRunTracker } = await import('../src/domain/runtracker.js');
const { buildSnapshot } = await import('../src/domain/snapshot.js');

const hayMongo = await connectTestDb('snapshot');
afterAll(async () => { await disconnectTestDb(hayMongo); });

beforeEach(async () => {
  _resetRunTracker();
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  vi.spyOn(console, 'log').mockImplementation(() => {});
  await Promise.all([
    Run.deleteMany({}), Measure.deleteMany({}), CoilSample.deleteMany({}),
    StatusSample.deleteMany({}), Alert.deleteMany({}),
  ]);
});

afterEach(() => { vi.restoreAllMocks(); });

type Group = 'targets' | 'alerts' | 'result';
function message(group: Group, payload: unknown) {
  return { group, topic: `biosoft/telemetry/${group}`, payload, retained: false, receivedAt: new Date() };
}

const TARGETS = { MODE: 'x', CEM: 1, FREQ: 10, DUR: 300, TOL: 5, TEST: true };
const ALERTA = { SRC: 'CEM1', TYPE: 'critical', COUNT: 1, LIMIT: 2 };

describe.skipIf(!hayMongo)('snapshot: panel de alertas', () => {
  it('muestra las alertas de la corrida en curso', async () => {
    await handleTelemetry(message('targets', TARGETS));
    await handleTelemetry(message('alerts', ALERTA));

    const snap = await buildSnapshot();
    expect(snap.run).not.toBeNull();
    expect(snap.alerts).toHaveLength(1);
  });

  it('al terminar la corrida el panel queda vacio', async () => {
    await handleTelemetry(message('targets', TARGETS));
    await handleTelemetry(message('alerts', ALERTA));
    await handleTelemetry(message('result', { REASON: 'critical', SRC: 'CEM1', TYPE: 'critical', COUNT: 2, LIMIT: 2 }));

    const snap = await buildSnapshot();
    expect(snap.run).toBeNull();
    expect(snap.alerts).toHaveLength(0);
  });

  // Lo que paso en banco (2026-10-03): alertas que llegaron sin corrida
  // abierta quedan con runId null, y sin experimento en curso el panel las
  // mostraba indefinidamente.
  it('sin corrida en curso no muestra las alertas huerfanas', async () => {
    await handleTelemetry(message('alerts', ALERTA));
    expect(await Alert.countDocuments({ runId: null })).toBe(1);   // se guarda igual

    const snap = await buildSnapshot();
    expect(snap.run).toBeNull();
    expect(snap.alerts).toHaveLength(0);
  });
});
