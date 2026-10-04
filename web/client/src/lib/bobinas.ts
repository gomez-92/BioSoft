import type { Coil } from './types.js';

// Que bobinas mostrar.
//
// La placa publica una entrada por cada bobina REGISTRADA en el Mega, y el
// Mega registra las que tiene cableadas en SoftMega2560.ino (BOB1 y BOB2 por
// defecto), no las que la tarjeta SD habilita: una bobina con
// `enabled: false` sigue apareciendo, con duty 0. Asi, con una sola bobina
// habilitada el monitor mostraba dos.
//
// La configuracion con la que corrio la corrida si sabe cuales estan
// habilitadas (seccion `coils`, por nombre), y el monitor la tiene guardada
// por configId. El numero de bobina de la telemetria es su posicion entre las
// registradas (BOB1 = 1 ...), que es el numero del nombre.
//
// Sin configuracion conocida (valores de fabrica o la placa nunca la
// informo), se muestran todas las que reportaron: inventar cuales estan
// habilitadas seria peor que mostrar una de mas.

export function bobinasHabilitadas(config: unknown): Set<number> | null {
  const lista = (config as { coils?: unknown } | null)?.coils;
  if (!Array.isArray(lista) || lista.length === 0) return null;
  const habilitadas = new Set<number>();
  lista.forEach((entrada, index) => {
    const coil = entrada as { name?: unknown; enabled?: unknown };
    if (coil?.enabled === false) return;
    const numeroEnNombre = typeof coil?.name === 'string' ? /(\d+)\s*$/.exec(coil.name)?.[1] : undefined;
    habilitadas.add(numeroEnNombre ? Number(numeroEnNombre) : index + 1);
  });
  return habilitadas;
}

export function filtrarBobinas<T extends { n: number }>(coils: T[], habilitadas: Set<number> | null): T[] {
  return habilitadas ? coils.filter((coil) => habilitadas.has(coil.n)) : coils;
}

/**
 * Una bobina a la que se le aplica duty pero no circula corriente: cable
 * suelto, etapa de potencia apagada o bobina sin montar. No se oculta -- es
 * justamente una falla que hay que ver --, se marca. Solo se puede decir si
 * la bobina tiene sensor de corriente (si no, `current` no viene).
 */
export const DUTY_MINIMO = 5;      // %
export const CORRIENTE_MINIMA = 0.02;   // A

export function sinCorriente(coil: Coil): boolean {
  return coil.duty !== undefined && coil.duty >= DUTY_MINIMO
    && coil.current !== undefined && Math.abs(coil.current) < CORRIENTE_MINIMA;
}
