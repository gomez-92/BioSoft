import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';
import { hashPassword } from '../src/auth/password.js';

// Borrado de corridas y series crudas. Lo que importa: que el borrado se
// lleve TODO lo de la corrida (si deja muestras sueltas, quedan apuntando a
// una corrida inexistente y engordan la base sin que nada las muestre), que
// la corrida en curso no se pueda borrar, y que solo lo haga un admin.

const { Alert, CoilSample, Measure, Run, StatusSample } = await import('../src/models/index.js');
const { User } = await import('../src/models/user.js');
const hayMongo = await connectTestDb('borrado');

let baseUrl = '';
let server: import('node:http').Server | undefined;
let admin = '';
let viewer = '';

beforeAll(async () => {
  if (!hayMongo) return;
  const express = (await import('express')).default;
  const { authRouter } = await import('../src/api/auth.js');
  const { runsRouter } = await import('../src/api/runs.js');
  const { requireAuth } = await import('../src/auth/middleware.js');

  await Promise.all([
    User.deleteMany({}), Run.deleteMany({}), Measure.deleteMany({}), CoilSample.deleteMany({}),
    StatusSample.deleteMany({}), Alert.deleteMany({}),
  ]);
  await User.create({ username: 'jefa', role: 'admin', passwordHash: await hashPassword('clave-de-jefa') });
  await User.create({ username: 'mira', role: 'viewer', passwordHash: await hashPassword('clave-de-mira') });

  const app = express();
  app.use(express.json());
  app.use('/api', authRouter);
  app.use('/api', requireAuth, runsRouter);
  server = app.listen(0);
  const address = server.address();
  baseUrl = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;

  const entrar = async (username: string, password: string) => {
    const r = await fetch(`${baseUrl}/api/auth/login`, {
      method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ username, password }),
    });
    return (await r.json()).token as string;
  };
  admin = await entrar('jefa', 'clave-de-jefa');
  viewer = await entrar('mira', 'clave-de-mira');
}, 30000);

afterAll(async () => {
  server?.close();
  await disconnectTestDb(hayMongo);
});

async function corridaConDatos(state: 'finished' | 'running' = 'finished') {
  const t0 = new Date(Date.UTC(2026, 9, 1, 10, Math.floor(Math.random() * 59)));
  const run = await Run.create({
    deviceId: 'biosoft-01', startedAt: t0, state, runType: 'test',
    ...(state === 'finished' ? { endedAt: new Date(t0.getTime() + 60_000) } : {}),
    stats: { measureCount: 2, alertCount: 1 },
  });
  const meta = { runId: run._id, deviceId: 'biosoft-01' };
  await Measure.insertMany([0, 2].map((s) => ({
    ts: new Date(t0.getTime() + s * 1000), meta, magneticField: 1 + s, temperature: 30 + s,
  })));
  await CoilSample.create({ ts: t0, meta, coils: [{ n: 1, current: 0.5, duty: 40 }] });
  await StatusSample.create({ ts: t0, meta, health: 'normal', progress: 10 });
  await Alert.create({
    ts: new Date(t0.getTime() + 3000), runId: run._id, source: 'TEMP1', type: 'streak', count: 1, limit: 3,
  });
  return run._id.toString();
}

const pedir = (metodo: string, ruta: string, token: string, cuerpo?: unknown) =>
  fetch(`${baseUrl}${ruta}`, {
    method: metodo,
    headers: { 'content-type': 'application/json', authorization: `Bearer ${token}` },
    body: cuerpo === undefined ? undefined : JSON.stringify(cuerpo),
  });

