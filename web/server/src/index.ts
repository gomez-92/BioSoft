import { createServer } from 'node:http';
import mongoose from 'mongoose';
import cors from 'cors';
import express from 'express';
import { authRouter } from './api/auth.js';
import { healthRouter } from './api/health.js';
import { liveRouter } from './api/live.js';
import { runsRouter } from './api/runs.js';
import { config } from './config.js';
import { requireAuth } from './auth/middleware.js';
import { seedInitialUser } from './auth/seed.js';
import { connectMongo } from './db/mongo.js';
import { initRunTracker, sweepStaleRuns } from './domain/runtracker.js';
import { registerHandlers } from './mqtt/handlers.js';
import { startIngestor, stopIngestor } from './mqtt/ingestor.js';
import { startRealtime } from './realtime/socket.js';

const app = express();
app.use(cors());
app.use(express.json());
// Lo unico publico: un latido para chequeos de disponibilidad, que no dice
// nada del experimento ni de la infraestructura.
app.get('/api/ping', (_req, res) => { res.json({ ok: true }); });

app.use('/api', authRouter);

// De aca para abajo todo exige token. /api/health incluido: dice la URL del
// broker, el clientId y cuantos datos hay guardados -- poco para un atacante,
// pero no hay ninguna razon para regalarlo.
app.use('/api', requireAuth, healthRouter);
app.use('/api', requireAuth, liveRouter);
app.use('/api', requireAuth, runsRouter);

const server = createServer(app);

connectMongo();
startRealtime(server);   // antes del ingestor: el handler emite apenas guarda
registerHandlers();

// El tracker se reconstruye ANTES de suscribirse: si el backend se reinicio a
// mitad de un experimento, las muestras que lleguen tienen que caer en la
// corrida que ya estaba abierta y no en una nueva.
mongoose.connection.once('connected', () => {
  seedInitialUser().catch((error) => console.error('[auth] fallo la semilla:', error));
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
