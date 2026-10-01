import mongoose from 'mongoose';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';
import { bucketAutomatico, parseBucket } from '../src/domain/buckets.js';

// La API de historicos. Lo que se prueba aca es lo que decide si una corrida
// vieja se puede leer o no: los filtros de la lista, la agregacion de las
// series y los casos de id invalido / inexistente.

const { Alert, CoilSample, Measure, Run } = await import('../src/models/index.js');
const hayMongo = await connectTestDb('historicos');

// Levantamos la app entera contra un puerto efimero: probar las rutas por HTTP
// verifica tambien el armado de la respuesta y los codigos de estado, que es
// justo donde se rompen las cosas en silencio.
let baseUrl = '';
let server: import('node:http').Server | undefined;

beforeAll(async () => {
  if (!hayMongo) return;
  const express = (await import('express')).default;
  const { runsRouter } = await import('../src/api/runs.js');
  const app = express();
  app.use('/api', runsRouter);
  server = app.listen(0);
  const address = server.address();
  baseUrl = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;

  await Promise.all([Run.deleteMany({}), Measure.deleteMany({}), CoilSample.deleteMany({}), Alert.deleteMany({})]);

  // Dos corridas: una completada en campo X, otra cortada por alerta critica
  // en campo nulo. Con eso se pueden ejercitar todos los filtros.
  const t0 = new Date('2026-09-20T10:00:00Z');
  const completada = await Run.create({
    deviceId: 'biosoft-01', startedAt: t0, endedAt: new Date('2026-09-20T11:00:00Z'),
    state: 'finished',
    targets: { mode: 'campo X', cem: 1.5, freq: 50, dur: 60, tol: 10 },
    result: { reason: 'completed', description: 'Duracion alcanzada' },
    stats: { measureCount: 4, alertCount: 0 },
  });
  const cortada = await Run.create({
    deviceId: 'biosoft-01', startedAt: new Date('2026-09-21T10:00:00Z'),
    endedAt: new Date('2026-09-21T10:33:00Z'), state: 'finished',
    targets: { mode: 'campo nulo', cem: 1.5, freq: 50, dur: 60, tol: 10 },
    result: { reason: 'critical', source: 'TEMP1', type: 'critical', count: 4, limit: 4 },
    stats: { measureCount: 0, alertCount: 2 },
  });
  // Una corrida de banco: la mas nueva, para que si se colara en el listado
  // por defecto apareciera primera y rompiera el orden esperado.
  await Run.create({
    deviceId: 'biosoft-01', startedAt: new Date('2026-09-22T10:00:00Z'),
    endedAt: new Date('2026-09-22T10:05:00Z'), state: 'finished', runType: 'test',
    targets: { mode: 'x', cem: 1, freq: 10, dur: 5, tol: 5, test: true },
    result: { reason: 'completed', test: true },
    stats: { measureCount: 0, alertCount: 0 },
  });

  // Cuatro mediciones DENTRO del primer minuto (0-45 s), con un pico en la
  // tercera. Con una a los 60 s caerian en dos buckets distintos.
  await Measure.insertMany([0, 15, 30, 45].map((s, i) => ({
    ts: new Date(t0.getTime() + s * 1000),
    meta: { runId: completada._id, deviceId: 'biosoft-01' },
    magneticField: 1.5,
    temperature: i === 2 ? 40 : 24,
  })));
  await CoilSample.insertMany([0, 30].map((s) => ({
    ts: new Date(t0.getTime() + s * 1000),
    meta: { runId: completada._id, deviceId: 'biosoft-01' },
    coils: [{ n: 1, current: 0.8, duty: 42 }, { n: 2, current: 0.82, duty: 43 }],
  })));
  await Alert.insertMany([1, 2].map((n) => ({
    ts: new Date(`2026-09-21T10:1${n}:00Z`), runId: cortada._id, deviceId: 'biosoft-01',
    source: 'TEMP1', type: 'critical', count: n, limit: 4,
  })));
}, 30000);

afterAll(async () => {
  server?.close();
  await disconnectTestDb(hayMongo);
});

const get = (ruta: string) => fetch(`${baseUrl}${ruta}`);

