import { config } from '../config.js';

// Ping a la placa (esp32/src/remoteping.hpp). Es la unica forma de saber si
// la placa esta viva AHORA: fuera de un experimento no publica telemetria, y
// "En linea" en la cabecera solo dice que el monitor escucha al broker, no que
// del otro lado haya alguien.
//
// El monitor publica biosoft/ping {r} y la placa contesta biosoft/pong con el
// mismo r, su estado, su configId, si ve al Mega y cuanto lleva encendida.
// Nada de esto se persiste: es un estado del enlace, como el del broker, y
// guardar un pong cada 15 s no le serviria a nadie.

export type EstadoPlaca = 'desconocido' | 'conectada' | 'sin_respuesta';

export interface PlacaStatus {
  estado: EstadoPlaca;
  ultimoPing: string | null;
  ultimaRespuesta: string | null;
  latenciaMs: number | null;
  /** Estado de la app en la placa: idle/ready/starting/running/stopping. */
  appState: string | null;
  configId: string | null;
  /** Por que corre esa configuracion: ok/nosd/nofile/unreadable/invalid/schema. */
  configStatus: string | null;
  /** Si la placa ve al Mega por el serial. */
  mega: boolean | null;
  uptimeSeconds: number | null;
}

type Publicar = (topic: string, payload: string) => Promise<void>;
type Emitir = (evento: string, data: unknown) => void;

let publicar: Publicar | null = null;
let emitir: Emitir = () => {};
let ESPERA_MS = 5000;

const INICIAL: PlacaStatus = {
  estado: 'desconocido', ultimoPing: null, ultimaRespuesta: null, latenciaMs: null,
  appState: null, configId: null, configStatus: null, mega: null, uptimeSeconds: null,
};
let status: PlacaStatus = { ...INICIAL };

// Pings en vuelo: id -> numero de orden, momento de envio y quien espera.
// El numero de orden (no la hora) es lo que dice si un ping posterior ya
// contesto: dos pings en el mismo milisegundo empatarian por fecha.
const enVuelo = new Map<string, { seq: number; enviadoEn: number; resolver: (s: PlacaStatus) => void }>();
let ultimoSeq = 0;
let ultimoSeqRespondido = 0;

export function configurarPing(opciones: { publicar: Publicar; emitir?: Emitir }): void {
  publicar = opciones.publicar;
  if (opciones.emitir) emitir = opciones.emitir;
}

export function placaStatus(): PlacaStatus {
  return status;
}

function cambiar(cambios: Partial<PlacaStatus>): void {
  status = { ...status, ...cambios };
  emitir('board:status', status);
}

function nuevoId(): string {
  return `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`;
}

/**
 * Manda un ping y resuelve con el estado resultante: al llegar el pong o al
 * vencer la espera. No rechaza nunca por falta de respuesta -- "sin
 * respuesta" ES el resultado --, solo si no se pudo publicar.
 */
export async function pingPlaca(): Promise<PlacaStatus> {
  if (!publicar) throw new Error('el monitor no esta conectado al broker');
  const id = nuevoId();
  const enviadoEn = Date.now();
  const seq = ++ultimoSeq;

  const respuesta = new Promise<PlacaStatus>((resolve) => {
    enVuelo.set(id, { seq, enviadoEn, resolver: resolve });
    setTimeout(() => {
      if (!enVuelo.delete(id)) return;
      // Solo pasa a "sin respuesta" si no contesto NINGUN ping posterior a
      // este (dos pings solapados, el mas viejo vence despues).
      if (ultimoSeqRespondido < seq) cambiar({ estado: 'sin_respuesta', latenciaMs: null });
      resolve(status);
    }, ESPERA_MS).unref?.();
  });

  try {
    await publicar(config.boardTopics.ping, JSON.stringify({ r: id }));
  } catch (error) {
    enVuelo.delete(id);
    throw error;
  }
  status = { ...status, ultimoPing: new Date(enviadoEn).toISOString() };
  return respuesta;
}

function objeto(payload: unknown): Record<string, unknown> | null {
  return typeof payload === 'object' && payload !== null && !Array.isArray(payload)
    ? (payload as Record<string, unknown>)
    : null;
}

export function handlePong(payload: unknown, retained = false): void {
  // Un pong retenido no prueba nada sobre el presente (la placa no publica
  // retenido, pero un cliente cualquiera podria).
  if (retained) return;
  const data = objeto(payload);
  if (!data || typeof data.r !== 'string') return;
  const pendiente = enVuelo.get(data.r);
  // Un pong que no es de un ping nuestro (otro monitor, uno ya vencido) no
  // dice la latencia de nada.
  if (!pendiente) return;
  enVuelo.delete(data.r);

  const ahora = Date.now();
  ultimoSeqRespondido = Math.max(ultimoSeqRespondido, pendiente.seq);
  cambiar({
    estado: 'conectada',
    ultimaRespuesta: new Date(ahora).toISOString(),
    latenciaMs: ahora - pendiente.enviadoEn,
    appState: typeof data.st === 'string' ? data.st : null,
    configId: typeof data.cfg === 'string' ? data.cfg : null,
    configStatus: typeof data.cfgst === 'string' ? data.cfgst : null,
    mega: typeof data.mega === 'boolean' ? data.mega : null,
    uptimeSeconds: typeof data.up === 'number' ? data.up : null,
  });
  pendiente.resolver(status);
}

/** Ping periodico. Sin broker no hace nada: ese estado ya lo informa el enlace. */
export function iniciarPingPeriodico(intervaloMs: number, conectado: () => boolean): () => void {
  const id = setInterval(() => {
    if (!conectado()) return;
    pingPlaca().catch(() => {});
  }, intervaloMs);
  id.unref();
  return () => clearInterval(id);
}

// Solo para los tests.
export function _resetPing(esperaMs = 5000): void {
  enVuelo.clear();
  ultimoSeq = 0;
  ultimoSeqRespondido = 0;
  status = { ...INICIAL };
  ESPERA_MS = esperaMs;
}
