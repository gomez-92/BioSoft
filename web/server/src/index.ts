import { createServer } from 'node:http';
import mongoose from 'mongoose';
import cors from 'cors';
import express from 'express';
import { healthRouter } from './api/health.js';
import { config } from './config.js';
import { connectMongo } from './db/mongo.js';
import { initRunTracker, sweepStaleRuns } from './domain/runtracker.js';
import { registerHandlers } from './mqtt/handlers.js';
import { startIngestor, stopIngestor } from './mqtt/ingestor.js';
import { startRealtime } from './realtime/socket.js';

const app = express();
app.use(cors());
app.use(express.json());
app.use('/api', healthRouter);

const server = createServer(app);

connectMongo();
startRealtime(server);   // antes del ingestor: el handler emite apenas guarda
registerHandlers();

// El tracker se reconstruye ANTES de suscribirse: si el backend se reinicio a
// mitad de un experimento, las muestras que lleguen tienen que caer en la
// corrida que ya estaba abierta y no en una nueva.
mongoose.connection.once('connected', () => {
  initRunTracker()
    .then(() => startIngestor())
    .catch((error) => {
      console.error('[app] no se pudo recuperar la corrida abierta:', error);
      startIngestor();
    });
});

// El barrido corre por intervalo, no por evento, porque el caso que atiende es
// justamente que dejaron de llegar eventos.
const barrido = setInterval(() => {
  if (mongoose.connection.readyState !== 1) return;
  sweepStaleRuns().catch((error) => console.error('[corridas] fallo el barrido:', error));
}, config.run.sweepIntervalSeconds * 1000);
barrido.unref();

server.listen(config.port, () => {
  console.log(`[http] escuchando en http://localhost:${config.port}`);
});

async function shutdown(signal: string): Promise<void> {
  console.log(`[app] ${signal} recibido, cerrando`);
  await stopIngestor();
  server.close(() => process.exit(0));
}

process.on('SIGINT', () => void shutdown('SIGINT'));
process.on('SIGTERM', () => void shutdown('SIGTERM'));
