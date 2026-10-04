import { randomBytes } from 'node:crypto';
import { Router } from 'express';
import mongoose from 'mongoose';
import { asincrono } from './asincrono.js';
import { correoDisponible, enviarCorreoDeClave } from '../auth/correo.js';
import { hashPassword } from '../auth/password.js';
import {
  ClaveInvalida, HORAS_INVITACION, HORAS_RECUPERACION, crearTokenDeClave, fijarClave, validarClave,
} from '../auth/recuperacion.js';
import { anotarBorrado, anotarUsuario } from '../auth/sesiones.js';
import { ROLES, User, type Rol } from '../models/user.js';

// Gestion de usuarios. Todo detras de requireAuth + requireAdmin (index.ts).
//
// Dos invariantes que la API sostiene, y no solo la pantalla:
//  - Siempre queda AL MENOS UN administrador. Sin ninguno, la unica forma de
//    volver a gestionar cuentas es la consola del servidor.
//  - Un administrador no se borra ni se quita el rol a si mismo: es la forma
//    mas facil de dejar el monitor sin administradores por un click.
//
// Al crear una cuenta, la contraseña es opcional. Sin ella se genera un
// ENLACE DE INVITACION (el mismo mecanismo que la recuperacion, con 72 h de
// vigencia) para que la persona elija la suya: asi el administrador nunca
// conoce la contraseña de nadie. El enlace se devuelve como token; el cliente
// arma la URL con su propio origen.
export const usuariosRouter = Router();

const PATRON_USUARIO = /^[a-z0-9._-]{3,32}$/;
const PATRON_CORREO = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

class Invalido extends Error {}

function leerUsuario(valor: unknown): string {
  const username = typeof valor === 'string' ? valor.trim().toLowerCase() : '';
  if (!PATRON_USUARIO.test(username)) {
    throw new Invalido('usuario: de 3 a 32 caracteres, solo letras, numeros, punto, guion y guion bajo');
  }
  return username;
}

function leerCorreo(valor: unknown): string | null {
  if (valor === undefined || valor === null || valor === '') return null;
  const email = typeof valor === 'string' ? valor.trim().toLowerCase() : '';
  if (!PATRON_CORREO.test(email)) throw new Invalido('el correo no tiene un formato valido');
  return email;
}

function leerRol(valor: unknown): Rol {
  if (!ROLES.includes(valor as Rol)) throw new Invalido('rol invalido (admin o viewer)');
  return valor as Rol;
}

function fila(user: {
  _id: unknown; username: string; email?: string | null; role?: string | null;
  createdAt?: Date | null; lastLoginAt?: Date | null; resetTokenExpiresAt?: Date | null;
}) {
  return {
    id: String(user._id),
    username: user.username,
    email: user.email ?? null,
    role: user.role ?? 'viewer',
    createdAt: user.createdAt ?? null,
    lastLoginAt: user.lastLoginAt ?? null,
    // Hay un enlace sin usar y vigente: la cuenta todavia espera que alguien
    // fije su contraseña (invitacion) o la recupere.
    enlacePendienteHasta: user.resetTokenExpiresAt && user.resetTokenExpiresAt > new Date()
      ? user.resetTokenExpiresAt : null,
  };
}

function manejar(error: unknown, res: import('express').Response): boolean {
  if (error instanceof Invalido || error instanceof ClaveInvalida) {
    res.status(400).json({ error: error.message });
    return true;
  }
  if ((error as { code?: number })?.code === 11000) {
    res.status(409).json({ error: 'ya existe un usuario con ese nombre' });
    return true;
  }
  return false;
}

async function otrosAdmins(id: string): Promise<number> {
  return User.countDocuments({ role: 'admin', _id: { $ne: id } });
}

usuariosRouter.get('/', asincrono(async (_req, res) => {
  const usuarios = await User.find().sort({ username: 1 }).lean();
  res.json({ items: usuarios.map(fila), correo: correoDisponible() });
}));

