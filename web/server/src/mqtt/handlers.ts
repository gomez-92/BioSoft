import mongoose from 'mongoose';
import { config } from '../config.js';
import { valorAntesDeAlerta } from '../domain/alertas.js';
import {
  closeRun, getCurrentRunId, noteSample, openRun,
} from '../domain/runtracker.js';
import {
  parseAlert, parseCoils, parseMeasures, parseResult, parseStatus, parseTargets,
} from '../domain/telemetry.js';
import { Alert, CoilSample, Measure, StatusSample } from '../models/index.js';
import { emitTelemetry } from '../realtime/socket.js';
import { onTelemetry, type TelemetryMessage } from './ingestor.js';

// Unico camino de escritura: parsea, guarda, y RECIEN DESPUES emite por
// Socket.IO el mismo objeto que guardo. Nada se manda en vivo sin haberse
// persistido, asi que lo que se ve en el tablero y lo que se consulta despues
// no pueden diferir.

function meta() {
  return { runId: getCurrentRunId(), deviceId: config.deviceId };
}

// Exportada para los tests: es el corazon de la ingesta y se puede ejercitar
// sin broker, pasandole el mensaje ya despachado.
export async function handleTelemetry(message: TelemetryMessage): Promise<void> {
  // Un retenido no es un evento nuevo: es el ultimo estado conocido que el
  // broker reentrega en CADA suscripcion. Guardarlo insertaria una muestra
  // duplicada con la fecha equivocada (la de entrega, no la de medicion) en
  // cada reconexion, y -- mucho peor -- un `targets` retenido abriria una
  // corrida fantasma con los objetivos del experimento anterior, mientras un
  // `result` retenido cerraria la que esta corriendo ahora mismo.
  if (message.retained) return;

  // Sin Mongo no se guarda, y entonces tampoco se emite: preferimos un
  // tablero que no muestre el dato a uno que muestre algo que no quedo
  // registrado en ningun lado.
  if (mongoose.connection.readyState !== 1) {
    console.warn(`[ingesta] mongo desconectado, se descarta ${message.group}`);
    return;
  }

  const ts = message.receivedAt;

  switch (message.group) {
    // targets y result son el principio y el fin de una corrida. Van primero
    // porque de ellos depende a que corrida pertenece todo lo demas.
    case 'targets': {
      const parsed = parseTargets(message.payload);
      if (!parsed) return;
      const runId = await openRun(parsed, ts);
      emitTelemetry('targets', { ts, runId: runId.toString(), ...parsed });
      break;
    }

    case 'result': {
      const parsed = parseResult(message.payload);
      if (!parsed) return;
      const runId = await closeRun(parsed, ts);
      emitTelemetry('result', { ts, runId: runId.toString(), ...parsed });
      break;
    }

    case 'measures': {
      const parsed = parseMeasures(message.payload);
      if (!parsed) return;
      await Measure.create({ ts, meta: meta(), ...parsed });
      await noteSample(ts, 'measure');
      emitTelemetry('measures', { ts, ...parsed });
      break;
    }

    case 'coils': {
      const coils = parseCoils(message.payload);
      if (!coils) return;
      await CoilSample.create({ ts, meta: meta(), coils });
      await noteSample(ts, 'other');
      emitTelemetry('coils', { ts, coils });
      break;
    }

    case 'status': {
      const parsed = parseStatus(message.payload);
      if (!parsed) return;
      await StatusSample.create({ ts, meta: meta(), ...parsed });
      await noteSample(ts, 'other');
      emitTelemetry('status', { ts, ...parsed });
      break;
    }

    case 'alerts': {
      const parsed = parseAlert(message.payload);
      if (!parsed) return;
      await Alert.create({ ts, runId: getCurrentRunId(), deviceId: config.deviceId, ...parsed });
      await noteSample(ts, 'alert');
      // Con la ultima medicion de su fuente, igual que la API de historicos:
      // el detalle de la alerta en vivo y el del historial dicen lo mismo.
      const cercano = await valorAntesDeAlerta(getCurrentRunId(), parsed.source, ts);
      emitTelemetry('alert', { ts, ...parsed, ...cercano });
      break;
    }
  }
}

// Los mensajes se procesan DE A UNO, en el orden en que llegaron. El ingestor
// no espera al handler, asi que sin esto un `targets` y la primera muestra
// posterior podrian resolverse entrelazados y la muestra caeria fuera de la
// corrida que le corresponde. Encadenar es suficiente: el volumen es de
// decenas de mensajes por minuto, no miles.
let cadena: Promise<void> = Promise.resolve();

export function registerHandlers(): void {
  onTelemetry((message) => {
    cadena = cadena
      .then(() => handleTelemetry(message))
      .catch((error) => console.error(`[ingesta] fallo procesando ${message.group}:`, error));
    return cadena;
  });
}
