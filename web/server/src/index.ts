import { createServer } from 'node:http';
import cors from 'cors';
import express from 'express';
import { healthRouter } from './api/health.js';
import { config } from './config.js';
import { connectMongo } from './db/mongo.js';
import { startIngestor, stopIngestor } from './mqtt/ingestor.js';
import { startRealtime } from './realtime/socket.js';

const app = express();
app.use(cors());
app.use(express.json());
app.use('/api', healthRouter);

const server = createServer(app);

connectMongo();
startIngestor();
startRealtime(server);

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
