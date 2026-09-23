import mongoose from 'mongoose';
import { config } from '../config.js';
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
//
// FASE 1: todavia no hay correlacion de corridas, asi que todo entra con
// runId null. La apertura y el cierre de `runs` es la fase 2; por eso
// `currentRunId()` esta aislado aca, es el unico punto que esa fase toca.

function currentRunId(): mongoose.Types.ObjectId | null {
  return null;   // fase 2
}

function meta() {
  return { runId: currentRunId(), deviceId: config.deviceId };
}

// Exportada para los tests: es el corazon de la ingesta y se puede ejercitar
// sin broker, pasandole el mensaje ya despachado.
export async function handleTelemetry(message: TelemetryMessage): Promise<void> {
  // Un retenido no es un evento nuevo: es el ultimo estado conocido que el
  // broker reentrega en CADA suscripcion. Guardarlo insertaria una muestra
  // duplicada con la fecha equivocada (la de entrega, no la de medicion) en
  // cada reconexion del backend, y en la fase 2 abriria un experimento
  // fantasma con un `targets` viejo. Va a alimentar el snapshot inicial de la
  // fase 3, no la base ni el vivo.
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
    case 'measures': {
      const parsed = parseMeasures(message.payload);
      if (!parsed) return;
      await Measure.create({ ts, meta: meta(), ...parsed });
      emitTelemetry('measures', { ts, ...parsed });
      break;
    }

    case 'coils': {
      const coils = parseCoils(message.payload);
      if (!coils) return;
      await CoilSample.create({ ts, meta: meta(), coils });
      emitTelemetry('coils', { ts, coils });
      break;
    }

    case 'status': {
      const parsed = parseStatus(message.payload);
      if (!parsed) return;
      await StatusSample.create({ ts, meta: meta(), ...parsed });
      emitTelemetry('status', { ts, ...parsed });
      break;
    }

    case 'alerts': {
      const parsed = parseAlert(message.payload);
      if (!parsed) return;
      await Alert.create({ ts, runId: currentRunId(), deviceId: config.deviceId, ...parsed });
      emitTelemetry('alert', { ts, ...parsed });
      break;
    }

    // targets y result definen el principio y el fin de una corrida, asi que
    // en la fase 2 van a abrirla y cerrarla. Por ahora solo se reemiten en
    // vivo: guardarlos sueltos, fuera de un `run`, seria un dato que despues
    // habria que migrar.
    case 'targets': {
      const parsed = parseTargets(message.payload);
      if (!parsed) return;
      emitTelemetry('targets', { ts, ...parsed });
      break;
    }

    case 'result': {
      const parsed = parseResult(message.payload);
      if (!parsed) return;
      emitTelemetry('result', { ts, ...parsed });
      break;
    }
  }
}

export function registerHandlers(): void {
  onTelemetry((message) => handleTelemetry(message));
}
