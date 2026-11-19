import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';
import { hashPassword } from '../src/auth/password.js';
import { ESCENARIOS_FABRICA } from '../src/domain/escenariosfabrica.js';
import { EscenarioInvalido, leerEscenario } from '../src/domain/escenarios.js';

// Biblioteca de escenarios de prueba. Lo que se pinea: que los de fabrica son
// los comprobados (tools/escenarios.json), que no se guarda un escenario que
// el Mega rechazaria, que solo un admin escribe, y que la siembra y la
// restauracion no pisan lo que hizo el usuario.

const original = fileURLToPath(new URL('../../../tools/escenarios.json', import.meta.url));

describe('escenarios de fabrica', () => {
  // Dentro del contenedor no esta tools/: ahi no hay contra que comparar.
  it.skipIf(!existsSync(original))('son identicos a tools/escenarios.json (si no: npm run sync:generador)', () => {
    expect(JSON.stringify(ESCENARIOS_FABRICA)).toBe(JSON.stringify(JSON.parse(readFileSync(original, 'utf8'))));
  });

  it('todos pasan la validacion del servidor', () => {
    for (const p of ESCENARIOS_FABRICA.profiles) {
      const sources = { [p.source]: { enabled: p.enabled, scenario: p.scenario } };
      expect(() => leerEscenario({ name: p.name, description: p.description, sources })).not.toThrow();
    }
  });
});

describe('validacion de un escenario (los limites del Mega)', () => {
  const base = () => ({
    name: 'Prueba', description: '',
    sources: { TEMP1: { enabled: true, scenario: { points: [[0, 0], [60, 2.5]], noise: 0.1, oscAmp: 0, oscPeriod: 0, dropAt: 0 } } },
  });
  const rechaza = (mutar: (b: ReturnType<typeof base>) => void, motivo: RegExp) => {
    const b = base(); mutar(b);
    expect(() => leerEscenario(b)).toThrow(EscenarioInvalido);
    expect(() => leerEscenario(b)).toThrow(motivo);
  };

  it('acepta uno valido y redondea el nivel a centesimas', () => {
    const b = base(); b.sources.TEMP1.scenario.points = [[0, 1.234]];
    expect(leerEscenario(b).sources.TEMP1?.scenario.points).toEqual([[0, 1.23]]);
  });
  it('rechaza mas de 8 puntos', () => rechaza((b) => {
    b.sources.TEMP1.scenario.points = Array.from({ length: 9 }, (_, k) => [k, 0]);
  }, /hasta 8/));
  it('rechaza segundos desordenados o con decimales', () => {
    rechaza((b) => { b.sources.TEMP1.scenario.points = [[60, 0], [30, 1]]; }, /en orden/);
    rechaza((b) => { b.sources.TEMP1.scenario.points = [[0.5, 0]]; }, /entero/);
  });
  it('rechaza un nivel fuera de -10..10', () => rechaza((b) => {
    b.sources.TEMP1.scenario.points = [[0, 11]];
  }, /fuera de -10..10/));
  it('rechaza ruido negativo y un duty de CEM1 fuera de 0..1', () => {
    rechaza((b) => { b.sources.TEMP1.scenario.noise = -1; }, /noise/);
    rechaza((b) => {
      b.sources = { CEM1: { enabled: true,
        scenario: { points: [], noise: 0, oscAmp: 0, oscPeriod: 0, dropAt: 0, setpointDuty: 1.5 } } } as never;
    }, /setpointDuty/);
  });
  it('rechaza un escenario con las dos fuentes juntas (son por fuente)', () => rechaza((b) => {
    (b.sources as Record<string, unknown>).CEM1 = { enabled: true,
      scenario: { points: [], noise: 0, oscAmp: 0, oscPeriod: 0, dropAt: 0, setpointDuty: 0.5 } };
  }, /una sola fuente/));
  it('rechaza un escenario sin fuentes, sin nombre o con fuentes desconocidas', () => {
    rechaza((b) => { b.sources = {} as never; }, /ninguna fuente/);
    rechaza((b) => { b.name = '  '; }, /nombre/);
    rechaza((b) => { (b.sources as Record<string, unknown>).SCT1 = {}; }, /CEM1 y TEMP1/);
  });
});

/* ----------------------------- con Mongo ----------------------------- */

const { User } = await import('../src/models/user.js');
const { Scenario, Setting } = await import('../src/models/index.js');
const hayMongo = await connectTestDb('escenarios');

let baseUrl = '';
let server: import('node:http').Server | undefined;