describe.skipIf(!hayMongo)('borrado de corridas', () => {
  it('un viewer no puede borrar', async () => {
    const id = await corridaConDatos();
    expect((await pedir('DELETE', `/api/runs/${id}`, viewer)).status).toBe(403);
    expect(await Run.exists({ _id: id })).toBeTruthy();
  });

  it('un admin borra la corrida con todas sus series y alertas', async () => {
    const id = await corridaConDatos();
    expect((await pedir('DELETE', `/api/runs/${id}`, admin)).status).toBe(200);
    expect(await Run.exists({ _id: id })).toBeNull();
    expect(await Measure.countDocuments({ 'meta.runId': id })).toBe(0);
    expect(await CoilSample.countDocuments({ 'meta.runId': id })).toBe(0);
    expect(await StatusSample.countDocuments({ 'meta.runId': id })).toBe(0);
    expect(await Alert.countDocuments({ runId: id })).toBe(0);
  });

  it('no borra una corrida en curso', async () => {
    const id = await corridaConDatos('running');
    expect((await pedir('DELETE', `/api/runs/${id}`, admin)).status).toBe(409);
    expect(await Measure.countDocuments({ 'meta.runId': id })).toBe(2);
    await Run.updateOne({ _id: id }, { $set: { state: 'orphan' } });
  });

  it('borra varias de una vez', async () => {
    const a = await corridaConDatos();
    const b = await corridaConDatos();
    const r = await pedir('POST', '/api/runs/delete', admin, { ids: [a, b] });
    expect(r.status).toBe(200);
    expect((await r.json()).deleted).toBe(2);
    expect(await Run.countDocuments({ _id: { $in: [a, b] } })).toBe(0);
    expect((await pedir('POST', '/api/runs/delete', admin, { ids: ['no-es-un-id'] })).status).toBe(400);
  });
});

describe.skipIf(!hayMongo)('series para la pantalla En curso', () => {
  it('bucket=raw devuelve las muestras sin agregar', async () => {
    const id = await corridaConDatos();
    const r = await pedir('GET', `/api/runs/${id}/measures?bucket=raw`, viewer);
    const data = await r.json();
    expect(data.bucket).toBe('raw');
    expect(data.points.map((p: { temp: number }) => p.temp)).toEqual([30, 32]);
    const bobinas = await (await pedir('GET', `/api/runs/${id}/coils?bucket=raw`, viewer)).json();
    expect(bobinas.coils).toEqual([1]);
    expect(bobinas.points[0].d1).toBe(40);
  });

  it('cada alerta trae la ultima medicion de SU fuente antes de levantarse', async () => {
    const id = await corridaConDatos();
    const { items } = await (await pedir('GET', `/api/runs/${id}/alerts`, viewer)).json();
    expect(items[0].source).toBe('TEMP1');
    expect(items[0].value).toBe(32);      // temperatura, no campo
    expect(items[0].valueAt).toBeDefined();
  });

  it('la lista cuenta las corridas por tipo aunque filtre uno', async () => {
    const data = await (await pedir('GET', '/api/runs?type=normal', viewer)).json();
    expect(data.total).toBe(0);
    expect(data.byType.test).toBeGreaterThan(0);
  });
});

describe.skipIf(!hayMongo)('duracion pedida en milisegundos', () => {
  // El simulador guardaba DUR en minutos y la placa en ms. Se normaliza todo
  // a ms; una duracion real en ms nunca baja de 1000, asi que no se toca.
  it('pasa a ms las corridas viejas en minutos y deja las de la placa', async () => {
    const { normalizarDuraciones } = await import('../src/domain/runtracker.js');
    const vieja = await Run.create({
      deviceId: 'biosoft-01', startedAt: new Date(), state: 'finished', targets: { dur: 60 },
    });
    const real = await Run.create({
      deviceId: 'biosoft-01', startedAt: new Date(), state: 'finished', targets: { dur: 300_000 },
    });
    await normalizarDuraciones();
    await normalizarDuraciones();   // idempotente
    expect((await Run.findById(vieja._id).lean())?.targets?.dur).toBe(3_600_000);
    expect((await Run.findById(real._id).lean())?.targets?.dur).toBe(300_000);
  });
});
