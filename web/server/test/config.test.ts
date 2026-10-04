import { afterAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';
import { MAX_CHUNK_ESCAPED, chunkText, crc32, sinCredenciales } from '../src/domain/configchunks.js';

// Configuracion remota (tarjeta 24). Todo lo que se prueba aca falla en
// silencio si se rompe: una configuracion mal reensamblada se guardaria como
// "la que corrio" una corrida, y un envio cortado dejaria a la placa con un
// archivo a medias.

const { ConfigRequest, ConfigSnapshot } = await import('../src/models/index.js');
const {
  _resetConfigSync, configurarSync, enviarConfiguracion, handleConfigMessage,
} = await import('../src/domain/configsync.js');

describe('crc32', () => {
  // El vector de prueba estandar del CRC32 IEEE: si da otra cosa, ningun id
  // de la placa va a coincidir.
  it('coincide con el vector de referencia', () => {
    expect(crc32('123456789')).toBe('cbf43926');
    expect(crc32('')).toBe('00000000');
  });
});

describe('chunkText', () => {
  it('reensambla exactamente el texto original', () => {
    const texto = JSON.stringify({ a: 'x'.repeat(2000), b: [1, 2, 3], c: 'con "comillas"' });
    expect(chunkText(texto).join('')).toBe(texto);
  });

  // El tope es en caracteres ESCAPADOS: una comilla ocupa dos dentro del
  // mensaje. Medido en crudo, un bloque lleno de comillas se pasaria del
  // buffer de 512 de la placa.
  it('ningun bloque se pasa del tope una vez escapado', () => {
    const texto = '"'.repeat(1000) + 'abc'.repeat(300);
    for (const bloque of chunkText(texto)) {
      expect(JSON.stringify(bloque).length - 2).toBeLessThanOrEqual(MAX_CHUNK_ESCAPED);
    }
  });

  it('un mensaje completo entra en el buffer de 512 de la placa', () => {
    const texto = '{"a":"' + 'x"'.repeat(4000) + '"}';
    for (const [i, bloque] of chunkText(texto).entries()) {
      const mensaje = JSON.stringify({ r: 'abcdefghijklmnopqrstuvwx', i, n: 99, d: bloque });
      expect(mensaje.length + 'biosoft/config/set'.length + 5).toBeLessThan(512);
    }
  });
});

describe('sinCredenciales', () => {
  it('nunca manda wifi ni broker', () => {
    const limpio = sinCredenciales({ schemaVersion: 1, wifi: [{ ssid: 'x' }], broker: { user: 'u' }, menus: {} });
    expect(limpio).toEqual({ schemaVersion: 1, menus: {} });
  });
});

const hayMongo = await connectTestDb('config');
afterAll(async () => { await disconnectTestDb(hayMongo); });

// Lo que la placa publicaria para un texto dado.
function publicarVigente(texto: string, desorden = false) {
  const id = crc32(texto);
  const bloques = chunkText(texto);
  const mensajes = bloques.map((d, i) => ({
    topic: `biosoft/config/current/${i}`, payload: { id, i, n: bloques.length, d }, retained: true,
  }));
  if (desorden) mensajes.reverse();
  mensajes.push({ topic: 'biosoft/config/current/meta', payload: { id, i: undefined as never, n: bloques.length, d: undefined as never }, retained: true });
  return { id, mensajes };
}

describe.skipIf(!hayMongo)('configuracion vigente de la placa', () => {
  beforeEach(async () => {
    _resetConfigSync();
    vi.spyOn(console, 'log').mockImplementation(() => {});
    vi.spyOn(console, 'warn').mockImplementation(() => {});
    await ConfigSnapshot.deleteMany({});
  });

  // Los bloques llegan retenidos y en cualquier orden al suscribirse.
  it('reensambla los bloques en cualquier orden y guarda por configId', async () => {
    const texto = JSON.stringify({ schemaVersion: 1, menus: { x: 'y'.repeat(1500) } });
    const { id, mensajes } = publicarVigente(texto, true);
    for (const mensaje of mensajes) await handleConfigMessage(mensaje);

    const guardada = await ConfigSnapshot.findOne({ configId: id }).lean();
    expect(guardada?.text).toBe(texto);
    expect((guardada?.content as { schemaVersion: number }).schemaVersion).toBe(1);
  });

  // Bloques de dos configuraciones mezclados (retenidos viejos en el broker)
  // darian un texto que no es ninguna de las dos: el CRC lo detecta.
  it('descarta un texto cuyo CRC no coincide con el id', async () => {
    const texto = JSON.stringify({ schemaVersion: 1, a: 'b' });
    await handleConfigMessage({
      topic: 'biosoft/config/current/0', payload: { id: 'deadbeef', i: 0, n: 1, d: texto }, retained: true,
    });
    expect(await ConfigSnapshot.countDocuments()).toBe(0);
  });

  it('"default" sin bloques queda registrada sin contenido', async () => {
    await handleConfigMessage({
      topic: 'biosoft/config/current/meta', payload: { id: 'default', n: 0 }, retained: true,
    });
    const guardada = await ConfigSnapshot.findOne({ configId: 'default' }).lean();
    expect(guardada).not.toBeNull();
    expect(guardada?.content).toBeNull();
  });

  // "default" junta causas distintas; un archivo descartado por JSON roto
  // no es lo mismo que una placa sin tarjeta, y el motivo tiene que llegar.
  it('"default" guarda el motivo que informa la placa, el ultimo', async () => {
    const meta = (src: string) => handleConfigMessage({
      topic: 'biosoft/config/current/meta', payload: { id: 'default', n: 0, src }, retained: true,
    });
    await meta('nosd');
    expect((await ConfigSnapshot.findOne({ configId: 'default' }).lean())?.loadStatus).toBe('nosd');
    await meta('invalid');
    expect((await ConfigSnapshot.findOne({ configId: 'default' }).lean())?.loadStatus).toBe('invalid');
    await meta('inventado');
    expect((await ConfigSnapshot.findOne({ configId: 'default' }).lean())?.loadStatus).toBeNull();
  });
});

describe.skipIf(!hayMongo)('envio de una configuracion a la placa', () => {
  const CONFIG = { schemaVersion: 1, runType: 'test', menus: { relleno: 'z'.repeat(1200) }, wifi: [{ ssid: 'SECRETA' }] };
  let publicados: Array<{ topic: string; payload: { r: string; i: number; n: number; d: string } }>;

  // Una placa simulada: confirma cada bloque y, en el ultimo, responde con el
  // estado final que se le pida.
  function placa(final: (r: string) => Record<string, unknown> | null, ocupada = false) {
    configurarSync({
      publicar: async (topic, raw) => {
        const payload = JSON.parse(raw);
        publicados.push({ topic, payload });
        const { r, i, n } = payload;
        let respuesta: Record<string, unknown> | null;
        if (ocupada) respuesta = { r, st: 'ocupada', msg: 'hay un experimento en curso' };
        else if (i + 1 < n) respuesta = { r, st: 'parcial', i };
        else respuesta = final(r);
        if (respuesta) {
          setTimeout(() => void handleConfigMessage({
            topic: 'biosoft/config/status', payload: respuesta, retained: false,
          }), 1);
        }
      },
    });
  }

  async function terminado(requestId: string) {
    for (let i = 0; i < 200; i++) {
      const pedido = await ConfigRequest.findOne({ requestId }).lean();
      if (pedido && !['enviando', 'parcial'].includes(pedido.state)) return pedido;
      await new Promise((r) => setTimeout(r, 20));
    }
    throw new Error('el pedido no termino');
  }

  beforeEach(async () => {
    _resetConfigSync(100);
    publicados = [];
    await ConfigRequest.deleteMany({});
  });

  it('manda todos los bloques en orden, sin credenciales, y registra la aceptacion', async () => {
    placa((r) => ({ r, st: 'aceptada', id: '0badc0de', msg: 'se aplica en el proximo reinicio' }));
    const { requestId, bloques } = await enviarConfiguracion(CONFIG, 'tester');
    const pedido = await terminado(requestId);

    expect(pedido.state).toBe('aceptada');
    expect(pedido.newConfigId).toBe('0badc0de');
    expect(publicados.map((m) => m.payload.i)).toEqual([...Array(bloques).keys()]);
    const texto = publicados.map((m) => m.payload.d).join('');
    expect(texto).not.toContain('SECRETA');
    expect(JSON.parse(texto).runType).toBe('test');
  });

  it('una placa ocupada corta el envio en el primer bloque', async () => {
    placa(() => null, true);
    const { requestId } = await enviarConfiguracion(CONFIG, 'tester');
    const pedido = await terminado(requestId);
    expect(pedido.state).toBe('ocupada');
    expect(publicados).toHaveLength(1);
  });

  // Sin confirmacion se reintenta el mismo bloque y despues se da por
  // perdido: nunca se manda el siguiente a ciegas.
  it('sin confirmacion reintenta y termina en no_entregada', async () => {
    configurarSync({ publicar: async (topic, raw) => { publicados.push({ topic, payload: JSON.parse(raw) }); } });
    const { requestId } = await enviarConfiguracion(CONFIG, 'tester');
    const pedido = await terminado(requestId);
    expect(pedido.state).toBe('no_entregada');
    expect(publicados.every((m) => m.payload.i === 0)).toBe(true);
    expect(publicados).toHaveLength(3);
  });

  // Lo que paso en el banco (2026-10-03): una confirmacion llega despues del
  // plazo, el monitor reenvia el bloque y la placa confirma las dos copias.
  // La segunda confirmacion cae mientras se espera el bloque siguiente: no
  // tiene que gastar ese intento ni provocar un reenvio.
  it('una confirmacion atrasada no hace reenviar el bloque siguiente', async () => {
    let esperado = 0;
    configurarSync({
      publicar: async (topic, raw) => {
        const payload = JSON.parse(raw);
        publicados.push({ topic, payload });
        const { r, i, n } = payload;
        // Como la placa: una copia repetida se vuelve a confirmar.
        let respuesta: Record<string, unknown>;
        // 30 ms normal: asi la confirmacion lenta (150) cae mientras se
        // espera el bloque 2 y no despues de que termino todo.
        let demora = 30;
        if (i + 1 === esperado) respuesta = { r, st: 'parcial', i };
        else {
          esperado++;
          respuesta = esperado < n ? { r, st: 'parcial', i } : { r, st: 'aceptada', id: '0badc0de' };
          if (i === 1) demora = 150;   // la confirmacion lenta (plazo: 100 ms)
        }
        setTimeout(() => void handleConfigMessage({
          topic: 'biosoft/config/status', payload: respuesta, retained: false,
        }), demora);
      },
    });
    const { requestId, bloques } = await enviarConfiguracion(CONFIG, 'tester');
    const pedido = await terminado(requestId);

    expect(pedido.state).toBe('aceptada');
    const enviados = publicados.map((m) => m.payload.i);
    expect(enviados.filter((i) => i === 1)).toHaveLength(2);
    for (let i = 0; i < bloques; i++) {
      if (i !== 1) expect(enviados.filter((x) => x === i)).toHaveLength(1);
    }
  });

  // Un reenvio que la placa recibe despues de terminar se contesta
  // "incompleta" (firmware anterior); eso no puede pisar "aceptada".
  it('un estado final tardio no pisa el resultado de la placa', async () => {
    placa((r) => ({ r, st: 'aceptada', id: '0badc0de' }));
    const { requestId } = await enviarConfiguracion(CONFIG, 'tester');
    await terminado(requestId);
    await handleConfigMessage({
      topic: 'biosoft/config/status',
      payload: { r: requestId, st: 'incompleta', msg: 'bloque de un pedido que no esta en curso' },
      retained: false,
    });
    const pedido = await ConfigRequest.findOne({ requestId }).lean();
    expect(pedido?.state).toBe('aceptada');
  });

  // Al reves si: un "no_entregada" lo decidio el monitor, y la respuesta
  // tardia de la placa es mas cierta.
  it('una respuesta tardia corrige un no_entregada', async () => {
    configurarSync({ publicar: async (topic, raw) => { publicados.push({ topic, payload: JSON.parse(raw) }); } });
    const { requestId } = await enviarConfiguracion(CONFIG, 'tester');
    expect((await terminado(requestId)).state).toBe('no_entregada');
    await handleConfigMessage({
      topic: 'biosoft/config/status', payload: { r: requestId, st: 'ocupada' }, retained: false,
    });
    expect((await ConfigRequest.findOne({ requestId }).lean())?.state).toBe('ocupada');
  });

  it('rechaza lo que no es una configuracion', async () => {
    placa(() => null);
    await expect(enviarConfiguracion([1, 2], 'tester')).rejects.toThrow();
    await expect(enviarConfiguracion({ schemaVersion: 2 }, 'tester')).rejects.toThrow('schemaVersion');
    await expect(enviarConfiguracion({ schemaVersion: 1, x: 'y'.repeat(8000) }, 'tester')).rejects.toThrow('tope');
  });
});
