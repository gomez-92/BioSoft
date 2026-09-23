import type { NextFunction, Request, RequestHandler, Response } from 'express';
import mongoose from 'mongoose';

// Express 4 NO captura el rechazo de un handler asincronico: la promesa queda
// sin manejar y Node, por defecto, TERMINA EL PROCESO. Con la base caida, un
// solo intento de login alcanzaba para matar el servidor -- y como la
// plataforma lo reinicia, quedaba en un bucle de reinicios que desde afuera se
// ve como "la app no anda", sin ninguna pista de que la causa era Mongo.
//
// Esto pasó de verdad en el primer despliegue.

export function asincrono(handler: RequestHandler): RequestHandler {
  return (req, res, next) => {
    Promise.resolve(handler(req, res, next)).catch(next);
  };
}

/**
 * Corta con 503 cuando la base no esta conectada, en vez de dejar que la
 * consulta espere el buffer de mongoose y falle diez segundos despues.
 *
 * La diferencia importa: diez segundos de espera parecen un cuelgue, y el
 * error que llega despues habla de un timeout de `users.findOne()`, que no
 * dice nada sobre la causa real. Un 503 inmediato con el motivo escrito es
 * diagnostico, no ruido.
 */
export function requiereBase(_req: Request, res: Response, next: NextFunction): void {
  if (mongoose.connection.readyState === 1) { next(); return; }
  res.status(503).json({
    error: 'base de datos no disponible',
    detalle: 'el servidor no esta conectado a MongoDB; ver /api/ping',
  });
}

/** Ultimo recinto: cualquier error que llegue hasta aca se responde como 500. */
export function manejadorDeErrores(
  error: Error, _req: Request, res: Response, _next: NextFunction,
): void {
  console.error('[http] error no manejado:', error.message);
  if (res.headersSent) return;
  res.status(500).json({ error: 'error interno' });
}