describe('buckets', () => {
  it('parsea los tamaños validos', () => {
    expect(parseBucket('30s')).toMatchObject({ unit: 'second', binSize: 30 });
    expect(parseBucket('5m')).toMatchObject({ unit: 'minute', binSize: 5 });
    expect(parseBucket('1h')).toMatchObject({ unit: 'hour', binSize: 1 });
  });

  it('rechaza lo que no lo es, en vez de caer en un default silencioso', () => {
    for (const valor of ['', '5x', 'm', '0m', '99999m', 5, null, undefined]) {
      expect(parseBucket(valor)).toBeNull();
    }
  });

  // Una corrida corta tiene que dar puntos finos y una larga, gruesos: es lo
  // que evita mandar miles de puntos al navegador sin perder detalle cuando
  // hay poco que mostrar.
  it('elige el bucket segun la duracion', () => {
    const desde = new Date('2026-09-20T10:00:00Z');
    const corta = bucketAutomatico(desde, new Date('2026-09-20T10:10:00Z'));
    const larga = bucketAutomatico(desde, new Date('2026-09-21T10:00:00Z'));
    expect(corta.binSize).toBeLessThanOrEqual(30);
    expect(['minute', 'hour']).toContain(larga.unit);
  });
});

describe.skipIf(!hayMongo)('API de historicos', () => {
  it('lista las corridas de la mas nueva a la mas vieja', async () => {
    const data = await (await get('/api/runs')).json();
    expect(data.total).toBe(2);
    expect(data.items[0].mode).toBe('campo nulo');
    expect(data.items[1].mode).toBe('campo X');
  });

  // El modo y el motivo tienen que venir en la LISTA: son lo que se busca al
  // recorrerla, y abrir cada corrida para saberlo la volveria inservible.
  it('la lista trae modo y motivo sin tener que abrir cada corrida', async () => {
    const data = await (await get('/api/runs')).json();
    expect(data.items[0].reason).toBe('critical');
    expect(data.items[0].alertCount).toBe(2);
  });

  it('filtra por motivo, por modo y por fecha', async () => {
    const porMotivo = await (await get('/api/runs?reason=completed')).json();
    expect(porMotivo.total).toBe(1);
    expect(porMotivo.items[0].reason).toBe('completed');

    const porModo = await (await get('/api/runs?mode=campo%20nulo')).json();
    expect(porModo.total).toBe(1);

    const porFecha = await (await get('/api/runs?from=2026-09-21T00:00:00Z')).json();
    expect(porFecha.total).toBe(1);
    expect(porFecha.items[0].mode).toBe('campo nulo');
  });

  // Las pruebas no se mezclan con los experimentos salvo que se pidan.
  it('por defecto deja afuera las corridas de prueba', async () => {
    const data = await (await get('/api/runs')).json();
    expect(data.total).toBe(2);
    expect(data.items.every((r: { runType: string }) => r.runType !== 'test')).toBe(true);
  });

  it('filtra por tipo, y type=all las trae todas', async () => {
    const pruebas = await (await get('/api/runs?type=test')).json();
    expect(pruebas.total).toBe(1);
    expect(pruebas.items[0].runType).toBe('test');

    const todas = await (await get('/api/runs?type=all')).json();
    expect(todas.total).toBe(3);

    const desconocidas = await (await get('/api/runs?type=unknown')).json();
    expect(desconocidas.total).toBe(2);
  });

  // Las corridas guardadas antes de que existiera el campo no lo tienen: el
  // filtro `unknown` tiene que encontrarlas igual.
  it('type=unknown incluye corridas sin el campo runType', async () => {
    const vieja = await Run.create({
      deviceId: 'biosoft-01', startedAt: new Date('2026-09-01T10:00:00Z'), state: 'finished',
    });
    await Run.collection.updateOne({ _id: vieja._id }, { $unset: { runType: '' } });
    try {
      const desconocidas = await (await get('/api/runs?type=unknown')).json();
      expect(desconocidas.items.some((r: { id: string; runType: string }) =>
        r.id === vieja._id.toString() && r.runType === 'unknown')).toBe(true);
    } finally {
      await Run.deleteOne({ _id: vieja._id });
    }
  });

  it('pagina', async () => {
    const pagina1 = await (await get('/api/runs?limit=1&page=1')).json();
    const pagina2 = await (await get('/api/runs?limit=1&page=2')).json();
    expect(pagina1.items).toHaveLength(1);
    expect(pagina2.items).toHaveLength(1);
    expect(pagina1.items[0].id).not.toBe(pagina2.items[0].id);
    expect(pagina1.total).toBe(2);
  });

  it('devuelve el detalle con objetivos y resultado', async () => {
    const lista = await (await get('/api/runs?reason=critical')).json();
    const detalle = await (await get(`/api/runs/${lista.items[0].id}`)).json();
    expect(detalle.targets.mode).toBe('campo nulo');
    expect(detalle.result.source).toBe('TEMP1');
  });

  it('404 si la corrida no existe y 400 si el id es invalido', async () => {
    expect((await get('/api/runs/000000000000000000000000')).status).toBe(404);
    expect((await get('/api/runs/no-es-un-id')).status).toBe(400);
    expect((await get('/api/runs/no-es-un-id/measures')).status).toBe(400);
  });

  // EL PUNTO DE LA AGREGACION. Las cuatro mediciones caen en un bucket de un
  // minuto; si solo se devolviera el promedio, el pico de 40 grados -- que es
  // el evento que alguien va a ir a buscar al grafico -- quedaria escondido
  // detras de un promedio de 28.
  it('agrega conservando el maximo, no solo el promedio', async () => {
    const lista = await (await get('/api/runs?reason=completed')).json();
    const serie = await (await get(`/api/runs/${lista.items[0].id}/measures?bucket=1m`)).json();

    expect(serie.bucket).toBe('1m');
    expect(serie.points).toHaveLength(1);
    expect(serie.points[0].n).toBe(4);
    expect(serie.points[0].tempMax).toBe(40);
    expect(serie.points[0].tempMin).toBe(24);
    expect(serie.points[0].temp).toBeCloseTo(28, 5);
  });

  it('con bucket fino devuelve un punto por medicion', async () => {
    const lista = await (await get('/api/runs?reason=completed')).json();
    const serie = await (await get(`/api/runs/${lista.items[0].id}/measures?bucket=10s`)).json();
    expect(serie.points).toHaveLength(4);
  });

  // Un bucket invalido no puede hacer caer la consulta ni devolver otra cosa
  // en silencio: cae en el automatico y lo dice en la respuesta.
  it('un bucket invalido cae en el automatico', async () => {
    const lista = await (await get('/api/runs?reason=completed')).json();
    const serie = await (await get(`/api/runs/${lista.items[0].id}/measures?bucket=cualquiera`)).json();
    expect(serie.points.length).toBeGreaterThan(0);
    expect(serie.bucket).toBeTruthy();
  });

  it('la serie de bobinas dice cuales reportaron', async () => {
    const lista = await (await get('/api/runs?reason=completed')).json();
    const serie = await (await get(`/api/runs/${lista.items[0].id}/coils?bucket=1m`)).json();
    expect(serie.coils).toEqual([1, 2]);
    expect(serie.points[0]).toHaveProperty('c1');
    expect(serie.points[0]).toHaveProperty('d2');
  });

  it('devuelve las alertas de la corrida en orden', async () => {
    const lista = await (await get('/api/runs?reason=critical')).json();
    const alertas = await (await get(`/api/runs/${lista.items[0].id}/alerts`)).json();
    expect(alertas.items).toHaveLength(2);
    expect(alertas.items[0].count).toBe(1);
    expect(alertas.items[1].count).toBe(2);
  });

  // El CSV va SIN AGREGAR: quien se lleva los datos a una planilla quiere las
  // mediciones como se tomaron, no el promedio que eligio el grafico.
  it('exporta un CSV con las mediciones crudas', async () => {
    const lista = await (await get('/api/runs?reason=completed')).json();
    const respuesta = await get(`/api/runs/${lista.items[0].id}/export.csv`);
    const texto = await respuesta.text();

    expect(respuesta.headers.get('content-type')).toContain('text/csv');
    expect(respuesta.headers.get('content-disposition')).toContain('attachment');
    const lineas = texto.trim().split('\n');
    expect(lineas[0]).toBe('ts,campo_mT,temperatura_C');
    expect(lineas).toHaveLength(5);          // cabecera + 4 mediciones
    expect(lineas[3]).toContain(',40');      // el pico sobrevive sin promediar
  });

  it('una corrida sin muestras devuelve una serie vacia, no un error', async () => {
    const lista = await (await get('/api/runs?reason=critical')).json();
    const serie = await (await get(`/api/runs/${lista.items[0].id}/measures`)).json();
    expect(serie.points).toEqual([]);
  });
});
