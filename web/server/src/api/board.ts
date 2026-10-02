import { Router } from 'express';
import { asincrono } from './asincrono.js';
import { pingPlaca, placaStatus } from '../domain/boardping.js';

// Estado del enlace con la placa (ping/pong, esp32/src/remoteping.hpp).
export const boardRouter = Router();

boardRouter.get('/board', (_req, res) => {
  res.json(placaStatus());
});

/** Ping a pedido: espera el pong (o el vencimiento) y devuelve el estado. */
boardRouter.post('/board/ping', asincrono(async (_req, res) => {
  try {
    res.json(await pingPlaca());
  } catch (error) {
    res.status(503).json({ error: (error as Error).message, ...placaStatus() });
  }
}));
