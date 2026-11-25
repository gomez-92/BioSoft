import { describe, expect, it } from 'vitest';
import { resumirCorridas, segundosDeExposicion, type CorridaParaStats } from '../src/domain/estadisticas.js';

// La agregacion de la pagina Inicio, sin Mongo. Lo que no se puede perder:
// las pruebas no entran en los numeros de experimentos, y una corrida sin
// marca SI (puede haber tenido animales).

const AHORA = Date.parse('2026-11-24T12:00:00Z');
const DIA = 24 * 3600 * 1000;
let n = 0;

function corrida(parcial: Partial<CorridaParaStats>): CorridaParaStats {
  n += 1;
  return {
    _id: `r${n}`,
    startedAt: new Date(AHORA - DIA),
    state: 'finished',
    runType: 'normal',
    targets: { mode: 'x' },
    result: { reason: 'completed', elapsedSeconds: 3600 },
    ...parcial,
  };
}

describe('estadisticas de Inicio', () => {
  it('separa pruebas de experimentos y cuenta las sin marca como experimento', () => {
    const r = resumirCorridas([
      corrida({}),
      corrida({ runType: 'unknown' }),
      corrida({ runType: 'test' }),
    ], AHORA);
    expect(r.experimentos.total).toBe(2);
    expect(r.experimentos.sinMarca).toBe(1);
    expect(r.pruebas.total).toBe(1);
    expect(r.idsExperimentos).toHaveLength(2);
  });

  it('las pruebas no suman horas ni desenlaces', () => {
    const r = resumirCorridas([corrida({ runType: 'test', result: { reason: 'critical', elapsedSeconds: 7200 } })], AHORA);
    expect(r.experimentos.horasExposicion).toBe(0);
    expect(r.experimentos.desenlaces.critical).toBe(0);
  });

  it('cuenta tratado y control con el mismo criterio que el cliente', () => {
    const r = resumirCorridas([
      corrida({ targets: { mode: 'x' } }),
      corrida({ targets: { mode: 'null' } }),
      corrida({ targets: { mode: 'campo nulo' } }),
    ], AHORA);
    expect(r.experimentos.tratado).toBe(1);
    expect(r.experimentos.control).toBe(2);
  });

  it('clasifica los desenlaces; una huerfana es sin cierre y la en curso no cuenta', () => {
    const r = resumirCorridas([
      corrida({}),
      corrida({ result: { reason: 'stopped', elapsedSeconds: 10 } }),
      corrida({ result: { reason: 'critical', elapsedSeconds: 10 } }),
      corrida({ result: { reason: 'lost' } }),
      corrida({ result: { reason: 'unknown' } }),
      corrida({ state: 'orphan', result: null, endedAt: new Date(AHORA - DIA + 600_000) }),
      corrida({ state: 'running', result: null }),
    ], AHORA);
    expect(r.experimentos.desenlaces).toEqual({ completed: 1, stopped: 1, critical: 1, lost: 1, otro: 1, sinCierre: 1 });
    expect(r.experimentos.total).toBe(7);
  });

  it('horas: el elapsed de la placa, o del inicio al ultimo dato si es huerfana', () => {
    expect(segundosDeExposicion(corrida({}))).toBe(3600);
    expect(segundosDeExposicion(corrida({ state: 'orphan', result: null, endedAt: new Date(AHORA - DIA + 1_800_000) }))).toBe(1800);
    expect(segundosDeExposicion(corrida({ state: 'running', result: null }))).toBe(0);
    const r = resumirCorridas([corrida({}), corrida({ result: { reason: 'stopped', elapsedSeconds: 1800 } })], AHORA);
    expect(r.experimentos.horasExposicion).toBe(1.5);
  });

  it('recientes = ultimos 30 dias, y el ultimo inicio es el mas nuevo', () => {
    const r = resumirCorridas([
      corrida({ startedAt: new Date(AHORA - 40 * DIA) }),
      corrida({ startedAt: new Date(AHORA - 2 * DIA) }),
      corrida({ runType: 'test', startedAt: new Date(AHORA - 40 * DIA) }),
    ], AHORA);
    expect(r.experimentos.recientes).toBe(1);
    expect(r.pruebas.recientes).toBe(0);
    expect(r.experimentos.ultimoInicio).toBe(new Date(AHORA - 2 * DIA).toISOString());
  });

  it('sin corridas, todo en cero', () => {
    const r = resumirCorridas([], AHORA);
    expect(r.experimentos.total).toBe(0);
    expect(r.experimentos.ultimoInicio).toBeNull();
  });
});