beforeAll(async () => {
  if (!hayMongo) return;
  const express = (await import('express')).default;
  const { authRouter } = await import('../src/api/auth.js');
  const { escenariosRouter } = await import('../src/api/escenarios.js');
  const { requireAuth } = await import('../src/auth/middleware.js');
  const { cargarSesiones } = await import('../src/auth/sesiones.js');

  await Promise.all([User.deleteMany({}), Scenario.deleteMany({}), Setting.deleteMany({})]);
  await Promise.all([Scenario.init(), Setting.init()]);
  await User.create([
    { username: 'admin', passwordHash: await hashPassword('clave-admin'), role: 'admin' },
    { username: 'visor', passwordHash: await hashPassword('clave-visor'), role: 'viewer' },
  ]);
  await cargarSesiones();

  const app = express();
  app.use(express.json());
  app.use('/api', authRouter);
  app.use('/api', requireAuth, escenariosRouter);
  server = app.listen(0);
  const address = server.address();
  baseUrl = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;
}, 30000);

afterAll(async () => {
  server?.close();
  await disconnectTestDb(hayMongo);
});

async function pedir(metodo: string, ruta: string, token?: string, cuerpo?: unknown) {
  const respuesta = await fetch(`${baseUrl}${ruta}`, {
    method: metodo,
    headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: cuerpo === undefined ? undefined : JSON.stringify(cuerpo),
  });
  return { status: respuesta.status, data: await respuesta.json().catch(() => ({})) };
}
async function entrar(username: string, password: string): Promise<string> {
  const { status, data } = await pedir('POST', '/api/auth/login', undefined, { username, password });
  expect(status).toBe(200);
  return data.token;
}

const propio = {
  name: 'Temperatura en escalera', description: 'sube de a medio nivel',
  sources: { TEMP1: { enabled: true, scenario: { points: [[0, 0], [60, 0.5], [120, 1], [180, 1.5]], noise: 0.1, oscAmp: 0, oscPeriod: 0, dropAt: 0 } } },
  reference: { intensity: 1, frequency: 10, tolerance: 5, tempNormal: { min: 30, max: 40 },
               tempCritical: { min: 25, max: 45 }, durationS: 300, mode: 'x' },
};

