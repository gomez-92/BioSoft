import type { NextFunction, Request, Response } from 'express';
import { tokenDeHeader, verifyToken, type Payload } from './tokens.js';

// Guarda de las rutas. Todo lo que devuelve datos del experimento pasa por
// aca; lo unico abierto es /api/ping, para que un chequeo de disponibilidad no
// necesite credenciales.
declare global {
  // eslint-disable-next-line @typescript-eslint/no-namespace
  namespace Express {
    interface Request { user?: Payload }
  }
}

export function requireAuth(req: Request, res: Response, next: NextFunction): void {
  const token = tokenDeHeader(req.headers.authorization);
  const payload = token ? verifyToken(token) : null;
  if (!payload) {
    res.status(401).json({ error: 'no autorizado' });
    return;
  }
  req.user = payload;
  next();
}
