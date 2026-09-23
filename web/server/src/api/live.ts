import { Router } from 'express';
import mongoose from 'mongoose';
import { buildSnapshot } from '../domain/snapshot.js';

// El mismo snapshot que recibe un cliente al conectarse por WebSocket, pero
// por HTTP: sirve para mirarlo con curl sin abrir el navegador, y es el
// fallback si el WebSocket no puede establecerse.
export const liveRouter = Router();

liveRouter.get('/live/snapshot', async (_req, res) => {
  if (mongoose.connection.readyState !== 1) {
    res.status(503).json({ error: 'mongo desconectado' });
    return;
  }
  res.json(await buildSnapshot());
});
