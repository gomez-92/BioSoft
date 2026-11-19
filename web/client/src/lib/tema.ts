import { useEffect, useState } from 'react';

// Tema del monitor: automatico (sigue al sistema), claro u oscuro. La
// preferencia es de este navegador (localStorage), no de la cuenta: el
// celular de noche y la computadora del laboratorio no tienen por que
// coincidir. Se aplica con data-theme en <html> (styles.css); en automatico
// no hay atributo y manda prefers-color-scheme.

export type Preferencia = 'auto' | 'claro' | 'oscuro';
export type Efectivo = 'claro' | 'oscuro';

const CLAVE = 'biosoft.tema';
const EVENTO = 'biosoft-tema';

export function leerPreferencia(): Preferencia {
  try {
    const v = localStorage.getItem(CLAVE);
    if (v === 'claro' || v === 'oscuro' || v === 'auto') return v;
  } catch { /* sin almacenamiento: automatico */ }
  return 'auto';
}

function sistemaOscuro(): boolean {
  return typeof window !== 'undefined' && !!window.matchMedia?.('(prefers-color-scheme: dark)').matches;
}

export function temaEfectivo(pref: Preferencia = leerPreferencia()): Efectivo {
  if (pref === 'auto') return sistemaOscuro() ? 'oscuro' : 'claro';
  return pref;
}

/** Aplica la preferencia al documento y avisa (graficos, generador embebido). */
export function aplicarPreferencia(pref: Preferencia): void {
  const raiz = document.documentElement;
  if (pref === 'auto') delete raiz.dataset.theme;
  else raiz.dataset.theme = pref === 'claro' ? 'light' : 'dark';
  window.dispatchEvent(new Event(EVENTO));
}

export function guardarPreferencia(pref: Preferencia): void {
  try { localStorage.setItem(CLAVE, pref); } catch { /* vale para esta sesion */ }
  aplicarPreferencia(pref);
}

/**
 * Tema efectivo actual, que se actualiza al cambiar la preferencia o, en
 * automatico, el tema del sistema.
 */
export function useTema(): { preferencia: Preferencia; efectivo: Efectivo; elegir: (p: Preferencia) => void } {
  const [preferencia, setPreferencia] = useState<Preferencia>(leerPreferencia);
  const [efectivo, setEfectivo] = useState<Efectivo>(() => temaEfectivo());
  useEffect(() => {
    const actualizar = () => { setPreferencia(leerPreferencia()); setEfectivo(temaEfectivo()); };
    const mq = window.matchMedia?.('(prefers-color-scheme: dark)');
    window.addEventListener(EVENTO, actualizar);
    mq?.addEventListener?.('change', actualizar);
    return () => {
      window.removeEventListener(EVENTO, actualizar);
      mq?.removeEventListener?.('change', actualizar);
    };
  }, []);
  return { preferencia, efectivo, elegir: guardarPreferencia };
}
