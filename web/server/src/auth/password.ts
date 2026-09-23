import { randomBytes, scrypt as scryptCallback, timingSafeEqual } from 'node:crypto';
import { promisify } from 'node:util';

// Hash de contraseñas con scrypt, que viene en Node y no necesita ninguna
// dependencia nativa. Se eligio sobre bcrypt por eso: una dependencia menos que
// compilar en el contenedor del despliegue, y scrypt esta diseñado para lo
// mismo (ser lento y caro en memoria a proposito, para que probar contraseñas
// al voleo no sea gratis).
//
// El formato guardado es "scrypt$<N>$<salt hex>$<hash hex>": lleva sus propios
// parametros adentro, asi que subir el costo mas adelante no invalida los
// hashes viejos -- cada uno se verifica con el N con el que se creo.

const scrypt = promisify(scryptCallback);

const N = 16384;     // costo de CPU/memoria
const LARGO = 64;    // bytes del hash
const SALT = 16;     // bytes del salt

export async function hashPassword(password: string): Promise<string> {
  const salt = randomBytes(SALT);
  const hash = (await scrypt(password, salt, LARGO)) as Buffer;
  return `scrypt$${N}$${salt.toString('hex')}$${hash.toString('hex')}`;
}

/**
 * Verifica una contraseña contra su hash.
 *
 * La comparacion es en TIEMPO CONSTANTE (timingSafeEqual): comparar con === se
 * corta en el primer byte distinto, y medir cuanto tarda la respuesta permite
 * adivinar el hash byte por byte. Es el mismo motivo por el que un usuario
 * inexistente tambien paga el costo de un scrypt en el login (ver api/auth.ts).
 */
export async function verifyPassword(password: string, guardado: string): Promise<boolean> {
  const partes = guardado.split('$');
  if (partes.length !== 4 || partes[0] !== 'scrypt') return false;

  const costo = Number(partes[1]);
  if (!Number.isInteger(costo) || costo < 1024) return false;

  const salt = Buffer.from(partes[2], 'hex');
  const esperado = Buffer.from(partes[3], 'hex');
  if (salt.length === 0 || esperado.length === 0) return false;

  const calculado = (await scrypt(password, salt, esperado.length)) as Buffer;
  return timingSafeEqual(calculado, esperado);
}
