// Preparacion de las series antes de graficarlas.

const UNIDAD_MS = { s: 1000, m: 60_000, h: 3_600_000 } as const;

export function bucketEnMs(label: string | undefined): number {
  const match = /^(\d+)(s|m|h)$/.exec(label ?? '');
  if (!match) return 60_000;
  return Number(match[1]) * UNIDAD_MS[match[2] as keyof typeof UNIDAD_MS];
}

/**
 * Inserta un punto nulo donde la serie tiene un hueco.
 *
 * La placa publica con QoS 0 y sin cola: si el backend estuvo caido, o se corto
 * el WiFi, esos minutos no existen y no van a llegar nunca. La API no devuelve
 * buckets vacios, asi que sin esto el grafico une el ultimo punto antes del
 * corte con el primero despues -- una linea recta, prolija y creible, sobre
 * cuarenta minutos en los que no se supo nada del experimento.
 *
 * Con un punto nulo en el medio y `connectNulls={false}`, el hueco se ve como
 * lo que es: un tramo sin datos.
 */
export function insertarHuecos<T extends { ts: string }>(
  puntos: T[],
  bucketMs: number,
): Array<T | { ts: string }> {
  if (puntos.length < 2) return puntos;
  const tolerancia = bucketMs * 1.5;
  const salida: Array<T | { ts: string }> = [puntos[0]];

  for (let i = 1; i < puntos.length; i++) {
    const anterior = new Date(puntos[i - 1].ts).getTime();
    const actual = new Date(puntos[i].ts).getTime();
    if (actual - anterior > tolerancia) {
      salida.push({ ts: new Date(anterior + bucketMs).toISOString() });
    }
    salida.push(puntos[i]);
  }
  return salida;
}

export function aMilisegundos<T extends { ts: string }>(puntos: T[]): Array<T & { t: number }> {
  return puntos.map((punto) => ({ ...punto, t: new Date(punto.ts).getTime() }));
}

/**
 * Lo que entra en una ventana deslizante que empieza en `desde` (ms).
 *
 * Se conserva ademas el ULTIMO punto anterior a la ventana: sin el, la linea
 * arrancaria en la primera muestra de adentro, despegada del borde izquierdo,
 * y pareceria que no hubo datos en ese tramo. El eje lo recorta (ver
 * `allowDataOverflow` en Graficos). La serie tiene que venir ordenada por
 * fecha, que es como la arma GraficosEnVivo.
 */
export function recortarVentana<T extends { ts: string }>(puntos: T[], desde: number | null): T[] {
  if (desde === null || puntos.length === 0) return puntos;
  let primero = puntos.findIndex((punto) => new Date(punto.ts).getTime() >= desde);
  if (primero === -1) primero = puntos.length;   // todo quedo antes de la ventana
  return puntos.slice(Math.max(0, primero - 1));
}
