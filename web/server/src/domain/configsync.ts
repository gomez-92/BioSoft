import { config } from '../config.js';
import { ConfigRequest, ConfigSnapshot } from '../models/index.js';
import { chunkText, crc32, problemaDeCredenciales, sinContrasenas } from './configchunks.js';

// Configuracion remota (tarjeta 24), las dos direcciones:
//
//  - IDA (placa -> monitor): la placa publica su configuracion vigente en
//    bloques RETENIDOS. Aca se reensamblan, se verifica que el CRC32 del
//    texto de exactamente el configId que la placa anuncia, y se guarda. Con
//    eso cada corrida (que trae su configId desde la tarjeta 23) queda
//    enlazada a su configuracion completa.
//  - VUELTA (monitor -> placa): una configuracion nueva, de a un bloque por
//    vez; el siguiente sale recien cuando la placa confirma el anterior.
//
// A diferencia de la telemetria, aca los retenidos SI se procesan: no son
// eventos sino el estado vigente, y reprocesarlos es idempotente (se guarda
// por configId).

export interface ConfigMessage {
  topic: string;
  payload: unknown;
  retained: boolean;
}

type Publicar = (topic: string, payload: string) => Promise<void>;
type Emitir = (evento: string, data: unknown) => void;

let publicar: Publicar | null = null;
let emitir: Emitir = () => {};

export function configurarSync(opciones: { publicar: Publicar; emitir?: Emitir }): void {
  publicar = opciones.publicar;
  if (opciones.emitir) emitir = opciones.emitir;
}

function objeto(payload: unknown): Record<string, unknown> | null {
  return typeof payload === 'object' && payload !== null && !Array.isArray(payload)
    ? (payload as Record<string, unknown>)
    : null;
}

/* ------------------------------ IDA ------------------------------ */

interface Efectivo { wifi: string[]; broker: Record<string, unknown> | null }
interface Armado { total?: number; loadStatus?: string; efectivo?: Efectivo; bloques: Map<number, string> }

// Los motivos que la placa puede informar (ConfigLoader::loadStatus). Uno
// desconocido se guarda como null: mejor "no se sabe" que un texto inventado.
const LOAD_STATUS = new Set(['ok', 'nosd', 'nofile', 'unreadable', 'invalid', 'schema', 'experiment']);

// Lo efectivo de la meta ("ew" redes, "eb" broker), solo con los tipos esperados.
function leerEfectivo(data: Record<string, unknown>): Efectivo | undefined {
  if (!Array.isArray(data.ew) && !objeto(data.eb)) return undefined;
  const wifi = Array.isArray(data.ew) ? data.ew.filter((s): s is string => typeof s === 'string') : [];
  const eb = objeto(data.eb);
  const broker = eb ? Object.fromEntries(
    ['server', 'port', 'user', 'clientId'].filter((k) => k in eb && ['string', 'number'].includes(typeof eb[k]))
      .map((k) => [k, eb[k]]),
  ) : null;
  return { wifi, broker };
}
const armando = new Map<string, Armado>();

async function completarSiEsta(id: string): Promise<void> {
  const armado = armando.get(id);
  if (!armado || armado.total === undefined) return;

  // n = 0: la placa corre con los defaults compilados, no hay archivo.
  if (armado.total === 0) {
    armando.delete(id);
    await ConfigSnapshot.updateOne(
      { configId: id },
      {
        $set: { lastReportedAt: new Date(), loadStatus: armado.loadStatus ?? null, effective: armado.efectivo ?? null },
        $setOnInsert: { text: '', content: null },
      },
      { upsert: true },
    );
    emitir('config:current', { configId: id });
    return;
  }
  if (armado.bloques.size < armado.total) return;

  const partes: string[] = [];
  for (let i = 0; i < armado.total; i++) {
    const parte = armado.bloques.get(i);
    if (parte === undefined) return;
    partes.push(parte);
  }
  armando.delete(id);
  const texto = partes.join('');

  // El CRC32 del texto TIENE que dar el id: es lo que prueba que el texto
  // reensamblado es el que la placa hasheo (y no una mezcla de bloques de
  // dos configuraciones, que es posible con retenidos viejos en el broker).
  if (crc32(texto) !== id) {
    console.warn(`[config] la configuracion ${id} reensamblada no coincide con su CRC: se descarta`);
    return;
  }
  let contenido: unknown;
  try {
    contenido = JSON.parse(texto);
  } catch {
    console.warn(`[config] la configuracion ${id} no es JSON valido: se descarta`);
    return;
  }

  await ConfigSnapshot.updateOne(
    { configId: id },
    { $set: {
      text: texto, content: contenido, lastReportedAt: new Date(), loadStatus: armado.loadStatus ?? 'ok',
      effective: armado.efectivo ?? null,
    } },
    { upsert: true },
  );
  console.log(`[config] configuracion vigente de la placa: ${id} (${armado.total} bloques)`);
  emitir('config:current', { configId: id });
}

