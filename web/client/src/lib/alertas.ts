import { duracion, horaCorta, nombreTipoAlerta, numero } from './format.js';
import { multiplicadorDe, rangosCampo, rangosTemperatura, type Rangos } from './rangos.js';
import type { AlertItem, Targets } from './types.js';

// El detalle de una alerta, armado de lo que se sabe de ella.
//
// La placa publica cuatro datos por alerta (fuente, tipo, cuenta y limite).
// Leidos solos -- "TEMP1 racha 2/5" -- no dicen que paso. Este modulo los
// cruza con lo demas que el monitor tiene: los objetivos de la corrida (de
// donde salen los rangos que vigila el Detector), la configuracion con la que
// corrio (los umbrales de cada regla) y la ultima medicion de esa fuente
// antes de la alerta. Una sola funcion para el detalle en la lista y para el
// globo del grafico, asi los dos dicen lo mismo.

const FUENTES: Record<string, { nombre: string; unidad: string; decimales: number }> = {
  CEM1: { nombre: 'Campo magnetico', unidad: 'mT', decimales: 3 },
  TEMP1: { nombre: 'Temperatura', unidad: '°C', decimales: 1 },
};

export function nombreFuente(source: string): string {
  const fuente = FUENTES[source];
  return fuente ? `${fuente.nombre} (${source})` : source;
}

/** A que grafico pertenece una alerta: cada una va al de SU magnitud. */
export function graficoDeFuente(source: string): 'campo' | 'temperatura' | null {
  if (source === 'CEM1') return 'campo';
  if (source === 'TEMP1') return 'temperatura';
  return null;
}

function umbral(config: unknown, source: string, tipo: string): number | undefined {
  const fuentes = (config as { detector?: { sources?: Array<Record<string, unknown>> } } | null)
    ?.detector?.sources;
  const fuente = fuentes?.find((f) => f.name === source) as
    { rules?: Record<string, { threshold?: unknown }> } | undefined;
  const valor = fuente?.rules?.[tipo]?.threshold;
  return typeof valor === 'number' ? valor : undefined;
}

function queSignifica(tipo: string, n: number | undefined): string {
  const cuantas = n !== undefined ? `${n} ` : '';
  switch (tipo) {
    case 'critical':
      return `${cuantas}lectura${n === 1 ? '' : 's'} seguida${n === 1 ? '' : 's'} fuera del rango CRITICO.`;
    case 'streak':
      return `${cuantas}lecturas seguidas fuera del rango normal (sin llegar a criticas).`;
    case 'frequency':
      return `${cuantas}lecturas fuera del rango normal dentro de la ventana de muestras, no necesariamente seguidas.`;
    case 'silence':
      return 'La fuente dejo de entregar lecturas validas (sensor desconectado o fallando).';
    default:
      return '';
  }
}

function rango(r: [number, number] | null, decimales: number, unidad: string): string {
  return r ? `${numero(r[0], decimales)} a ${numero(r[1], decimales)} ${unidad}` : '--';
}

export interface DetalleAlerta {
  titulo: string;
  lineas: string[];
  /** Esta alerta alcanzo el limite de su regla: es la que corto el experimento. */
  corto: boolean;
}

export function detalleAlerta(alerta: AlertItem, contexto: {
  targets?: Targets | null; config?: unknown; inicio?: string | number;
}): DetalleAlerta {
  const fuente = FUENTES[alerta.source];
  const corto = alerta.count >= alerta.limit;
  const lineas: string[] = [];

  const significado = queSignifica(alerta.type, umbral(contexto.config, alerta.source, alerta.type));
  if (significado) lineas.push(significado);

  lineas.push(corto
    ? `Alerta ${alerta.count} de ${alerta.limit}: alcanzo el limite y CORTO el experimento.`
    : `Alerta ${alerta.count} de ${alerta.limit} de este tipo: al llegar a ${alerta.limit} se corta el experimento.`);

  if (fuente && alerta.value !== undefined) {
    const cuando = alerta.valueAt
      ? Math.max(0, Math.round((new Date(alerta.ts).getTime() - new Date(alerta.valueAt).getTime()) / 1000))
      : undefined;
    lineas.push(`Ultima medicion publicada antes de la alerta: ${numero(alerta.value, fuente.decimales)} ${fuente.unidad}` +
      (cuando !== undefined ? ` (${cuando < 2 ? 'en el momento' : `${duracion(cuando)} antes`})` : '') +
      '. El Mega muestrea mas seguido de lo que la placa publica: el valor exacto del disparo puede ser otro.');
  }

  let rangos: Rangos | null = null;
  if (alerta.source === 'CEM1') rangos = rangosCampo(contexto.targets, multiplicadorDe(contexto.config));
  if (alerta.source === 'TEMP1') rangos = rangosTemperatura(contexto.targets);
  if (fuente && rangos && (rangos.normal || rangos.critico)) {
    lineas.push(`Rango normal: ${rango(rangos.normal, fuente.decimales, fuente.unidad)} · ` +
      `critico: ${rango(rangos.critico, fuente.decimales, fuente.unidad)}.`);
  }

  const inicio = contexto.inicio !== undefined ? new Date(contexto.inicio).getTime() : undefined;
  const momento = horaCorta(alerta.ts);
  lineas.push(inicio !== undefined
    ? `${momento}, a los ${duracion((new Date(alerta.ts).getTime() - inicio) / 1000)} del inicio.`
    : momento);

  return {
    titulo: `${nombreFuente(alerta.source)} — ${nombreTipoAlerta(alerta.type)}`,
    lineas,
    corto,
  };
}
