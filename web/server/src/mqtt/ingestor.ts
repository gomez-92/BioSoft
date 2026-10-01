import { readFileSync } from 'node:fs';
import mqtt, { type MqttClient } from 'mqtt';
import { config, groupByTopic, type TelemetryGroup } from '../config.js';
import { emitLinkStatus } from '../realtime/socket.js';

// Un mensaje ya despachado a su grupo, antes de parsear. `retained` es el dato
// que no se puede perder: cinco de los seis grupos se publican con retain, asi
// que el broker reentrega el ultimo de cada uno en CADA reconexion. Tratarlos
// como eventos nuevos haria que cada reinicio del backend abriera un
// experimento fantasma y cerrara el que esta corriendo.
export interface TelemetryMessage {
  group: TelemetryGroup;
  topic: string;
  payload: unknown;
  retained: boolean;
  receivedAt: Date;
}

export type MessageHandler = (message: TelemetryMessage) => void | Promise<void>;

// El certificado puede venir como archivo (desarrollo) o pegado en una
// variable de entorno (la nube, donde no hay donde poner un archivo suelto).
/**
 * Un PEM que viene de una variable de entorno trae los saltos de linea como
 * "\n" LITERALES: un archivo .env no admite valores multilinea, y ninguna
 * plataforma deja pegar un certificado con saltos reales en un secreto.
 *
 * Hay que devolverlos a saltos de verdad o el certificado es texto invalido y
 * TLS lo rechaza -- con el mismo error que si no lo hubieras cargado, asi que
 * parece que el secreto no llego cuando en realidad llego mal.
 */
export function pemDesdeEntorno(valor: string): string {
  return valor.includes('\\n') ? valor.replace(/\\n/g, '\n') : valor;
}

function caDelBroker(): Buffer[] | undefined {
  if (config.mqtt.caPem) return [Buffer.from(pemDesdeEntorno(config.mqtt.caPem))];
  if (config.mqtt.caPath) return [readFileSync(config.mqtt.caPath)];
  return undefined;   // se usa el almacen de CAs del sistema
}

let client: MqttClient | undefined;
let connected = false;
const lastMessageAt = new Map<TelemetryGroup, Date>();
const handlers: MessageHandler[] = [];

export function onTelemetry(handler: MessageHandler): void {
  handlers.push(handler);
}

// Configuracion remota (tarjeta 24). Va por un camino aparte de la
// telemetria a proposito: aca los retenidos SI importan (la configuracion
// vigente se publica retenida) y no tiene nada que ver con las corridas.
export interface ConfigMessageIn { topic: string; payload: unknown; retained: boolean }
const configHandlers: Array<(message: ConfigMessageIn) => void | Promise<void>> = [];

export function onConfigMessage(handler: (message: ConfigMessageIn) => void | Promise<void>): void {
  configHandlers.push(handler);
}

/** Publica hacia la placa (QoS 1, sin retain). */
export async function publishToBoard(topic: string, payload: string): Promise<void> {
  if (!client || !connected) throw new Error('sin conexion con el broker');
  await client.publishAsync(topic, payload, { qos: 1, retain: false });
}

export function startIngestor(): void {
  const groups = groupByTopic();

  client = mqtt.connect(config.mqtt.url, {
    clientId: config.mqtt.clientId,
    username: config.mqtt.username,
    password: config.mqtt.password,
    // clientId fijo + clean:false para que una reconexion corta no pierda lo
    // publicado en el medio. No arregla un corte largo: la placa publica con
    // QoS 0 y no encola nada, asi que lo que se pierde mientras el backend
    // esta caido se pierde para siempre (huecos legitimos en el historico).
    clean: false,
    reconnectPeriod: 5000,
    ca: caDelBroker(),
  });

  client.on('connect', () => {
    connected = true;
    emitLinkStatus(true);
    console.log(`[mqtt] conectado a ${config.mqtt.url} como ${config.mqtt.clientId}`);
    const topics = [
      ...groups.keys(),
      `${config.configTopics.currentPrefix}#`,
      config.configTopics.status,
    ];
    for (const topic of topics) {
      client!.subscribe(topic, { qos: 1 }, (error) => {
        if (error) console.error(`[mqtt] no se pudo suscribir a ${topic}:`, error.message);
        else console.log(`[mqtt] suscripto a ${topic}`);
      });
    }
  });

  client.on('reconnect', () => console.log('[mqtt] reconectando...'));
  client.on('close', () => {
    connected = false;
    emitLinkStatus(false);
    console.warn('[mqtt] conexion cerrada');
  });
  client.on('error', (error) => console.error('[mqtt] error:', error.message));

  client.on('message', (topic, raw, packet) => {
    if (topic.startsWith(config.configTopics.currentPrefix) || topic === config.configTopics.status) {
      let payload: unknown;
      try {
        payload = JSON.parse(raw.toString());
      } catch {
        console.error(`[mqtt] payload no es JSON en ${topic}: ${raw.toString()}`);
        return;
      }
      const message = { topic, payload, retained: packet.retain === true };
      for (const handler of configHandlers) {
        Promise.resolve(handler(message)).catch((error) =>
          console.error(`[mqtt] handler de configuracion fallo en ${topic}:`, error),
        );
      }
      return;
    }

    const group = groups.get(topic);
    if (!group) return;

    let payload: unknown;
    try {
      payload = JSON.parse(raw.toString());
    } catch {
      // Un payload ilegible se loguea ENTERO a proposito: la causa tipica es
      // que la placa lo publico truncado o que alguien renombro algo, y el
      // texto crudo es la unica pista que queda.
      console.error(`[mqtt] payload no es JSON en ${topic}: ${raw.toString()}`);
      return;
    }

    const message: TelemetryMessage = {
      group,
      topic,
      payload,
      retained: packet.retain === true,
      receivedAt: new Date(),
    };
    lastMessageAt.set(group, message.receivedAt);

    for (const handler of handlers) {
      Promise.resolve(handler(message)).catch((error) =>
        console.error(`[mqtt] handler de ${group} fallo:`, error),
      );
    }
  });
}

export function mqttStatus() {
  return {
    connected,
    url: config.mqtt.url,
    clientId: config.mqtt.clientId,
    lastMessageAt: Object.fromEntries(
      [...lastMessageAt.entries()].map(([group, date]) => [group, date.toISOString()]),
    ),
  };
}

export async function stopIngestor(): Promise<void> {
  await client?.endAsync();
  connected = false;
}
