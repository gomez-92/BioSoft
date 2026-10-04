import jwt from 'jsonwebtoken';
import { config } from '../config.js';

// Tokens de sesion. Se usa jsonwebtoken y no un HMAC a mano porque los errores
// clasicos de JWT (aceptar alg "none", confundir algoritmos, no chequear la
// expiracion) son faciles de cometer y esta libreria ya los tiene cerrados.
//
// El algoritmo se fija EN LA VERIFICACION, no solo al firmar: sin
// `algorithms: ['HS256']`, un token que dice venir con otro algoritmo podria
// llegar a validarse. Es la confusion de algoritmos, el agujero mas conocido
// de JWT.

// `iat` (emitido en, segundos) solo viene al verificar: lo pone la libreria al
// firmar, y es lo que auth/sesiones.ts compara para revocar sesiones viejas.
export interface Payload { sub: string; username: string; iat?: number }

export function signToken(payload: Payload): string {
  return jwt.sign({ sub: payload.sub, username: payload.username }, config.auth.secret, {
    algorithm: 'HS256',
    // El tipo de la libreria es un literal ("7d", "12h"...) y esto viene de
    // una variable de entorno, que es string a secas.
    expiresIn: config.auth.expiresIn as jwt.SignOptions['expiresIn'],
  });
}

export function verifyToken(token: string): Payload | null {
  try {
    const decoded = jwt.verify(token, config.auth.secret, { algorithms: ['HS256'] });
    if (typeof decoded === 'string' || !decoded.sub || typeof decoded.sub !== 'string') return null;
    return {
      sub: decoded.sub,
      username: String((decoded as Record<string, unknown>).username ?? ''),
      iat: typeof decoded.iat === 'number' ? decoded.iat : undefined,
    };
  } catch {
    // Token invalido, vencido o firmado con otra clave. No se distingue entre
    // los casos hacia afuera: decir "vencido" vs "invalido" le confirma a
    // quien prueba tokens que acerto la firma.
    return null;
  }
}

/** Extrae el token del encabezado "Authorization: Bearer <token>". */
export function tokenDeHeader(header: string | undefined): string | null {
  if (!header) return null;
  const [esquema, valor] = header.split(' ');
  return esquema?.toLowerCase() === 'bearer' && valor ? valor : null;
}
