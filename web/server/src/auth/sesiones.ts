import { User } from '../models/user.js';
import type { Payload } from './tokens.js';

// Revocacion de sesiones.
//
// Los tokens son JWT: se validan con la firma, sin preguntarle a la base. Eso
// es lo que deja al ping y a /api/health contestar con Mongo caido, y no se
// quiere perder. La contracara es que un token firmado vale hasta que vence
// (7 dias por defecto) aunque al usuario lo hayan borrado o le hayan cambiado
// la contraseña justamente para sacarlo.
//
// Se resuelve con una tabla EN MEMORIA, por usuario, de "los tokens emitidos
// antes de tal momento ya no valen". Se carga de la base al conectar y se
// actualiza con cada cambio hecho desde este servidor. Son pocos usuarios.
//
// Un usuario que NO esta en la tabla se deja pasar: puede haberlo creado el
// script de consola (otro proceso) despues de la carga, y un token valido
// solo lo pudo firmar este servidor. Por eso al borrar un usuario no se lo
// saca de la tabla: se lo deja marcado con "nada vale" (Infinity).

const validoDesde = new Map<string, number>();
let cargada = false;

/**
 * Migracion de arranque + carga de la tabla. Las cuentas anteriores a los
 * roles pasan a "admin": antes de los roles todas podian hacer todo, y dejar
 * el monitor sin ningun administrador lo dejaria sin forma de gestionar
 * usuarios salvo por consola.
 */
export async function cargarSesiones(): Promise<void> {
  const migradas = await User.updateMany({ role: { $exists: false } }, { $set: { role: 'admin' } });
  if (migradas.modifiedCount > 0) {
    console.log(`[auth] ${migradas.modifiedCount} usuario(s) anteriores a los roles pasan a admin`);
  }
  const usuarios = await User.find().select('_id tokensValidAfter').lean();
  validoDesde.clear();
  for (const usuario of usuarios) {
    validoDesde.set(usuario._id.toString(), usuario.tokensValidAfter?.getTime() ?? 0);
  }
  cargada = true;
}

/** Despues de crear un usuario o de mover su tokensValidAfter. */
export function anotarUsuario(id: string, tokensValidAfter: Date | null | undefined): void {
  validoDesde.set(id, tokensValidAfter?.getTime() ?? 0);
}

/** Despues de borrarlo: ningun token suyo vale mas. */
export function anotarBorrado(id: string): void {
  validoDesde.set(id, Number.POSITIVE_INFINITY);
}

/**
 * Momento a guardar en tokensValidAfter para revocar las sesiones de ahora.
 *
 * Redondeado AL SEGUNDO hacia abajo, y no es un detalle: el `iat` del JWT
 * tiene resolucion de segundos. Con el instante exacto, el token nuevo que
 * se emite en el mismo segundo del cambio (el del propio usuario que acaba
 * de cambiar su clave) tendria un iat MENOR y quedaria revocado al nacer.
 */
export function ahoraParaRevocar(): Date {
  return new Date(Math.floor(Date.now() / 1000) * 1000);
}

export function sesionVigente(payload: Payload): boolean {
  if (!cargada) return true;
  const desde = validoDesde.get(payload.sub);
  if (desde === undefined) return true;
  if (desde === Number.POSITIVE_INFINITY) return false;
  return (payload.iat ?? 0) * 1000 >= desde;
}

// Solo para los tests.
export function _reiniciarSesiones(): void {
  validoDesde.clear();
  cargada = false;
}
