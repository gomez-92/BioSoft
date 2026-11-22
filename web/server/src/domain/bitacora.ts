import mongoose from 'mongoose';
import { config } from '../config.js';
import { BITACORA_RETENCION_S, BoardEvent, Run } from '../models/index.js';

// Bitacora de la placa (esp32/src/eventlog.hpp). La placa publica en
// biosoft/log unos pocos eventos curados -- reinicios, enlaces caidos,
// corridas perdidas, configuracion ignorada --, de a uno y con QoS 0:
//
//   {"n":7,"ts":1763820000123,"up":312,"lv":"W","c":"broker_caido","m":"...","d":0}
//
// Mismo criterio que la telemetria: se guarda y RECIEN DESPUES se emite, y
// un retenido no es un evento (la placa no retiene; un retenido de otro
// cliente se le entregaria a cada monitor nuevo como si acabara de pasar).

export type NivelEvento = 'E' | 'W' | 'I';

export interface EventoPlaca {
  ts: Date;
  level: NivelEvento;
  code: string;
  message: string;
  seq: number | null;
  uptimeSeconds: number | null;
  dropped: number;
  /** Numero de corrida en curso cuando paso (run de la placa), o null. */
  runNumber: number | null;
}

const NIVELES: readonly string[] = ['E', 'W', 'I'];
const CODIGO = /^[a-z][a-z0-9_]{0,31}$/;
const MAX_MENSAJE = 300;
// Antes de esto la placa no tenia hora valida (MySystem::_timeIsValid).
const HORA_MINIMA = Date.UTC(2025, 0, 1);
// Tolerancia a un reloj de la placa adelantado respecto del servidor.
const ADELANTO_MAXIMO_MS = 5 * 60_000;

function entero(valor: unknown): number | null {
  return typeof valor === 'number' && Number.isInteger(valor) && valor >= 0 ? valor : null;
}

/**
 * Valida el mensaje. null si no es un evento: sin nivel conocido, sin codigo
 * o con un codigo que no tiene forma de codigo (se guarda lo que la placa
 * manda, pero no basura que despues no se pueda filtrar).
 *
 * La fecha es la de la placa si es creible (posterior a 2025 y no del
 * futuro): los eventos que esperaron en su buffer con el broker caido
 * llegan tarde, y la hora de recepcion los ubicaria mal. Si no, la de
 * recepcion.
 */
export function parseEvento(payload: unknown, receivedAt: Date): EventoPlaca | null {
  if (typeof payload !== 'object' || payload === null || Array.isArray(payload)) return null;
  const data = payload as Record<string, unknown>;
  if (typeof data.lv !== 'string' || !NIVELES.includes(data.lv)) return null;
  if (typeof data.c !== 'string' || !CODIGO.test(data.c)) return null;

  const ts = typeof data.ts === 'number' && Number.isFinite(data.ts)
    && data.ts >= HORA_MINIMA && data.ts <= receivedAt.getTime() + ADELANTO_MAXIMO_MS
    ? new Date(data.ts)
    : receivedAt;

  return {
    ts,
    level: data.lv as NivelEvento,
    code: data.c,
    message: typeof data.m === 'string' ? data.m.slice(0, MAX_MENSAJE) : '',
    seq: entero(data.n),
    uptimeSeconds: entero(data.up),
    dropped: entero(data.d) ?? 0,
    runNumber: entero(data.run) || null,
  };
}

type Emitir = (evento: string, data: unknown) => void | Promise<void>;
let emitir: Emitir = () => {};

export function configurarBitacora(opciones: { emitir: Emitir }): void {
  emitir = opciones.emitir;
}

// A que corrida pertenece un evento. La placa no manda id de corrida (el
// monitor las deduce de targets/result), y un evento puede llegar tarde
// (esperando en el buffer de la placa con el broker caido): por eso NO se
// guarda la corrida "abierta al recibirlo", que podria ser la siguiente.
// Se calcula al consultar, por fecha: el evento es de la corrida cuya
// ventana [startedAt, endedAt] lo contiene (una en curso, hasta ahora). Es
// el mismo criterio del detalle de corrida, sin su minuto de margen.
export interface CorridaDelEvento {
  id: string; number: number | null; startedAt: string; runType: string; state: string;
}

