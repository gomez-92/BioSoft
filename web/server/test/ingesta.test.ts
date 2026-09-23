import mongoose from 'mongoose';
import { afterAll, afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';

// El socket se reemplaza por un espia: aca interesa QUE se emite y sobre todo
// que no se emita nada que no se haya guardado antes.
const emitted: Array<{ event: string; data: unknown }> = [];
vi.mock('../src/realtime/socket.js', () => ({
  emitTelemetry: (event: string, data: unknown) => { emitted.push({ event, data }); },
  realtimeStatus: () => ({ clients: 0 }),
  startRealtime: () => {},
}));

const { handleTelemetry } = await import('../src/mqtt/handlers.js');
const { Alert, CoilSample, Measure, StatusSample } = await import('../src/models/index.js');
type Group = 'measures' | 'coils' | 'status' | 'targets' | 'alerts' | 'result';

const hayMongo = await connectTestDb('ingesta');

afterAll(async () => { await disconnectTestDb(hayMongo); });

beforeEach(async () => {
  emitted.length = 0;
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  await Promise.all([
    Measure.deleteMany({}), CoilSample.deleteMany({}),
    StatusSample.deleteMany({}), Alert.deleteMany({}),
  ]);
});

afterEach(() => { vi.restoreAllMocks(); });

function message(group: Group, payload: unknown, retained = false) {
  return { group, topic: `biosoft/telemetry/${group}`, payload, retained, receivedAt: new Date() };
}

describe.skipIf(!hayMongo)('ingesta', () => {
  it('guarda una medicion y la emite', async () => {
    await handleTelemetry(message('measures', { CEM1: 1.5, TEMP1: 24.2 }));

    const stored = await Measure.find().lean();
    expect(stored).toHaveLength(1);
    expect(stored[0].magneticField).toBe(1.5);
    expect(stored[0].temperature).toBe(24.2);
    expect(emitted).toEqual([{ event: 'measures', data: expect.objectContaining({ magneticField: 1.5 }) }]);
  });

  it('guarda solo las bobinas que reportaron', async () => {
    await handleTelemetry(message('coils', { c1: 0.85, d1: 43.6, c2: 0.8, d2: 44.7 }));

    const stored = await CoilSample.find().lean();
    expect(stored[0].coils).toHaveLength(2);
    expect(stored[0].coils.map((coil) => coil.n)).toEqual([1, 2]);
  });

  it('guarda el estado con el tiempo convertido a segundos', async () => {
    await handleTelemetry(message('status', {
      ESTADO: 'warning', PROGRESS: 50, ELAPSED_TIME: '00:30:00',
      REMAINING: 1800, STATE: 'running', MEGA: true,
    }));

    const stored = await StatusSample.find().lean();
    expect(stored[0].elapsedSeconds).toBe(1800);
    expect(stored[0].elapsedText).toBe('00:30:00');
  });

  it('guarda una alerta', async () => {
    await handleTelemetry(message('alerts', { SRC: 'TEMP1', TYPE: 'critical', COUNT: 2, LIMIT: 4 }));

    const stored = await Alert.find().lean();
    expect(stored).toHaveLength(1);
    expect(stored[0].source).toBe('TEMP1');
    expect(emitted[0].event).toBe('alert');
  });

  // EL CASO QUE MAS IMPORTA. Cinco de los seis grupos se publican con retain,
  // asi que el broker reentrega el ultimo de cada uno en CADA suscripcion. Sin
  // este filtro, cada reinicio del backend insertaria muestras duplicadas con
  // la fecha de entrega en vez de la de medicion, y en la fase 2 abriria un
  // experimento fantasma con un targets viejo.
  it('no guarda ni emite nada que llegue retenido', async () => {
    await handleTelemetry(message('measures', { CEM1: 1.5, TEMP1: 24 }, true));
    await handleTelemetry(message('status', { PROGRESS: 100 }, true));
    await handleTelemetry(message('targets', { MODE: 'campo X' }, true));
    await handleTelemetry(message('result', { REASON: 'completed' }, true));

    expect(await Measure.countDocuments()).toBe(0);
    expect(await StatusSample.countDocuments()).toBe(0);
    expect(emitted).toHaveLength(0);
  });

  // Un mensaje sin nada aprovechable no tiene que dejar un documento vacio en
  // la serie: al graficarlo seria un punto en cero indistinguible de una
  // medicion real de cero.
  it('descarta un mensaje sin ningun campo util', async () => {
    await handleTelemetry(message('measures', {}));
    await handleTelemetry(message('coils', {}));
    await handleTelemetry(message('alerts', { SRC: 'TEMP1' }));

    expect(await Measure.countDocuments()).toBe(0);
    expect(await CoilSample.countDocuments()).toBe(0);
    expect(await Alert.countDocuments()).toBe(0);
    expect(emitted).toHaveLength(0);
  });

  it('no se cae con un payload que no es un objeto', async () => {
    await handleTelemetry(message('measures', 'no soy json de objeto'));
    await handleTelemetry(message('coils', null));
    expect(await Measure.countDocuments()).toBe(0);
  });

  // Preferimos un tablero que no muestre el dato a uno que muestre algo que no
  // quedo registrado en ningun lado.
  it('no emite en vivo si no pudo guardar', async () => {
    const readyState = vi.spyOn(mongoose.connection, 'readyState', 'get').mockReturnValue(0);

    await handleTelemetry(message('measures', { CEM1: 1.5 }));

    expect(emitted).toHaveLength(0);
    readyState.mockRestore();
    expect(await Measure.countDocuments()).toBe(0);
  });

  // Sin corrida abierta la muestra igual se guarda: es una medicion real. Pero
  // queda explicitamente sin corrida, no con un id inventado. (La asociacion a
  // una corrida se prueba en corridas.test.ts.)
  it('deja las muestras sin corrida cuando no hay ninguna abierta', async () => {
    await handleTelemetry(message('measures', { CEM1: 1.5 }));
    const stored = await Measure.find().lean();
    expect(stored[0].meta.runId).toBeNull();
    expect(stored[0].meta.deviceId).toBe('biosoft-01');
  });
});