async function recibirVigente(topic: string, payload: unknown): Promise<void> {
  const data = objeto(payload);
  if (!data || typeof data.id !== 'string' || !/^[0-9a-f]{8}$|^default$/.test(data.id)) return;
  const id = data.id;
  const armado = armando.get(id) ?? { bloques: new Map<number, string>() };
  armando.set(id, armado);

  const sufijo = topic.slice(config.configTopics.currentPrefix.length);
  if (sufijo === 'meta') {
    if (typeof data.n === 'number') armado.total = data.n;
    if (typeof data.src === 'string' && LOAD_STATUS.has(data.src)) armado.loadStatus = data.src;
    const efectivo = leerEfectivo(data);
    if (efectivo) armado.efectivo = efectivo;
    // La placa publica la meta AL FINAL: para entonces los bloques ya
    // completaron la configuracion y el armado se cerro. Lo que trae la meta
    // (por que corre, y las redes y el broker efectivos) se aplica a lo ya
    // guardado; si todavia no existe, lo toma el cierre de mas abajo.
    const tarde: Record<string, unknown> = {};
    if (armado.loadStatus) tarde.loadStatus = armado.loadStatus;
    if (armado.efectivo) tarde.effective = armado.efectivo;
    if (Object.keys(tarde).length > 0) {
      const r = await ConfigSnapshot.updateOne({ configId: id }, { $set: tarde });
      if (r.matchedCount > 0 && armado.bloques.size === 0 && (armado.total ?? 0) > 0) {
        // Ya estaba guardada: este armado de la meta sola no espera bloques.
        armando.delete(id);
        emitir('config:current', { configId: id });
        return;
      }
    }
  } else if (typeof data.i === 'number' && typeof data.d === 'string') {
    armado.bloques.set(data.i, data.d);
    if (typeof data.n === 'number') armado.total = data.n;
  }
  await completarSiEsta(id);
}

/* ----------------------------- VUELTA ----------------------------- */

// Estados que cierran un pedido. "parcial" no: es la confirmacion de un
// bloque, y es lo que habilita mandar el siguiente.
const FINALES = new Set(['aceptada', 'invalida', 'ocupada', 'incompleta', 'error', 'no_entregada', 'revertida', 'red_conservada']);

let ESPERA_BLOQUE_MS = 5000;
const REINTENTOS = 3;

type Respuesta = { st: string; i?: number; msg?: string; id?: string };
const esperando = new Map<string, (respuesta: Respuesta) => void>();

let enCurso: string | null = null;

export function pedidoEnCurso(): string | null {
  return enCurso;
}

function nuevoId(): string {
  return `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 8)}`;
}

function esperar(requestId: string, ms: number): Promise<Respuesta | null> {
  return new Promise((resolve) => {
    const timer = setTimeout(() => {
      esperando.delete(requestId);
      resolve(null);
    }, ms);
    esperando.set(requestId, (respuesta) => {
      clearTimeout(timer);
      esperando.delete(requestId);
      resolve(respuesta);
    });
  });
}

async function actualizar(requestId: string, cambios: Record<string, unknown>): Promise<void> {
  const pedido = await ConfigRequest.findOneAndUpdate(
    { requestId }, { $set: cambios }, { new: true },
  ).select('-text').lean();
  if (pedido) emitir('config:request', pedido);
}

export class PedidoInvalido extends Error {}

const SENSOR_REAL: Record<string, string> = { CEM1: 'mlx90393', TEMP1: 'ds18b20' };

/**
 * Un EXPERIMENTO (todo runType que no sea "test": ausente o mal escrito, el
 * firmware lo toma como normal) corre si o si con los sensores reales y el Detector encendido
 * (decision del 2026-11-19). El generador ya no deja armar otra cosa; esto es
 * la misma regla del lado del servidor, para un archivo editado a mano o un
 * generador viejo. Devuelve el motivo, o null si es coherente. Las claves
 * ausentes cuentan con su default compilado, que es el sensor real y el
 * Detector encendido.
 */