interface Ventana { id: string; desde: number; hasta: number; corrida: CorridaDelEvento }

function ventanaDe(run: {
  _id: unknown; number?: number | null; startedAt: Date; endedAt?: Date | null; runType?: string | null; state: string;
}): Ventana {
  return {
    id: String(run._id),
    desde: run.startedAt.getTime(),
    hasta: run.endedAt ? run.endedAt.getTime() : Date.now(),
    corrida: {
      id: String(run._id),
      number: run.number ?? null,
      startedAt: run.startedAt.toISOString(),
      runType: run.runType ?? 'unknown',
      state: run.state,
    },
  };
}

/**
 * Las corridas que pueden contener eventos: los eventos viven una semana
 * (indice TTL), asi que alcanza con las que terminaron despues de eso o
 * siguen sin terminar. Son pocas; se traen enteras.
 */
async function ventanasRecientes(): Promise<Ventana[]> {
  const desde = new Date(Date.now() - (BITACORA_RETENCION_S + 24 * 3600) * 1000);
  const runs = await Run.find({ $or: [{ endedAt: { $gte: desde } }, { endedAt: null }] })
    .select('_id number startedAt endedAt runType state').lean();
  return runs.map(ventanaDe);
}

// Un numero de corrida se puede repetir (borrar la flash de la ESP32 reinicia
// el contador): el evento se asigna a la corrida con ese numero MAS CERCANA
// en el tiempo, y solo si esta a menos de esto.
const MARGEN_NUMERO_MS = 24 * 3600 * 1000;

function corridaDe(ts: Date, ventanas: Ventana[], runNumber?: number | null): CorridaDelEvento | null {
  const t = ts.getTime();
  // Primero por numero: el inicio de una corrida pasa en la placa ANTES de
  // que el monitor la abra (la abre al recibir targets), y por fecha quedaria
  // afuera.
  if (runNumber) {
    let cercana: Ventana | null = null;
    let distancia = Infinity;
    for (const v of ventanas) {
      if (v.corrida.number !== runNumber) continue;
      const d = t < v.desde ? v.desde - t : t > v.hasta ? t - v.hasta : 0;
      if (d < distancia) { distancia = d; cercana = v; }
    }
    if (cercana && distancia <= MARGEN_NUMERO_MS) return cercana.corrida;
  }
  // Si dos se solaparan (no deberia: hay una sola abierta por equipo), la
  // mas reciente.
  let mejor: Ventana | null = null;
  for (const v of ventanas) {
    if (t >= v.desde && t <= v.hasta && (!mejor || v.desde > mejor.desde)) mejor = v;
  }
  return mejor ? mejor.corrida : null;
}

/** Un evento listo para el front (el mismo objeto por REST y por socket). */
export function eventoParaCliente(doc: {
  _id: unknown; ts: Date; level: string; code: string; message?: string | null;
  seq?: number | null; uptimeSeconds?: number | null; dropped?: number | null; runNumber?: number | null;
}, ventanas: Ventana[] = []) {
  return {
    id: String(doc._id),
    ts: doc.ts.toISOString(),
    level: doc.level,
    code: doc.code,
    message: doc.message ?? '',
    seq: doc.seq ?? null,
    uptimeSeconds: doc.uptimeSeconds ?? null,
    dropped: doc.dropped ?? 0,
    run: corridaDe(doc.ts, ventanas, doc.runNumber),
  };
}

export async function handleEventoPlaca(payload: unknown, retained: boolean, receivedAt = new Date()): Promise<void> {
  if (retained) return;
  const evento = parseEvento(payload, receivedAt);
  if (!evento) {
    console.warn(`[bitacora] evento descartado: ${JSON.stringify(payload).slice(0, 200)}`);
    return;
  }
  if (mongoose.connection.readyState !== 1) {
    console.warn(`[bitacora] mongo desconectado, se descarta ${evento.code}`);
    return;
  }
  const doc = await BoardEvent.create({ ...evento, receivedAt, deviceId: config.deviceId });
  // El de en vivo sale con su corrida, igual que los de la API.
  await emitir('board:event', eventoParaCliente(doc, await ventanasRecientes()));
}

