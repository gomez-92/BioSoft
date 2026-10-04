import type mongoose from 'mongoose';
import { Measure } from '../models/index.js';

// Contexto de una alerta: la ultima medicion de SU fuente antes de que se
// levantara.
//
// La placa publica la alerta sin el valor que la disparo (SRC, TYPE, COUNT y
// LIMIT, nada mas: ver MySystem::_publishAlert), asi que lo mas cercano que
// el monitor puede mostrar es la ultima medicion publicada de esa magnitud.
// Se devuelve con su fecha porque NO es el valor del disparo: el Mega muestrea
// mas seguido de lo que la placa publica, y una racha puede haberse armado
// entre dos publicaciones. Quien lo lea tiene que ver de cuando es.

const CAMPO_POR_FUENTE: Record<string, 'magneticField' | 'temperature'> = {
  CEM1: 'magneticField',
  TEMP1: 'temperature',
};

/** Hasta donde hacia atras se busca: mas viejo que esto ya no describe la alerta. */
const VENTANA_MS = 5 * 60_000;

export interface ValorCercano { value: number; valueAt: Date }

export async function valorAntesDeAlerta(
  runId: mongoose.Types.ObjectId | null, source: string, ts: Date,
): Promise<ValorCercano | null> {
  const campo = CAMPO_POR_FUENTE[source];
  if (!campo || !runId) return null;
  const muestra = await Measure.findOne({
    'meta.runId': runId,
    ts: { $lte: ts, $gte: new Date(ts.getTime() - VENTANA_MS) },
    [campo]: { $ne: null },
  }).sort({ ts: -1 }).lean();
  const valor = muestra?.[campo];
  if (!muestra || typeof valor !== 'number') return null;
  return { value: valor, valueAt: muestra.ts };
}
