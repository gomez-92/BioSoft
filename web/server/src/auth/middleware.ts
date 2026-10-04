import type { NextFunction, Request, Response } from 'express';
import mongoose from 'mongoose';
import { User } from '../models/user.js';
import { sesionVigente } from './sesiones.js';
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
  // Firma valida no alcanza: un usuario borrado, o una sesion abierta antes
  // de cambiar la contraseña, ya no entra (ver auth/sesiones.ts).
  if (!payload || !sesionVigente(payload)) {
    res.status(401).json({ error: 'no autorizado' });
    return;
  }
  req.user = payload;
  next();
}

/**
 * Solo administradores. Va DESPUES de requireAuth.
 *
 * El rol se lee de la base en cada pedido y no del token: si viviera en el
 * token, quitarle el rol a alguien recien surtiria efecto cuando su sesion
 * venciera, una semana despues. Las rutas que protege (configuracion,
 * borrado, usuarios) necesitan la base de todas formas.
 */
export function requireAdmin(req: Request, res: Response, next: NextFunction): void {
  if (mongoose.connection.readyState !== 1) {
    res.status(503).json({ error: 'base de datos no disponible' });
    return;
  }
  if (!mongoose.isValidObjectId(req.user?.sub)) {
    res.status(403).json({ error: 'hace falta ser administrador' });
    return;
  }
  User.findById(req.user?.sub).select('role').lean()
    .then((usuario) => {
      if (usuario?.role !== 'admin') {
        res.status(403).json({ error: 'hace falta ser administrador' });
        return;
      }
      next();
    })
    .catch(next);
}