/** all = todos; in = solo durante alguna corrida; out = solo fuera de corridas. */
export type AlcanceEventos = 'all' | 'in' | 'out';

export interface ConsultaEventos {
  levels?: NivelEvento[];
  code?: string;
  from?: Date;
  to?: Date;
  /** Paginado hacia atras: eventos ANTERIORES a esta fecha. */
  before?: Date;
  scope?: AlcanceEventos;
  /** Solo los de esa corrida (su ventana). Pisa a `scope`. */
  runId?: string;
  /**
   * Solo los de la corrida con ese numero (#42). Si el numero se repite
   * (flash de la ESP32 borrada), la mas reciente. Pisa a `scope`.
   */
  runNumber?: number;
  limit: number;
}

export class CorridaInexistente extends Error {}

/** De la mas reciente a la mas vieja. `hasMore` dice si hay otra pagina. */
export async function buscarEventos(consulta: ConsultaEventos) {
  const condiciones: Record<string, unknown>[] = [];
  if (consulta.levels && consulta.levels.length > 0) condiciones.push({ level: { $in: consulta.levels } });
  if (consulta.code) condiciones.push({ code: consulta.code });
  const ts: Record<string, Date> = {};
  if (consulta.from) ts.$gte = consulta.from;
  if (consulta.to) ts.$lte = consulta.to;
  if (consulta.before) ts.$lt = consulta.before;
  if (Object.keys(ts).length > 0) condiciones.push({ ts });

  const ventanas = await ventanasRecientes();
  const rango = (v: Ventana) => ({ ts: { $gte: new Date(v.desde), $lte: new Date(v.hasta) } });
  // Los eventos de una corrida: los de su ventana MAS los que traen su
  // numero cerca (el inicio, que cae un instante antes de la ventana).
  const deLaCorrida = (v: Ventana) => (v.corrida.number
    ? {
        $or: [rango(v), {
          runNumber: v.corrida.number,
          ts: { $gte: new Date(v.desde - MARGEN_NUMERO_MS), $lte: new Date(v.hasta + MARGEN_NUMERO_MS) },
        }],
      }
    : rango(v));

  if (consulta.runId || consulta.runNumber) {
    let ventana = consulta.runId
      ? ventanas.find((v) => v.id === consulta.runId)
      : ventanas.filter((v) => v.corrida.number === consulta.runNumber).sort((a, b) => b.desde - a.desde)[0];
    if (!ventana) {
      // Una corrida vieja: ya no tiene eventos (TTL), pero existe.
      const campos = '_id number startedAt endedAt runType state';
      const run = consulta.runId
        ? (mongoose.isValidObjectId(consulta.runId) ? await Run.findById(consulta.runId).select(campos).lean() : null)
        : await Run.findOne({ number: consulta.runNumber }).sort({ startedAt: -1 }).select(campos).lean();
      if (!run) throw new CorridaInexistente();
      ventana = ventanaDe(run);
    }
    condiciones.push(deLaCorrida(ventana));
    const docs = await BoardEvent.find({ $and: condiciones }).sort({ ts: -1, _id: -1 }).limit(consulta.limit + 1).lean();
    return {
      items: docs.slice(0, consulta.limit).map((doc) => eventoParaCliente(doc, ventanas)),
      hasMore: docs.length > consulta.limit,
      // Que corrida se filtro: con un numero, el cliente no sabe el id.
      run: ventana.corrida,
    };
  } else if (consulta.scope === 'in') {
    // Dentro de alguna ventana, o con numero de corrida (lo pone la placa
    // solo mientras corre).
    condiciones.push({ $or: [...ventanas.map(rango), { runNumber: { $gt: 0 } }] });
  } else if (consulta.scope === 'out') {
    condiciones.push({ $nor: [...ventanas.map(rango), { runNumber: { $gt: 0 } }] });
  }

  const filtro = condiciones.length > 0 ? { $and: condiciones } : {};
  const docs = await BoardEvent.find(filtro).sort({ ts: -1, _id: -1 }).limit(consulta.limit + 1).lean();
  return {
    items: docs.slice(0, consulta.limit).map((doc) => eventoParaCliente(doc, ventanas)),
    hasMore: docs.length > consulta.limit,
  };
}
