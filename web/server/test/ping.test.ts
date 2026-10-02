import { beforeEach, describe, expect, it } from 'vitest';
import {
  _resetPing, configurarPing, handlePong, pingPlaca, placaStatus,
} from '../src/domain/boardping.js';

// Ping a la placa. No necesita Mongo: es estado del enlace, no se persiste.

let publicados: Array<{ topic: string; payload: { r: string } }> = [];
let emitidos: unknown[] = [];

beforeEach(() => {
  _resetPing(50);
  publicados = [];
  emitidos = [];
  configurarPing({
    publicar: async (topic, payload) => { publicados.push({ topic, payload: JSON.parse(payload) }); },
    emitir: (_evento, data) => emitidos.push(data),
  });
});

describe('ping a la placa', () => {
  it('arranca en desconocido: sin ping no se sabe nada', () => {
    expect(placaStatus().estado).toBe('desconocido');
  });

  it('el pong con el mismo id deja la placa conectada con lo que informa', async () => {
    const espera = pingPlaca();
    await Promise.resolve();
    expect(publicados[0].topic).toBe('biosoft/ping');
    handlePong({ r: publicados[0].payload.r, st: 'ready', cfg: 'cbf43926', mega: true, up: 120 });
    const status = await espera;
    expect(status.estado).toBe('conectada');
    expect(status.appState).toBe('ready');
    expect(status.configId).toBe('cbf43926');
    expect(status.mega).toBe(true);
    expect(status.uptimeSeconds).toBe(120);
    expect(status.latenciaMs).not.toBeNull();
    expect(emitidos.length).toBe(1);
  });

  it('sin pong vence y queda sin respuesta', async () => {
    const status = await pingPlaca();
    expect(status.estado).toBe('sin_respuesta');
  });

  it('un pong de un id ajeno no cuenta', async () => {
    const espera = pingPlaca();
    await Promise.resolve();
    handlePong({ r: 'otro', st: 'ready' });
    expect((await espera).estado).toBe('sin_respuesta');
  });

  // Un pong retenido diria que la placa contesto cuando puede estar apagada.
  it('un pong retenido no cuenta', async () => {
    const espera = pingPlaca();
    await Promise.resolve();
    handlePong({ r: publicados[0].payload.r, st: 'ready' }, true);
    expect((await espera).estado).toBe('sin_respuesta');
  });

  it('una placa que deja de contestar pasa de conectada a sin respuesta', async () => {
    const primera = pingPlaca();
    await Promise.resolve();
    handlePong({ r: publicados[0].payload.r, st: 'ready' });
    expect((await primera).estado).toBe('conectada');
    expect((await pingPlaca()).estado).toBe('sin_respuesta');
  });
});