export function incoherenciaDeExperimento(data: Record<string, unknown>): string | null {
  // Experimento es todo lo que no es "test", como en la placa
  // (ConfigLoader::loadRunType): un runType mal escrito tambien.
  if (data.runType === 'test') return null;
  const detector = (data.detector ?? {}) as { enabled?: unknown; sources?: unknown };
  if (detector.enabled === false) {
    return 'un experimento (runType normal) tiene que correr con el Detector encendido; para apagarlo, declarar la corrida como test';
  }
  const fuentes = Array.isArray(detector.sources) ? detector.sources as Array<{ name?: unknown; sensor?: unknown }> : [];
  for (const fuente of fuentes) {
    const real = typeof fuente.name === 'string' ? SENSOR_REAL[fuente.name] : undefined;
    if (real && fuente.sensor !== undefined && fuente.sensor !== real) {
      return `un experimento (runType normal) corre solo con sensores reales: ${String(fuente.name)} esta en ` +
        `"${String(fuente.sensor)}" y tiene que ser "${real}"; para simular, declarar la corrida como test`;
    }
  }
  return null;
}

/**
 * Valida lo minimo que el monitor puede garantizar (la validacion de cada
 * regla la hizo el generador embebido, y la placa vuelve a parsear) y
 * arranca el envio. No espera a que termine: el estado se sigue por el
 * documento del pedido y por Socket.IO.
 */
export async function enviarConfiguracion(
  configuracion: unknown, usuario: string,
): Promise<{ requestId: string; bloques: number }> {
  if (!publicar) throw new PedidoInvalido('el monitor no esta conectado al broker');
  if (enCurso) throw new PedidoInvalido('ya hay un envio en curso');

  const data = objeto(configuracion);
  if (!data) throw new PedidoInvalido('la configuracion tiene que ser un objeto JSON');
  if (data.schemaVersion !== 1) throw new PedidoInvalido('schemaVersion tiene que ser 1');
  const incoherencia = incoherenciaDeExperimento(data);
  if (incoherencia) throw new PedidoInvalido(incoherencia);

  const credenciales = problemaDeCredenciales(data);
  if (credenciales) throw new PedidoInvalido(credenciales);
  // Con el mismo client id que este monitor, el broker echaria a uno de los
  // dos cada vez que el otro conecta: la placa y el monitor se turnarian.
  const clientId = (data.broker as { clientId?: unknown } | undefined)?.clientId;
  if (typeof clientId === 'string' && clientId === config.mqtt.clientId) {
    throw new PedidoInvalido(`broker.clientId "${clientId}" es el del monitor: la placa necesita uno distinto`);
  }

  // Lo que viaja lleva las contrasenas que se hayan escrito; lo que se
  // guarda en la base, no (ver sinContrasenas).
  const texto = JSON.stringify(data);
  const guardado = JSON.stringify(sinContrasenas(data));
  // Tope de la placa: ConfigLoader lee con un buffer de 8 KB, y el archivo
  // que graba (minificado) puede sumar las contrasenas que ya tenia.
  const bytes = Buffer.byteLength(texto, 'utf8');
  if (bytes > 7000) {
    throw new PedidoInvalido(`la configuracion pesa ${bytes} bytes; el tope es 7000`);
  }

  const bloques = chunkText(texto);
  const requestId = nuevoId();
  await ConfigRequest.create({
    requestId, user: usuario, text: guardado, chunks: bloques.length, state: 'enviando', sentChunks: 0,
  });
  enCurso = requestId;

  void mandarBloques(requestId, bloques).finally(() => {
    if (enCurso === requestId) enCurso = null;
  });
  return { requestId, bloques: bloques.length };
}

