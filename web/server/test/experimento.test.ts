import { describe, expect, it } from 'vitest';
import { incoherenciaDeExperimento } from '../src/domain/configsync.js';

// Un experimento corre si o si con sensores reales y el Detector encendido.
// El generador no deja armar otra cosa; esta es la misma regla en el
// servidor, para lo que no venga del generador.

const fuentes = (cem: string, temp: string) => ({
  detector: { enabled: true, sources: [{ name: 'CEM1', sensor: cem }, { name: 'TEMP1', sensor: temp }] },
});

describe('coherencia de un experimento', () => {
  it('experimento con sensores reales y Detector: valido', () => {
    expect(incoherenciaDeExperimento({ runType: 'normal', ...fuentes('mlx90393', 'ds18b20') })).toBeNull();
  });
  it('un runType mal escrito tambien es experimento (como en la placa)', () => {
    expect(incoherenciaDeExperimento({ runType: 'Normal', ...fuentes('sim', 'ds18b20') })).toMatch(/CEM1/);
  });
  it('sin runType cuenta como experimento (asi lo lee el firmware)', () => {
    expect(incoherenciaDeExperimento(fuentes('scenario', 'ds18b20'))).toMatch(/CEM1/);
  });
  it('rechaza sim, scenario y none en un experimento', () => {
    expect(incoherenciaDeExperimento({ runType: 'normal', ...fuentes('mlx90393', 'sim') })).toMatch(/TEMP1/);
    expect(incoherenciaDeExperimento({ runType: 'normal', ...fuentes('mlx90393', 'none') })).toMatch(/TEMP1/);
    expect(incoherenciaDeExperimento({ runType: 'normal', ...fuentes('scenario', 'ds18b20') })).toMatch(/CEM1/);
  });
  it('rechaza el Detector apagado en un experimento', () => {
    expect(incoherenciaDeExperimento({ runType: 'normal', detector: { enabled: false } })).toMatch(/Detector/);
  });
  it('una prueba puede simular y apagar el Detector', () => {
    expect(incoherenciaDeExperimento({ runType: 'test', ...fuentes('scenario', 'none') })).toBeNull();
    expect(incoherenciaDeExperimento({ runType: 'test', detector: { enabled: false } })).toBeNull();
  });
  it('sin seccion detector rigen los defaults (reales, encendido): valido', () => {
    expect(incoherenciaDeExperimento({ runType: 'normal' })).toBeNull();
  });
});
