import { describe, expect, it } from 'vitest';
import {
  esAlertaCritica, esCampoNulo, etiquetaTipoCorrida, nombreModo, nombreTipoAlerta, tipoDesdeMarca,
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
