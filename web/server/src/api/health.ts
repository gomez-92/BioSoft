import { Router } from 'express';
import { asincrono } from './asincrono.js';
import { config } from '../config.js';
import { mongoStatus } from '../db/mongo.js';
import { getCurrentRunId } from '../domain/runtracker.js';
import { Alert, CoilSample, Measure, Run, StatusSample } from '../models/index.js';
import { mqttStatus } from '../mqtt/ingestor.js';
import { realtimeStatus } from '../realtime/socket.js';

// La primera pregunta cuando "no llega el dato" es cual de los tres enlaces se
// cayo. Este endpoint la contesta sin mirar logs.
export const healthRouter = Router();

healthRouter.get('/health', asincrono(async (_req, res) => {
  const mongo = mongoStatus();
  const mqtt = mqttStatus();

  // Cuantos documentos hay de cada cosa. Es la forma mas directa de ver si la
  // ingesta esta escribiendo de verdad, y no solo recibiendo.
  let stored: Record<string, number> | undefined;
  if (mongo.connected) {
    const [runs, measures, coils, statuses, alerts] = await Promise.all([
      Run.countDocuments(), Measure.countDocuments(), CoilSample.countDocuments(),
      StatusSample.countDocuments(), Alert.countDocuments(),
    ]);
    stored = { runs, measures, coils, statuses, alerts };
  }

  res.json({
    ok: mongo.connected && mqtt.connected,
    deviceId: config.deviceId,
    uptimeSeconds: Math.floor(process.uptime()),
    mongo,
    mqtt,
    stored,
    currentRun: getCurrentRunId()?.toString() ?? null,
    realtime: realtimeStatus(),
    topics: config.topics,
  });
}));