describe.skipIf(!hayMongo)('biblioteca de escenarios', () => {
  it('la primera siembra trae los de fabrica, en el orden del archivo y con su promesa', async () => {
    const { sembrarEscenarios } = await import('../src/domain/escenarios.js');
    await sembrarEscenarios();
    const { status, data } = await pedir('GET', '/api/scenarios', await entrar('visor', 'clave-visor'));
    expect(status).toBe(200);
    expect(data.items.map((e: { factoryKey: string }) => e.factoryKey))
      .toEqual(ESCENARIOS_FABRICA.profiles.map((p) => p.key));
    expect(data.items[1].expect).toEqual(ESCENARIOS_FABRICA.profiles[1].expect);
    // Cada uno es de una fuente, y la fila lo dice.
    expect(data.items.map((e: { source: string }) => e.source))
      .toEqual(ESCENARIOS_FABRICA.profiles.map((p) => p.source));
  }, 20000);

  it('un viewer lee pero no escribe', async () => {
    const visor = await entrar('visor', 'clave-visor');
    expect((await pedir('POST', '/api/scenarios', visor, propio)).status).toBe(403);
    expect((await pedir('POST', '/api/scenarios/restore', visor)).status).toBe(403);
  });

  it('un admin crea, edita y borra; el nombre es unico', async () => {
    const admin = await entrar('admin', 'clave-admin');
    const creado = await pedir('POST', '/api/scenarios', admin, propio);
    expect(creado.status).toBe(201);
    expect(creado.data.item.reference.durationS).toBe(300);
    expect((await pedir('POST', '/api/scenarios', admin, propio)).status).toBe(409);
    expect((await pedir('POST', '/api/scenarios', admin, { ...propio, name: 'Otro', sources: {} })).status).toBe(400);

    const id = creado.data.item.id;
    const editado = await pedir('PUT', `/api/scenarios/${id}`, admin, { ...propio, description: 'otra' });
    expect(editado.status).toBe(200);
    expect(editado.data.item.description).toBe('otra');

    expect((await pedir('DELETE', `/api/scenarios/${id}`, admin)).status).toBe(200);
    expect((await pedir('DELETE', `/api/scenarios/${id}`, admin)).status).toBe(404);
  });

  it('editar uno de fabrica lo marca modificado y le quita la promesa', async () => {
    const admin = await entrar('admin', 'clave-admin');
    const lista = (await pedir('GET', '/api/scenarios', admin)).data.items;
    const alta = lista.find((e: { factoryKey: string }) => e.factoryKey === 'tempAlta');
    const sources = JSON.parse(JSON.stringify(alta.sources));
    sources.TEMP1.scenario.points[2][1] = 3;
    const r = await pedir('PUT', `/api/scenarios/${alta.id}`, admin, { name: alta.name, description: alta.description, sources });
    expect(r.data.item.modificado).toBe(true);
    expect(r.data.item.expect).toBeNull();
  });

  it('restaurar vuelve los de fabrica y recrea los borrados, sin tocar los propios', async () => {
    const admin = await entrar('admin', 'clave-admin');
    let lista = (await pedir('GET', '/api/scenarios', admin)).data.items;
    const normal = lista.find((e: { factoryKey: string }) => e.factoryKey === 'tempNormal');
    await pedir('DELETE', `/api/scenarios/${normal.id}`, admin);
    // Uno propio que ocupa el nombre de uno de fabrica borrado.
    await pedir('POST', '/api/scenarios', admin, { ...propio, name: 'Temperatura normal' });

    const r = await pedir('POST', '/api/scenarios/restore', admin);
    expect(r.status).toBe(200);
    lista = r.data.items;
    const deFabrica = lista.filter((e: { factoryKey: string | null }) => e.factoryKey);
    expect(deFabrica).toHaveLength(ESCENARIOS_FABRICA.profiles.length);
    expect(deFabrica.every((e: { modificado: boolean }) => !e.modificado)).toBe(true);
    expect(lista.find((e: { factoryKey: string }) => e.factoryKey === 'tempNormal').name).toBe('Temperatura normal (fabrica)');
    expect(lista.find((e: { name: string; factoryKey: string | null }) => e.name === 'Temperatura normal' && !e.factoryKey))
      .toBeTruthy();
  });

  it('un escenario de fabrica nuevo (actualizacion) se siembra solo, sin revivir los borrados', async () => {
    const { sembrarEscenarios } = await import('../src/domain/escenarios.js');
    const [ultimo, ...resto] = [...ESCENARIOS_FABRICA.profiles].reverse();
    // Como si la base se hubiera sembrado antes de que existiera `ultimo`,
    // y despues el admin hubiera borrado otro.
    await Scenario.deleteMany({ factoryKey: { $in: [ultimo.key, resto[0].key] } });
    await Setting.updateOne({ key: 'escenariosSembrados' },
      { $set: { value: { keys: ESCENARIOS_FABRICA.profiles.filter((p) => p.key !== ultimo.key).map((p) => p.key) } } });
    await sembrarEscenarios();
    expect(await Scenario.exists({ factoryKey: ultimo.key })).toBeTruthy();
    expect(await Scenario.exists({ factoryKey: resto[0].key })).toBeFalsy();
  });

  it('los escenarios viejos con las dos fuentes juntas se pasan a uno por fuente', async () => {
    const { migrarEscenariosCombinados } = await import('../src/domain/escenarios.js');
    const senal = { points: [], noise: 0.1, oscAmp: 0, oscPeriod: 0, dropAt: 0 };
    await Scenario.deleteMany({});
    await Scenario.collection.insertMany([
      // De fabrica, de antes: combinado, con una clave que sigue existiendo.
      { name: 'Temperatura alta', factoryKey: 'tempAlta', description: '',
        sources: { CEM1: { enabled: false, scenario: { ...senal, setpointDuty: 0.5 } },
                   TEMP1: { enabled: true, scenario: { ...senal, points: [[0, 0], [60, 0], [60, 2.5]] } } } },
      // De fabrica con una clave que ya no existe.
      { name: 'Todo normal', factoryKey: 'normal', description: '',
        sources: { CEM1: { enabled: true, scenario: { ...senal, setpointDuty: 0.5 } }, TEMP1: { enabled: true, scenario: senal } } },
      // Propio, combinado.
      { name: 'Mio', factoryKey: null, description: 'los dos',
        sources: { CEM1: { enabled: true, scenario: { ...senal, setpointDuty: 0.3 } }, TEMP1: { enabled: false, scenario: senal } } },
    ]);
    await migrarEscenariosCombinados();
    const filas = await Scenario.find().lean();
    expect(filas.every((e) => Object.keys(e.sources as object).length === 1)).toBe(true);
    expect(Object.keys(filas.find((e) => e.factoryKey === 'tempAlta')!.sources as object)).toEqual(['TEMP1']);
    expect(filas.some((e) => e.factoryKey === 'normal')).toBe(false);
    expect(filas.filter((e) => !e.factoryKey).map((e) => e.name).sort()).toEqual(['Mio (campo)', 'Mio (temperatura)']);
    // Correrla de nuevo no cambia nada.
    await migrarEscenariosCombinados();
    expect(await Scenario.countDocuments()).toBe(filas.length);
  });

  it('la siembra no se repite aunque se borren todos', async () => {
    const { sembrarEscenarios } = await import('../src/domain/escenarios.js');
    await Scenario.deleteMany({});
    await sembrarEscenarios();
    expect(await Scenario.countDocuments()).toBe(0);
  });
});
