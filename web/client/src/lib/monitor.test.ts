import { describe, expect, it } from 'vitest';
import { detalleAlerta, graficoDeFuente } from './alertas.js';
import { bobinasHabilitadas, filtrarBobinas, sinCorriente } from './bobinas.js';
import { multiplicadorDe, rangosCampo, rangosTemperatura } from './rangos.js';
import { recortarVentana } from './series.js';

// Lo que estas funciones deciden no da error si se rompe: un grafico con la
// banda en el lugar equivocado, una bobina de mas, una alerta en el grafico
// que no es el suyo.

describe('rangos (espejo de detectorconfigbuilder.hpp)', () => {
  it('campo X: banda alrededor de la intensidad, critico con el multiplicador', () => {
    const r = rangosCampo({ mode: 'x', cem: 2, tol: 10 }, 1.25);
    expect(r.objetivo).toBe(2);
    expect(r.normal).toEqual([1.8, 2.2]);
    expect(r.critico![0]).toBeCloseTo(1.75);
    expect(r.critico![1]).toBeCloseTo(2.25);
  });

  // El error que tenia el grafico: en campo nulo el Mega vigila alrededor de
  // CERO, con la tolerancia medida sobre la intensidad elegida.
  it('campo nulo: banda alrededor de 0, no de la intensidad', () => {
    const r = rangosCampo({ mode: 'null', cem: 1, tol: 5 }, 1.25);
    expect(r.objetivo).toBe(0);
    expect(r.normal![0]).toBeCloseTo(-0.05);
    expect(r.normal![1]).toBeCloseTo(0.05);
    expect(r.critico![1]).toBeCloseTo(0.0625);
  });

  it('sin objetivos no inventa rangos', () => {
    expect(rangosCampo(null)).toEqual({ normal: null, critico: null });
    expect(rangosTemperatura({ tnmin: 30, tnmax: 40 }).critico).toBeNull();
  });

  it('lee el multiplicador de la configuracion, o usa el de fabrica', () => {
    expect(multiplicadorDe({ detector: { sources: [{ name: 'CEM1', range: { criticalMultiplier: 1.5 } }] } })).toBe(1.5);
    expect(multiplicadorDe(null)).toBe(1.25);
  });
});

describe('bobinas', () => {
  const config = {
    coils: [
      { name: 'BOB1', enabled: true }, { name: 'BOB2', enabled: false },
      { name: 'BOB3', enabled: false }, { name: 'BOB4', enabled: false },
    ],
  };

  // El caso del banco: una bobina habilitada, el Mega reportando dos.
  it('oculta las que la configuracion deshabilita', () => {
    const habilitadas = bobinasHabilitadas(config);
    expect([...habilitadas!]).toEqual([1]);
    expect(filtrarBobinas([{ n: 1 }, { n: 2 }], habilitadas)).toEqual([{ n: 1 }]);
  });

  it('sin configuracion conocida muestra todas las que reportaron', () => {
    expect(bobinasHabilitadas(null)).toBeNull();
    expect(filtrarBobinas([{ n: 1 }, { n: 2 }], null)).toHaveLength(2);
  });

  it('marca sin corriente solo si hay sensor y se le aplica duty', () => {
    expect(sinCorriente({ n: 1, duty: 40, current: 0 })).toBe(true);
    expect(sinCorriente({ n: 1, duty: 40, current: 0.8 })).toBe(false);
    expect(sinCorriente({ n: 1, duty: 0, current: 0 })).toBe(false);
    expect(sinCorriente({ n: 1, duty: 40 })).toBe(false);   // sin sensor: no se sabe
  });
});

describe('alertas', () => {
  it('cada fuente va a su grafico', () => {
    expect(graficoDeFuente('CEM1')).toBe('campo');
    expect(graficoDeFuente('TEMP1')).toBe('temperatura');
    expect(graficoDeFuente('SCT013-1')).toBeNull();
  });

  it('el detalle dice que significa, si corto, el valor y los rangos', () => {
    const config = { detector: { sources: [{ name: 'TEMP1', rules: { streak: { threshold: 5 } } }] } };
    const d = detalleAlerta(
      { ts: '2026-10-04T10:05:00Z', source: 'TEMP1', type: 'streak', count: 5, limit: 5, value: 41.2, valueAt: '2026-10-04T10:04:58Z' },
      { targets: { tnmin: 30, tnmax: 40, tcmin: 25, tcmax: 45 }, config, inicio: '2026-10-04T10:00:00Z' },
    );
    expect(d.corto).toBe(true);
    const texto = d.lineas.join('\n');
    expect(texto).toContain('5 lecturas seguidas');
    expect(texto).toContain('CORTO');
    expect(texto).toContain('41.2 °C');
    expect(texto).toContain('30.0 a 40.0 °C');
    expect(texto).toContain('5m 00s del inicio');
  });
});

describe('ventana deslizante', () => {
  const p = (s: number) => ({ ts: new Date(Date.UTC(2026, 9, 4, 10, 0, s)).toISOString() });
  const t = (s: number) => Date.UTC(2026, 9, 4, 10, 0, s);

  it('deja lo de adentro mas el ultimo punto anterior, para que la linea llegue al borde', () => {
    const serie = [p(0), p(10), p(20), p(30), p(40)];
    expect(recortarVentana(serie, t(25))).toEqual([p(20), p(30), p(40)]);
  });

  it('sin ventana (corrida entera) devuelve todo', () => {
    expect(recortarVentana([p(0), p(10)], null)).toHaveLength(2);
  });

  // Un corte largo: todo quedo antes de la ventana. Se conserva el ultimo
  // punto para el borde y el resto de la ventana queda vacio -- el hueco
  // se ve, no se disimula.
  it('si todo quedo antes de la ventana, conserva solo el ultimo punto', () => {
    expect(recortarVentana([p(0), p(10)], t(50))).toEqual([p(10)]);
  });
});
