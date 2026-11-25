import type { Rangos } from './rangos.js';

// En que zona cae una medicion, con los MISMOS rangos que vigila el Mega
// (lib/rangos.ts). Es lo que la pantalla En curso le dice a quien mira desde
// afuera: "1.528 mT" no significa nada si no se sabe si esta bien.
export type Zona = 'normal' | 'advertencia' | 'critico';

export function zonaDe(valor: number | undefined, rangos: Rangos): Zona | null {
  if (valor === undefined || valor === null || !rangos.normal) return null;
  const [nmin, nmax] = rangos.normal;
  if (valor >= nmin && valor <= nmax) return 'normal';
  if (rangos.critico) {
    const [cmin, cmax] = rangos.critico;
    if (valor < cmin || valor > cmax) return 'critico';
  }
  return 'advertencia';
}

export const TEXTO_ZONA: Record<Zona, string> = {
  normal: 'Dentro del rango normal',
  advertencia: 'Fuera del normal, todavía no crítico',
  critico: 'En zona crítica',
};

/**
 * Tramos de la regla que se dibuja bajo cada medicion: de un poco antes del
 * critico inferior a un poco despues del superior. `piso` recorta abajo (el
 * campo es un modulo: no hay mT negativos). Devuelve null sin rangos.
 */
export function escalaDe(rangos: Rangos, piso?: number) {
  if (!rangos.normal || !rangos.critico) return null;
  const [cmin, cmax] = rangos.critico;
  const margen = (cmax - cmin) * 0.25 || 1;
  let lo = cmin - margen;
  const hi = cmax + margen;
  if (piso !== undefined) lo = Math.max(lo, piso);
  const pos = (v: number) => Math.min(100, Math.max(0, ((v - lo) / (hi - lo)) * 100));
  return { lo, hi, pos, normal: rangos.normal, critico: rangos.critico };
}
