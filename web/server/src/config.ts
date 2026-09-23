import { randomBytes } from 'node:crypto';
import 'dotenv/config';

// Toda la configuracion del backend en un solo lugar, leida una vez al
// arrancar. Los defaults son los de esp32/src/topics.hpp, asi que un .env
// vacio ya habla el mismo idioma que una placa sin tarjeta SD.

function randomSecret(): string {
  console.warn(
    '[auth] JWT_SECRET no esta definido: se usa una clave al azar. ' +
    'Las sesiones abiertas se invalidan en cada reinicio del servidor.',
  );
  return randomBytes(48).toString('hex');
}

function env(name: string, fallback: string): string {
  const value = process.env[name];
  return value === undefined || value === '' ? fallback : value;
}

export const config = {
  port: Number(env('PORT', '4000')),

  mongoUri: env('MONGO_URI', 'mongodb://localhost:27017/biosoft'),

  mqtt: {
    url: env('MQTT_URL', 'mqtt://localhost:1883'),
    username: process.env.MQTT_USER || undefined,
    password: process.env.MQTT_PASSWORD || undefined,
    clientId: env('MQTT_CLIENT_ID', 'biosoft-backend-dev'),
    caPath: process.env.MQTT_CA_PATH || undefined,
    // En la nube casi nunca se puede subir un archivo suelto: el certificado
    // se pega como variable de entorno. Se acepta cualquiera de las dos.
    caPem: process.env.MQTT_CA || undefined,
  },

  deviceId: env('DEVICE_ID', 'biosoft-01'),

  // Origenes permitidos para CORS. VACIO = mismo origen solamente, que es el
  // caso de produccion (el backend sirve el front). Solo hace falta listar
  // algo si el front se despliega en otro dominio.
  corsOrigins: (process.env.CORS_ORIGINS ?? '').split(',').map((o) => o.trim()).filter(Boolean),

  auth: {
    // Sin JWT_SECRET se genera uno al azar y se avisa: el servidor arranca
    // igual (util en desarrollo), pero cada reinicio invalida las sesiones
    // abiertas. En produccion tiene que estar puesto, o cada despliegue
    // desloguea a todo el mundo.
    secret: process.env.JWT_SECRET || randomSecret(),
    expiresIn: env('JWT_EXPIRES_IN', '7d'),
    // Semilla del primer usuario, solo si la base no tiene ninguno.
    seedUser: process.env.SEED_USER || '',
    seedPassword: process.env.SEED_PASSWORD || '',
  },

  run: {
    // Una corrida sin datos por este tiempo se da por muerta y se cierra como
    // huerfana. Tiene que ser holgadamente mayor al intervalo de `status`
    // (30 s por defecto), o un experimento sano con la red lenta se cerraria
    // solo; el default son 5 minutos, 10 tandas perdidas seguidas.
    staleAfterSeconds: Number(env('RUN_STALE_SECONDS', '300')),
    sweepIntervalSeconds: Number(env('RUN_SWEEP_SECONDS', '60')),
  },

  // El orden de las claves define el orden de la suscripcion; los nombres son
  // los que usa el resto del backend para despachar cada mensaje.
  topics: {
    measures: env('TOPIC_MEASURES', 'biosoft/telemetry/measures'),
    coils: env('TOPIC_COILS', 'biosoft/telemetry/coils'),
    status: env('TOPIC_STATUS', 'biosoft/telemetry/status'),
    targets: env('TOPIC_TARGETS', 'biosoft/telemetry/targets'),
    alerts: env('TOPIC_ALERTS', 'biosoft/telemetry/alerts'),
    result: env('TOPIC_RESULT', 'biosoft/telemetry/result'),
  },

  // Los cinco campos que la tarjeta SD puede renombrar. El resto de las claves
  // ("c1".."d4", "REASON", "SRC"...) son estructurales: parte de la forma del
  // mensaje, no valores que el operador elija, y por eso no se configuran.
  fields: {
    magneticField: env('FIELD_MAGNETIC', 'CEM1'),
    temperature: env('FIELD_TEMPERATURE', 'TEMP1'),
    health: env('FIELD_HEALTH', 'ESTADO'),
    progress: env('FIELD_PROGRESS', 'PROGRESS'),
    elapsed: env('FIELD_ELAPSED', 'ELAPSED_TIME'),
  },
} as const;

export type TelemetryGroup = keyof typeof config.topics;

// Topic -> grupo, para despachar sin comparar strings sueltos en el ingestor.
export function groupByTopic(): Map<string, TelemetryGroup> {
  const map = new Map<string, TelemetryGroup>();
  for (const [group, topic] of Object.entries(config.topics)) {
    map.set(topic, group as TelemetryGroup);
  }
  return map;
}
