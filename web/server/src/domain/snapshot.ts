import { config } from '../config.js';
import { Alert, CoilSample, Measure, Run, StatusSample } from '../models/index.js';
import { mqttStatus } from '../mqtt/ingestor.js';
import { getCurrentRunId } from './runtracker.js';

// El estado completo del sistema, armado desde la base.
//
// Esto es lo que reemplaza al `retain` del broker PARA EL NAVEGADOR. La placa
// publica con retain justamente para que un tablero que se abre a mitad de un
// experimento vea el estado actual en vez de una pantalla vacia hasta la
// proxima tanda -- pero el navegador no habla MQTT, asi que ese beneficio se
// perderia. El snapshot se lo devuelve, y ademas mejor: sale de lo que
// quedo GUARDADO, no de lo ultimo que el broker tenga en memoria.
//
// Se manda apenas un cliente se conecta por WebSocket, y tambien esta como
// endpoint REST para poder mirarlo con curl.

const MAX_ALERTAS = 5;

export interface Snapshot {
  deviceId: string;
  run: {
    id: string;
    startedAt: string;
    state: string;
    runType: string;
    targets: Record<string, unknown> | null;
  } | null;
  measures: { ts: string; magneticField?: number; temperature?: number } | null;
  coils: { ts: string; coils: Array<{ n: number; current?: number; duty?: number }> } | null;
  status: {
    ts: string; health?: string; progress?: number; elapsedSeconds?: number;
    elapsedText?: string; remainingSeconds?: number; state?: string; megaOk?: boolean;
  } | null;
  alerts: Array<{ ts: string; source: string; type: string; count: number; limit: number }>;
  link: { broker: boolean; lastMessageAt: Record<string, string> };
}

export async function buildSnapshot(): Promise<Snapshot> {
  const runId = getCurrentRunId();

  // Las alertas se acotan a la corrida en curso: una alerta de un experimento
  // anterior no dice nada del que esta pasando ahora, y mostrarla arriba
  // sugeriria un problema actual que no existe.
  const [run, measure, coil, status, alerts] = await Promise.all([
    runId ? Run.findById(runId).lean() : null,
    Measure.findOne().sort({ ts: -1 }).lean(),
    CoilSample.findOne().sort({ ts: -1 }).lean(),
    StatusSample.findOne().sort({ ts: -1 }).lean(),
    Alert.find(runId ? { runId } : { runId: null }).sort({ ts: -1 }).limit(MAX_ALERTAS).lean(),
  ]);

  return {
    deviceId: config.deviceId,
    run: run
      ? {
          id: run._id.toString(),
          startedAt: run.startedAt.toISOString(),
          state: run.state,
          runType: run.runType ?? 'unknown',
          targets: (run.targets as Record<string, unknown>) ?? null,
        }
      : null,
    measures: measure
      ? {
          ts: measure.ts.toISOString(),
          magneticField: measure.magneticField ?? undefined,
          temperature: measure.temperature ?? undefined,
        }
      : null,
    coils: coil
      ? {
          ts: coil.ts.toISOString(),
          coils: coil.coils.map((c) => ({
            n: c.n, current: c.current ?? undefined, duty: c.duty ?? undefined,
          })),
        }
      : null,
    status: status
      ? {
          ts: status.ts.toISOString(),
          health: status.health ?? undefined,
          progress: status.progress ?? undefined,
          elapsedSeconds: status.elapsedSeconds ?? undefined,
          elapsedText: status.elapsedText ?? undefined,
          remainingSeconds: status.remainingSeconds ?? undefined,
          state: status.state ?? undefined,
          megaOk: status.megaOk ?? undefined,
        }
      : null,
    alerts: alerts.map((alert) => ({
      ts: alert.ts.toISOString(),
      source: alert.source, type: alert.type, count: alert.count, limit: alert.limit,
    })),
    link: { broker: mqttStatus().connected, lastMessageAt: mqttStatus().lastMessageAt },
  };
}
