import { config } from '../config.js';
import { Alert, CoilSample, Measure, Run, StatusSample } from '../models/index.js';
import { mqttStatus } from '../mqtt/ingestor.js';
import { valorAntesDeAlerta } from './alertas.js';
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
  alerts: Array<{
    ts: string; source: string; type: string; count: number; limit: number;
    value?: number; valueAt?: Date;
  }>;
  /**
   * La ultima corrida terminada, con su resultado. Sin esto, recargar la
   * pagina despues de una prueba borraba el cartel de "como termino": el
   * resultado solo vivia en la memoria del navegador que lo vio llegar.
   */
  lastRun: {
    id: string; startedAt: string; endedAt: string | null; state: string; runType: string;
    result: Record<string, unknown> | null;
  } | null;
  link: { broker: boolean; lastMessageAt: Record<string, string> };
}

export async function buildSnapshot(): Promise<Snapshot> {
  const runId = getCurrentRunId();

  // Las alertas se acotan a la corrida en curso: una alerta de un experimento
  // anterior no dice nada del que esta pasando ahora, y mostrarla arriba
  // sugeriria un problema actual que no existe. Sin corrida en curso, ninguna:
  // antes se mostraban las de runId null -- las huerfanas, que llegaron sin
  // una corrida abierta (su `targets` nunca llego) --, y quedaban en el panel
  // indefinidamente con "Sin experimento en curso" al lado (banco,
  // 2026-10-03). No se borran: siguen siendo datos reales de la base.
  const [run, measure, coil, status, alerts, ultima] = await Promise.all([
    runId ? Run.findById(runId).lean() : null,
    Measure.findOne().sort({ ts: -1 }).lean(),
    CoilSample.findOne().sort({ ts: -1 }).lean(),
    StatusSample.findOne().sort({ ts: -1 }).lean(),
    runId ? Alert.find({ runId }).sort({ ts: -1 }).limit(MAX_ALERTAS).lean() : [],
    runId ? null : Run.findOne({ state: { $ne: 'running' } }).sort({ startedAt: -1 }).lean(),
  ]);
  const alertasConValor = await Promise.all(alerts.map(async (alert) => ({
    ts: alert.ts.toISOString(),
    source: alert.source, type: alert.type, count: alert.count, limit: alert.limit,
    ...(await valorAntesDeAlerta(runId, alert.source, alert.ts)),
  })));

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
    alerts: alertasConValor,
    lastRun: ultima
      ? {
          id: ultima._id.toString(),
          startedAt: ultima.startedAt.toISOString(),
          endedAt: ultima.endedAt?.toISOString() ?? null,
          state: ultima.state,
          runType: ultima.runType ?? 'unknown',
          result: (ultima.result as Record<string, unknown>) ?? null,
        }
      : null,
    link: { broker: mqttStatus().connected, lastMessageAt: mqttStatus().lastMessageAt },
  };
}