usuariosRouter.post('/', asincrono(async (req, res) => {
  try {
    const username = leerUsuario(req.body?.username);
    const email = leerCorreo(req.body?.email);
    const role = leerRol(req.body?.role ?? 'viewer');
    const conClave = typeof req.body?.password === 'string' && req.body.password !== '';
    // Sin contraseña, una al azar que nadie conoce: la cuenta no se puede usar
    // hasta que se fije una con el enlace de invitacion.
    const password = conClave ? validarClave(req.body.password) : randomBytes(32).toString('hex');

    const user = await User.create({ username, email, role, passwordHash: await hashPassword(password) });
    anotarUsuario(user._id.toString(), null);

    let invitacion: { token: string; expiresAt: Date; enviada: boolean } | null = null;
    if (!conClave) {
      const { token, expiresAt } = await crearTokenDeClave(user._id, HORAS_INVITACION);
      const enviada = Boolean(email && correoDisponible());
      if (enviada) {
        enviarCorreoDeClave(email!, username, token, HORAS_INVITACION)
          .catch((error) => console.error('[usuarios] no se pudo mandar la invitacion:', error));
      }
      invitacion = { token, expiresAt, enviada };
    }
    console.log(`[usuarios] ${req.user?.username} creo "${username}" (${role})`);
    res.status(201).json({ user: fila(await User.findById(user._id).lean() ?? user), invitacion });
  } catch (error) {
    if (!manejar(error, res)) throw error;
  }
}));

usuariosRouter.patch('/:id', asincrono(async (req, res) => {
  const { id } = req.params;
  if (!mongoose.isValidObjectId(id)) { res.status(400).json({ error: 'id invalido' }); return; }
  const user = await User.findById(id);
  if (!user) { res.status(404).json({ error: 'usuario no encontrado' }); return; }

  try {
    if (req.body?.email !== undefined) user.email = leerCorreo(req.body.email);
    if (req.body?.role !== undefined) {
      const role = leerRol(req.body.role);
      if (role !== 'admin' && user.role === 'admin') {
        if (id === req.user?.sub) {
          res.status(409).json({ error: 'no podes quitarte el rol de administrador a vos mismo' });
          return;
        }
        if (await otrosAdmins(id) === 0) {
          res.status(409).json({ error: 'tiene que quedar al menos un administrador' });
          return;
        }
      }
      user.role = role;
    }
    if (typeof req.body?.password === 'string' && req.body.password !== '') {
      // Antes del save de los otros campos no importa el orden: fijarClave
      // escribe solo hash, revocacion y enlace.
      await fijarClave(id, validarClave(req.body.password));
    }
    await user.save();
    console.log(`[usuarios] ${req.user?.username} modifico "${user.username}"`);
    res.json({ user: fila((await User.findById(id).lean())!) });
  } catch (error) {
    if (!manejar(error, res)) throw error;
  }
}));

/** Enlace para que el usuario fije una contraseña nueva (2 h). */
usuariosRouter.post('/:id/reset-link', asincrono(async (req, res) => {
  const { id } = req.params;
  if (!mongoose.isValidObjectId(id)) { res.status(400).json({ error: 'id invalido' }); return; }
  const user = await User.findById(id).lean();
  if (!user) { res.status(404).json({ error: 'usuario no encontrado' }); return; }

  const { token, expiresAt } = await crearTokenDeClave(user._id, HORAS_RECUPERACION);
  const enviada = Boolean(user.email && correoDisponible() && req.body?.enviar === true);
  if (enviada) {
    enviarCorreoDeClave(user.email!, user.username, token, HORAS_RECUPERACION)
      .catch((error) => console.error('[usuarios] no se pudo mandar el enlace:', error));
  }
  console.log(`[usuarios] ${req.user?.username} genero un enlace de clave para "${user.username}"`);
  res.json({ token, expiresAt, enviada });
}));

usuariosRouter.delete('/:id', asincrono(async (req, res) => {
  const { id } = req.params;
  if (!mongoose.isValidObjectId(id)) { res.status(400).json({ error: 'id invalido' }); return; }
  if (id === req.user?.sub) {
    res.status(409).json({ error: 'no podes borrar tu propia cuenta' });
    return;
  }
  const user = await User.findById(id).lean();
  if (!user) { res.status(404).json({ error: 'usuario no encontrado' }); return; }
  if (user.role === 'admin' && await otrosAdmins(id) === 0) {
    res.status(409).json({ error: 'tiene que quedar al menos un administrador' });
    return;
  }
  await User.deleteOne({ _id: id });
  anotarBorrado(id);
  console.log(`[usuarios] ${req.user?.username} borro "${user.username}"`);
  res.json({ ok: true });
}));
