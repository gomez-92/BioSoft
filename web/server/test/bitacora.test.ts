import { afterAll, afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';

const { buscarEventos, configurarBitacora, handleEventoPlaca, parseEvento } = await import('../src/domain/bitacora.js');
const { BoardEvent } = await import('../src/models/index.js');

const RECIBIDO = new Date('2026-11-22T15:00:00.000Z');

describe('parseEvento', () => {
  it('toma la hora de la placa cuando es creible', () => {
    const ts = RECIBIDO.getTime() - 90_000;   // espero en el buffer con el broker caido
    const evento = parseEvento({ n: 3, ts, up: 40, lv: 'W', c: 'broker_caido', m: 'desconectado' }, RECIBIDO);
    expect(evento).toEqual({
      ts: new Date(ts), level: 'W', code: 'broker_caido', message: 'desconectado',
      seq: 3, uptimeSeconds: 40, dropped: 0, runNumber: null,
    });
    expect(parseEvento({ lv: 'I', c: 'corrida_inicio', run: 42 }, RECIBIDO)?.runNumber).toBe(42);
  });

  it('usa la hora de recepcion sin ts, con un ts de 1970 o uno del futuro', () => {
    for (const ts of [undefined, 12_000, RECIBIDO.getTime() + 3_600_000]) {
      expect(parseEvento({ ts, lv: 'I', c: 'arranque' }, RECIBIDO)?.ts).toEqual(RECIBIDO);
    }
  });

  it('rechaza lo que no es un evento', () => {
    expect(parseEvento(null, RECIBIDO)).toBeNull();
    expect(parseEvento([], RECIBIDO)).toBeNull();
    expect(parseEvento({ lv: 'D', c: 'arranque' }, RECIBIDO)).toBeNull();      // D no viaja
    expect(parseEvento({ lv: 'E' }, RECIBIDO)).toBeNull();
    expect(parseEvento({ lv: 'E', c: 'Mega Reinicio' }, RECIBIDO)).toBeNull();
  });

  it('guarda cuantos eventos piso la placa antes de este', () => {
    expect(parseEvento({ lv: 'E', c: 'mega_reinicio', d: 4 }, RECIBIDO)?.dropped).toBe(4);
  });

  it('corta un texto desmedido', () => {
    expect(parseEvento({ lv: 'I', c: 'arranque', m: 'x'.repeat(1000) }, RECIBIDO)?.message).toHaveLength(300);
  });
});

const hayMongo = await connectTestDb('bitacora');
afterAll(async () => { await disconnectTestDb(hayMongo); });

const emitidos: Array<{ evento: string; data: unknown }> = [];
beforeEach(async () => {
  emitidos.length = 0;
  configurarBitacora({ emitir: (evento, data) => emitidos.push({ evento, data }) });
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  if (hayMongo) await BoardEvent.deleteMany({});
});
afterEach(() => { vi.restoreAllMocks(); });

describe.skipIf(!hayMongo)('bitacora', () => {
  it('guarda y recien despues emite', async () => {
    await handleEventoPlaca({ n: 1, lv: 'E', c: 'mega_reinicio', m: 'el Mega se reinicio' }, false, RECIBIDO);
    const guardados = await BoardEvent.find().lean();
    expect(guardados).toHaveLength(1);
    expect(emitidos).toEqual([{
      evento: 'board:event',
      data: expect.objectContaining({ id: String(guardados[0]._id), code: 'mega_reinicio', level: 'E' }),
    }]);
  });

  it('ignora un retenido y un mensaje invalido', async () => {
    await handleEventoPlaca({ lv: 'E', c: 'mega_reinicio' }, true, RECIBIDO);
    await handleEventoPlaca({ lv: 'X', c: 'mega_reinicio' }, false, RECIBIDO);
    expect(await BoardEvent.countDocuments()).toBe(0);
    expect(emitidos).toHaveLength(0);
  });

  it('filtra por nivel y codigo, y pagina hacia atras', async () => {
    const base = RECIBIDO.getTime();
    const eventos = [
      { lv: 'I', c: 'arranque' }, { lv: 'W', c: 'broker_caido' }, { lv: 'I', c: 'broker_ok' },
      { lv: 'E', c: 'mega_reinicio' }, { lv: 'W', c: 'broker_caido' },
    ];
    for (const [i, e] of eventos.entries()) {
      await handleEventoPlaca({ ...e, ts: base - (eventos.length - i) * 1000 }, false, RECIBIDO);
    }

    const graves = await buscarEventos({ levels: ['E', 'W'], limit: 10 });
    expect(graves.items.map((e) => e.code)).toEqual(['broker_caido', 'mega_reinicio', 'broker_caido']);

    const broker = await buscarEventos({ code: 'broker_caido', limit: 10 });
    expect(broker.items).toHaveLength(2);

    const primera = await buscarEventos({ limit: 2 });
    expect(primera.hasMore).toBe(true);
    const segunda = await buscarEventos({ limit: 2, before: new Date(primera.items[1].ts) });
    expect(segunda.items.map((e) => e.code)).toEqual(['broker_ok', 'broker_caido']);
  });
});

describe.skipIf(!hayMongo)('bitacora: acceso', () => {
  it('solo un administrador la lee', async () => {
    const express = (await import('express')).default;
    const { hashPassword } = await import('../src/auth/password.js');
    const { User } = await import('../src/models/user.js');
    const { authRouter } = await import('../src/api/auth.js');
    const { bitacoraRouter } = await import('../src/api/bitacora.js');
    const { requireAuth } = await import('../src/auth/middleware.js');

    await User.deleteMany({});
    await User.create({ username: 'jefa', role: 'admin', passwordHash: await hashPassword('clave-de-jefa') });
    await User.create({ username: 'mira', role: 'viewer', passwordHash: await hashPassword('clave-de-mira') });
    await handleEventoPlaca({ lv: 'E', c: 'mega_reinicio' }, false, RECIBIDO);

    const app = express();
    app.use(express.json());
    app.use('/api', authRouter);
    app.use('/api', requireAuth, bitacoraRouter);
    const server = app.listen(0);
    try {
      const address = server.address();
      const base = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;
      const entrar = async (username: string, password: string) => (await (await fetch(`${base}/api/auth/login`, {
        method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ username, password }),
      })).json()).token as string;
      const pedir = async (token: string) => fetch(`${base}/api/events`, { headers: { authorization: `Bearer ${token}` } });

      expect((await pedir(await entrar('mira', 'clave-de-mira'))).status).toBe(403);
      const respuesta = await pedir(await entrar('jefa', 'clave-de-jefa'));
      expect(respuesta.status).toBe(200);
      expect((await respuesta.json()).items).toHaveLength(1);
    } finally {
      server.close();
    }
  }, 30000);
});

