import { Router } from 'express';
import { config } from '../config.js';
import { mongoStatus } from '../db/mongo.js';
import { mqttStatus } from '../mqtt/ingestor.js';
import { realtimeStatus } from '../realtime/socket.js';

// La primera pregunta cuando "no llega el dato" es cual de los tres enlaces se
// cayo. Este endpoint la contesta sin mirar logs.
export const healthRouter = Router();

healthRouter.get('/health', (_req, res) => {
  const mongo = mongoStatus();
  const mqtt = mqttStatus();
  res.json({
    ok: mongo.connected && mqtt.connected,
    deviceId: config.deviceId,
    uptimeSeconds: Math.floor(process.uptime()),
    mongo,
    mqtt,
    realtime: realtimeStatus(),
    topics: config.topics,
  });
});
