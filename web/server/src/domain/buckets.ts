// Agregacion de las series por intervalo de tiempo.
//
// A 30 s por muestra, un experimento de 8 h son ~960 puntos por serie, y el
// dia que el intervalo baje a 5 s son 5760. Mandarlos todos al navegador
// funciona hasta que deja de funcionar, asi que se agregan en la base.
//
// Cada bucket lleva PROMEDIO, MINIMO Y MAXIMO, no solo el promedio: promediar
// esconde justamente las excursiones que disparan las alertas. Un pico de
// temperatura de 40 grados durante quince segundos, promediado en un bucket de
// cinco minutos, desaparece -- y es exactamente el evento que corto el
// experimento y que alguien va a ir a buscar al grafico.
//
// Los buckets SIN DATOS no se devuelven. La placa publica con QoS 0 y sin
// cola, asi que un hueco en el historico es real: hubo un rato en que no se
// supo nada. Rellenarlo con ceros o interpolarlo dibujaria una linea prolija
// sobre un periodo en que el experimento pudo estar haciendo cualquier cosa.

export interface Bucket { unit: 'second' | 'minute' | 'hour'; binSize: number; label: string }

const PATRON = /^(\d+)(s|m|h)$/;
const UNIDADES = { s: 'second', m: 'minute', h: 'hour' } as const;

// Tope de puntos por serie. Por encima de esto el grafico no gana detalle: hay
// mas puntos que pixeles de ancho.
const MAX_PUNTOS = 500;

export function parseBucket(texto: unknown): Bucket | null {
  if (typeof texto !== 'string') return null;
  const match = PATRON.exec(texto.trim());
  if (!match) return null;
  const binSize = Number(match[1]);
  if (binSize < 1 || binSize > 1440) return null;
  return { unit: UNIDADES[match[2] as keyof typeof UNIDADES], binSize, label: texto.trim() };
}

/**
 * Bucket automatico a partir de la duracion de la corrida: el que deja la
 * serie por debajo de MAX_PUNTOS eligiendo un tamaño redondo, para que el eje
 * del grafico caiga en marcas legibles y no en "cada 37 segundos".
 */
export function bucketAutomatico(desde: Date, hasta: Date): Bucket {
  const segundos = Math.max(1, (hasta.getTime() - desde.getTime()) / 1000);
  const minimo = segundos / MAX_PUNTOS;

  const escala: Array<[number, Bucket]> = [
    [10, { unit: 'second', binSize: 10, label: '10s' }],
    [30, { unit: 'second', binSize: 30, label: '30s' }],
    [60, { unit: 'minute', binSize: 1, label: '1m' }],
    [300, { unit: 'minute', binSize: 5, label: '5m' }],
    [900, { unit: 'minute', binSize: 15, label: '15m' }],
    [3600, { unit: 'hour', binSize: 1, label: '1h' }],
  ];
  for (const [tamaño, bucket] of escala) if (tamaño >= minimo) return bucket;
  return { unit: 'hour', binSize: 6, label: '6h' };
}

// $dateTrunc agrupa por intervalo sin tener que calcular los bordes a mano.
export function truncarFecha(bucket: Bucket) {
  return { $dateTrunc: { date: '$ts', unit: bucket.unit, binSize: bucket.binSize } };
}
