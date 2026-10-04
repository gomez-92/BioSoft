import { describe, expect, it } from 'vitest';
import {
  avanceFinal, duracionPedida, esAlertaCritica, esCampoNulo, etiquetaTipoCorrida, motivoConfig, nombreModo, nombreTipoAlerta,
  relajacionesConAviso, relajacionesDe, tipoDesdeMarca,
} from './format.js';

// El modo de exposicion es lo unico que distingue el grupo tratado del grupo
// control. Equivocarse no se nota mirando la pantalla: un campo nulo se ve, en
// todo lo demas, igual que un experimento normal.
//
// Esto se rompio de verdad. El codigo preguntaba `mode.includes('nulo')`, y la
// placa publica "null" -- que NO contiene "nulo". Una corrida de control se
// habria mostrado como tratada, y el simulador no lo revelaba porque mandaba
// la etiqueta ("Campo nulo") en vez del valor.
describe('modo de campo', () => {
  // LO QUE MANDA LA PLACA: el campo `value` de optionsFieldMode.
  it('reconoce los valores reales que publica la placa', () => {
    expect(esCampoNulo('null')).toBe(true);
    expect(esCampoNulo('x')).toBe(false);
  });

  it('los muestra con un nombre legible', () => {
    expect(nombreModo('null')).toBe('CAMPO NULO');
    expect(nombreModo('x')).toBe('CAMPO X');
  });

  // Las corridas viejas del simulador guardaron la etiqueta; se siguen leyendo.
  it('acepta tambien las etiquetas de corridas viejas', () => {
    expect(esCampoNulo('Campo nulo')).toBe(true);
    expect(esCampoNulo('campo nulo')).toBe(true);
    expect(esCampoNulo('Campo X')).toBe(false);
  });

  it('no se cae sin modo', () => {
    expect(esCampoNulo(undefined)).toBe(false);
    expect(esCampoNulo(null)).toBe(false);
    expect(esCampoNulo('')).toBe(false);
    expect(nombreModo(undefined)).toBe('--');
  });

  // Ante algo inesperado conviene mostrar "CAMPO X" (el caso por defecto del
  // firmware, ver CoilExcitation) antes que inventar que es control.
  it('trata lo desconocido como campo X, no como nulo', () => {
    expect(esCampoNulo('otra cosa')).toBe(false);
  });
});

// La marca de prueba es lo que separa datos de banco de un experimento. Mismo
// criterio que el backend: sin marca no es "normal".
describe('tipo de corrida', () => {
  it('deriva el tipo de la marca TEST', () => {
    expect(tipoDesdeMarca(true)).toBe('test');
    expect(tipoDesdeMarca(false)).toBe('normal');
    expect(tipoDesdeMarca(undefined)).toBe('unknown');
  });

  it('solo marca lo que no es un experimento normal', () => {
    expect(etiquetaTipoCorrida('test')).toBe('PRUEBA');
    expect(etiquetaTipoCorrida('unknown')).toBe('SIN MARCA');
    expect(etiquetaTipoCorrida(undefined)).toBe('SIN MARCA');
    expect(etiquetaTipoCorrida('normal')).toBeNull();
  });
});

// Un sensor caido corta igual que un limite critico (tarjeta 25): no puede
// mostrarse con el color de una advertencia ni con la clave cruda.
describe('alerta de sensor sin lecturas', () => {
  it('tiene nombre propio', () => {
    expect(nombreTipoAlerta('silence')).toBe('sin lecturas');
  });

  it('cuenta como critica', () => {
    expect(esAlertaCritica('silence')).toBe(true);
    expect(esAlertaCritica('critical')).toBe(true);
    expect(esAlertaCritica('streak')).toBe(false);
    expect(esAlertaCritica(undefined)).toBe(false);
  });
});

// La mascara RLX de la placa (tarjeta 23). El orden de bits tiene que ser el
// de ConfigLoader::Relaxations: si se corre uno, cada corrida se traduce mal.
describe('relajaciones', () => {
  it('traduce cada bit en el orden de la placa', () => {
    expect(relajacionesDe(1)).toEqual(['Detector apagado']);
    expect(relajacionesDe(1 << 3)).toEqual(['Sensores simulados o escenario']);
    expect(relajacionesDe(1 << 6)).toEqual(['Sin exigir la placa de control']);
    expect(relajacionesDe(0)).toEqual([]);
    expect(relajacionesDe(undefined)).toEqual([]);
  });

  // CEM1 sin vigilar es el default de fabrica: avisarlo marcaria a todo
  // experimento.
  it('CEM1 sin vigilar se registra pero no avisa', () => {
    expect(relajacionesDe(4)).toEqual(['CEM1 sin vigilar']);
    expect(relajacionesConAviso(4)).toEqual([]);
    expect(relajacionesConAviso(4 | 1)).toEqual(['Detector apagado']);
  });
});

describe('motivoConfig', () => {
  // Un archivo descartado esta en la tarjeta y se ignora: tiene que avisarse.
  it('avisa los archivos ignorados y la falta de tarjeta', () => {
    for (const st of ['nosd', 'unreadable', 'invalid', 'schema']) {
      expect(motivoConfig('default', st).aviso).toBe(true);
    }
  });
  it('sin archivo es un arranque normal, sin aviso', () => {
    expect(motivoConfig('default', 'nofile').aviso).toBe(false);
  });
  it('una placa vieja que no informa el motivo no inventa uno', () => {
    expect(motivoConfig('default', null).texto).toContain('no informa');
  });
  it('una configuracion cargada no avisa nada', () => {
    expect(motivoConfig('cbf43926', 'ok').aviso).toBe(false);
  });
});

describe('duracion pedida y avance final', () => {
  // La placa publica DUR en milisegundos: 300000 son 5 minutos, no 300000.
  it('formatea la duracion pedida desde milisegundos', () => {
    expect(duracionPedida(300_000)).toBe('5m 00s');
    expect(duracionPedida(3_600_000)).toBe('1h 00m');
    expect(duracionPedida(undefined)).toBe('--');
  });

  it('una corrida completed cerro al 100% y con la duracion entera', () => {
    expect(avanceFinal({ reason: 'completed', progressPercent: 99, elapsedSeconds: 298 }, 300_000))
      .toEqual({ porcentaje: 100, segundos: 300 });
  });

  it('una cortada antes conserva lo que informo la placa', () => {
    expect(avanceFinal({ reason: 'critical', progressPercent: 55, elapsedSeconds: 165 }, 300_000))
      .toEqual({ porcentaje: 55, segundos: 165 });
  });
});
