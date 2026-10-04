import { esCampoNulo } from './format.js';
import type { Targets } from './types.js';

// Rangos normal y critico de cada fuente, tal como los arma el Mega para el
// Detector. Son los que deciden cuando se levanta una alerta, asi que el
// grafico y el detalle de una alerta tienen que mostrar ESTOS, no otros.
//
// Espejo de mega2560/src/detectorconfigbuilder.hpp (applyCemRanges /
// applyCemNullRanges / applyTempRanges) -- mismo riesgo de deriva que los dos
// seriallink.hpp: si el Mega cambia como deriva un rango, aca hay que
// cambiarlo tambien.
//
// En CAMPO NULO el objetivo es 0 y la banda es la tolerancia medida sobre la
// intensidad ELEGIDA (1 mT al 5% => ±0.05 mT alrededor de 0). Antes el grafico
// dibujaba la banda alrededor de la intensidad tambien en campo nulo, que es
// justo lo contrario de lo que el Mega vigila.

/** Default compilado de detector.sources[CEM1].range.criticalMultiplier. */
export const MULTIPLICADOR_CRITICO_DEFAULT = 1.25;

export interface Rangos {
  objetivo?: number;
  normal: [number, number] | null;
  critico: [number, number] | null;
}

export function rangosCampo(targets: Targets | null | undefined, multiplicador = MULTIPLICADOR_CRITICO_DEFAULT): Rangos {
  const cem = targets?.cem;
  const tol = targets?.tol;
  if (cem === undefined || tol === undefined) return { normal: null, critico: null };
  if (esCampoNulo(targets?.mode)) {
    const banda = cem * tol / 100;
    return { objetivo: 0, normal: [-banda, banda], critico: [-multiplicador * banda, multiplicador * banda] };
  }
  return {
    objetivo: cem,
    normal: [cem * (1 - tol / 100), cem * (1 + tol / 100)],
    critico: [cem * (1 - multiplicador * tol / 100), cem * (1 + multiplicador * tol / 100)],
  };
}

export function rangosTemperatura(targets: Targets | null | undefined): Rangos {
  const t = targets;
  return {
    normal: t?.tnmin !== undefined && t?.tnmax !== undefined ? [t.tnmin, t.tnmax] : null,
    critico: t?.tcmin !== undefined && t?.tcmax !== undefined ? [t.tcmin, t.tcmax] : null,
  };
}

/** El multiplicador critico de CEM1 de una configuracion, o el default. */
export function multiplicadorDe(config: unknown): number {
  const fuentes = (config as { detector?: { sources?: Array<Record<string, unknown>> } } | null)
    ?.detector?.sources;
  const cem = fuentes?.find((f) => f.name === 'CEM1') as { range?: { criticalMultiplier?: unknown } } | undefined;
  const valor = cem?.range?.criticalMultiplier;
  return typeof valor === 'number' && valor > 0 ? valor : MULTIPLICADOR_CRITICO_DEFAULT;
}