async function mandarBloques(requestId: string, bloques: string[]): Promise<void> {
  const total = bloques.length;
  for (let i = 0; i < total; i++) {
    let respuesta: Respuesta | null = null;
    for (let intento = 0; intento < REINTENTOS && !respuesta; intento++) {
      const limite = Date.now() + ESPERA_BLOQUE_MS;
      let espera = esperar(requestId, ESPERA_BLOQUE_MS);
      await publicar!(config.configTopics.set, JSON.stringify({ r: requestId, i, n: total, d: bloques[i] }));
      respuesta = await espera;
      // Una confirmacion de OTRO bloque (la segunda respuesta a un reenvio
      // que llego tarde) no cuenta, pero tampoco gasta el intento: se sigue
      // esperando lo que queda del plazo SIN reenviar. Antes se reenviaba,
      // y cada reenvio generaba otra confirmacion repetida que caia sobre
      // el bloque siguiente: en el banco (2026-10-03) 18 "parcial" para 12
      // bloques, y reenvios que llegaban despues de "aceptada".
      while (respuesta && respuesta.st === 'parcial' && respuesta.i !== i) {
        const resta = limite - Date.now();
        if (resta <= 0) { respuesta = null; break; }
        espera = esperar(requestId, resta);
        respuesta = await espera;
      }
    }

    if (!respuesta) {
      await actualizar(requestId, {
        state: 'no_entregada', message: `la placa no confirmo el bloque ${i + 1} de ${total}`,
      });
      return;
    }
    if (FINALES.has(respuesta.st)) {
      await actualizar(requestId, {
        state: respuesta.st, message: respuesta.msg ?? null, newConfigId: respuesta.id ?? null,
        sentChunks: i + 1,
      });
      return;
    }
    await actualizar(requestId, { state: 'parcial', sentChunks: i + 1 });
  }
  // El ultimo bloque deberia haber cerrado el pedido con su estado final.
  await actualizar(requestId, { state: 'no_entregada', message: 'la placa no informo el resultado' });
}

async function recibirEstado(payload: unknown): Promise<void> {
  const data = objeto(payload);
  if (!data || typeof data.r !== 'string' || typeof data.st !== 'string') return;
  const respuesta: Respuesta = {
    st: data.st,
    i: typeof data.i === 'number' ? data.i : undefined,
    msg: typeof data.msg === 'string' ? data.msg : undefined,
    id: typeof data.id === 'string' ? data.id : undefined,
  };
  const despertar = esperando.get(data.r);
  if (despertar) {
    despertar(respuesta);
    return;
  }
  // Un estado final sin nadie esperando (p. ej. "ocupada" de un pedido que ya
  // se dio por perdido): igual se registra, pero solo sobre un pedido que no
  // tiene ya un resultado de la placa. Un reenvio tardio del ultimo bloque
  // llega despues de "aceptada", la placa contesta "incompleta" y eso
  // pisaba el resultado real: la SD estaba grabada y el monitor decia que no.
  // "revertida" llega despues de "aceptada", a veces minutos despues: la
  // placa probo la configuracion, no llego al broker y volvio a la anterior
  // (MySystem::_checkConfigTrial). Es lo unico que puede pisar un "aceptada".
  // "red_conservada" tambien: se aplico, pero la red en uso que se quito no
  // se pudo borrar porque las otras no conectaron.
  if (respuesta.st === 'revertida' || respuesta.st === 'red_conservada') {
    const pedido = await ConfigRequest.findOneAndUpdate(
      { requestId: data.r, state: { $in: ['aceptada', ...ABIERTOS] } },
      { $set: { state: respuesta.st, message: respuesta.msg ?? null } }, { new: true },
    ).select('-text').lean();
    if (pedido) emitir('config:request', pedido);
    return;
  }
  if (FINALES.has(respuesta.st)) {
    await actualizarSiAbierto(data.r, {
      state: respuesta.st, message: respuesta.msg ?? null, newConfigId: respuesta.id ?? null,
    });
  }
}

// Abiertos: todavia sin respuesta final de la placa. "no_entregada" lo puso
// el monitor (se canso de esperar), asi que una respuesta tardia si lo
// corrige.
const ABIERTOS = ['enviando', 'parcial', 'no_entregada'];

async function actualizarSiAbierto(requestId: string, cambios: Record<string, unknown>): Promise<void> {
  const pedido = await ConfigRequest.findOneAndUpdate(
    { requestId, state: { $in: ABIERTOS } }, { $set: cambios }, { new: true },
  ).select('-text').lean();
  if (pedido) emitir('config:request', pedido);
}

/* ----------------------------- entrada ----------------------------- */

export async function handleConfigMessage(message: ConfigMessage): Promise<void> {
  if (message.topic.startsWith(config.configTopics.currentPrefix)) {
    await recibirVigente(message.topic, message.payload);
  } else if (message.topic === config.configTopics.status && !message.retained) {
    await recibirEstado(message.payload);
  }
}

// Solo para los tests.
export function _resetConfigSync(esperaMs = 5000): void {
  armando.clear();
  esperando.clear();
  enCurso = null;
  ESPERA_BLOQUE_MS = esperaMs;
}
