import { createHash, randomBytes } from 'node:crypto';
import { User } from '../models/user.js';
import { hashPassword } from './password.js';
import { ahoraParaRevocar, anotarUsuario } from './sesiones.js';

// Enlaces de un solo uso para fijar contraseña: la recuperacion ("olvide mi
// contraseña") y la invitacion de una cuenta nueva son el mismo mecanismo con
// distinta vigencia.
//
// En la base se guarda solo el HASH del token. El token es largo y al azar
// (256 bits), asi que un sha256 alcanza: no hace falta un hash lento como el
// de las contraseñas, que existe para proteger secretos cortos elegidos por
// personas.

export const MIN_CLAVE = 8;
export const HORAS_RECUPERACION = 2;
export const HORAS_INVITACION = 72;

function hashDeToken(token: string): string {
  return createHash('sha256').update(token).digest('hex');
}

/** Genera un token nuevo para el usuario (invalida el anterior). */
export async function crearTokenDeClave(userId: unknown, horas: number): Promise<{ token: string; expiresAt: Date }> {
  const token = randomBytes(32).toString('base64url');
  const expiresAt = new Date(Date.now() + horas * 3_600_000);
  await User.updateOne(
    { _id: userId },
    { $set: { resetTokenHash: hashDeToken(token), resetTokenExpiresAt: expiresAt } },
  );
  return { token, expiresAt };
}

export class ClaveInvalida extends Error {}

export function validarClave(password: unknown): string {
  if (typeof password !== 'string' || password.length < MIN_CLAVE) {
    throw new ClaveInvalida(`la contraseña tiene que tener al menos ${MIN_CLAVE} caracteres`);
  }
  if (password.length > 200) throw new ClaveInvalida('la contraseña es demasiado larga');
  return password;
}

/**
 * Cambia la contraseña y revoca todas las sesiones abiertas del usuario. Es
 * el unico camino por el que cambia una contraseña desde la web, sea por
 * enlace, por cambio propio o por un administrador.
 */
export async function fijarClave(userId: string, password: string): Promise<void> {
  const tokensValidAfter = ahoraParaRevocar();
  await User.updateOne(
    { _id: userId },
    {
      $set: { passwordHash: await hashPassword(password), tokensValidAfter },
      $unset: { resetTokenHash: '', resetTokenExpiresAt: '' },
    },
  );
  anotarUsuario(userId, tokensValidAfter);
}

/** Usa un token de enlace. Devuelve el usuario, o null si no vale (o vencio). */
export async function usarTokenDeClave(token: string, password: string): Promise<string | null> {
  if (!token) return null;
  const usuario = await User.findOne({
    resetTokenHash: hashDeToken(token),
    resetTokenExpiresAt: { $gt: new Date() },
  }).lean();
  if (!usuario) return null;
  await fijarClave(usuario._id.toString(), password);
  return usuario.username;
}