describe.skipIf(!hayMongo)('bitacora: retencion', () => {
  it('Mongo borra los eventos a la semana (indice TTL)', async () => {
    await BoardEvent.init();
    const indices = await BoardEvent.collection.indexes();
    const ttl = indices.find((i) => i.key.receivedAt === 1);
    expect(ttl?.expireAfterSeconds).toBe(7 * 24 * 3600);
  });
});

describe.skipIf(!hayMongo)('bitacora: corridas', () => {
  it('cada evento sabe en que corrida paso, y se filtra por corrida o por fuera/dentro', async () => {
    const { Run } = await import('../src/models/index.js');
    await Run.deleteMany({});
    const t0 = Date.now() - 3_600_000;
    const terminada = await Run.create({
      deviceId: 'biosoft-01', state: 'finished', runType: 'test',
      startedAt: new Date(t0), endedAt: new Date(t0 + 600_000),
    });
    const enCurso = await Run.create({
      deviceId: 'biosoft-01', state: 'running', runType: 'normal', startedAt: new Date(t0 + 1_800_000),
    });
    const en = (ms: number) => t0 + ms;
    // antes | durante la terminada | entre las dos | durante la en curso
    for (const [ms, c] of [[-60_000, 'arranque'], [300_000, 'broker_caido'], [900_000, 'enlace_caido'], [2_400_000, 'mega_reinicio']] as const) {
      await handleEventoPlaca({ lv: 'W', c, ts: en(ms) }, false, new Date());
    }

    const todos = await buscarEventos({ limit: 10 });
    const corridaPorCodigo = Object.fromEntries(todos.items.map((e) => [e.code, e.run?.id ?? null]));
    expect(corridaPorCodigo).toEqual({
      arranque: null, broker_caido: String(terminada._id), enlace_caido: null, mega_reinicio: String(enCurso._id),
    });

    const dentro = await buscarEventos({ scope: 'in', limit: 10 });
    expect(dentro.items.map((e) => e.code)).toEqual(['mega_reinicio', 'broker_caido']);
    const fuera = await buscarEventos({ scope: 'out', limit: 10 });
    expect(fuera.items.map((e) => e.code)).toEqual(['enlace_caido', 'arranque']);
    const deUna = await buscarEventos({ runId: String(terminada._id), limit: 10 });
    expect(deUna.items.map((e) => e.code)).toEqual(['broker_caido']);
    await expect(buscarEventos({ runId: '000000000000000000000000', limit: 10 })).rejects.toThrow();

    // El inicio pasa en la placa ANTES de que el monitor abra la corrida: por
    // fecha quedaria afuera, por numero no.
    await Run.updateOne({ _id: terminada._id }, { $set: { number: 7 } });
    await handleEventoPlaca({ lv: 'I', c: 'corrida_inicio', ts: en(-500), run: 7 }, false, new Date());
    const inicio = (await buscarEventos({ code: 'corrida_inicio', limit: 1 })).items[0];
    expect(inicio.run?.id).toBe(String(terminada._id));
    expect((await buscarEventos({ runId: String(terminada._id), limit: 10 })).items.map((e) => e.code))
      .toEqual(['broker_caido', 'corrida_inicio']);
    expect((await buscarEventos({ scope: 'out', limit: 10 })).items.map((e) => e.code))
      .toEqual(['enlace_caido', 'arranque']);

    // Por numero: la misma corrida, y la respuesta dice cual es.
    const porNumero = await buscarEventos({ runNumber: 7, limit: 10 });
    expect(porNumero.items.map((e) => e.code)).toEqual(['broker_caido', 'corrida_inicio']);
    expect((porNumero as { run?: { id: string } }).run?.id).toBe(String(terminada._id));
    await expect(buscarEventos({ runNumber: 999, limit: 10 })).rejects.toThrow();

    // El de en vivo tambien sale con su corrida.
    expect(emitidos.find((e) => (e.data as { code: string }).code === 'mega_reinicio')?.data)
      .toMatchObject({ run: { id: String(enCurso._id), runType: 'normal' } });
    await Run.deleteMany({});
  });
});
