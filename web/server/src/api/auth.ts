import { Router } from 'express';
import { asincrono } from './asincrono.js';
import { requireAuth } from '../auth/middleware.js';
import { correoDisponible, enviarCorreoDeClave } from '../auth/correo.js';
import { hashPassword, verifyPassword } from '../auth/password.js';
import {
  ClaveInvalida, HORAS_RECUPERACION, crearTokenDeClave, fijarClave, usarTokenDeClave, validarClave,
} from '../auth/recuperacion.js';
import { signToken } from '../auth/tokens.js';
import { User } from '../models/user.js';

export const authRouter = Router();

// Limite de intentos por usuario+IP. No pretende ser un WAF: alcanza para que
// probar contraseñas al voleo contra este servidor no sea practico, que es
// todo lo que hace falta en un monitor con dos o tres cuentas.
const VENTANA_MS = 15 * 60 * 1000;
const MAX_INTENTOS = 8;
const intentos = new Map<string, { cuenta: number; desde: number }>();

function limitado(clave: string): boolean {
  const registro = intentos.get(clave);
  if (!registro) return false;
  if (Date.now() - registro.desde > VENTANA_MS) { intentos.delete(clave); return false; }
  return registro.cuenta >= MAX_INTENTOS;
}

function anotarFallo(clave: string): void {
  const registro = intentos.get(clave);
  if (!registro || Date.now() - registro.desde > VENTANA_MS) {
    intentos.set(clave, { cuenta: 1, desde: Date.now() });
    return;
  }
  registro.cuenta++;
}

// Un hash descartable para el caso "usuario inexistente". Sin esto, el login
// de un usuario que no existe responde mucho mas rapido que el de uno que si
// -- y esa diferencia de tiempo permite averiguar que nombres de usuario son
// validos sin acertar una sola contraseña.
const HASH_SEÑUELO = await hashPassword('usuario-inexistente-' + Math.random());

authRouter.post('/auth/login', asincrono(async (req, res) => {
  const username = typeof req.body?.username === 'string' ? req.body.username.trim().toLowerCase() : '';
  const password = typeof req.body?.password === 'string' ? req.body.password : '';

  if (!username || !password) {
    res.status(400).json({ error: 'faltan usuario o contraseña' });
    return;
  }

  const clave = `${req.ip}|${username}`;
  if (limitado(clave)) {
    res.status(429).json({ error: 'demasiados intentos, espera unos minutos' });
    return;
  }

  const user = await User.findOne({ username });
  const ok = user
    ? await verifyPassword(password, user.passwordHash)
    : (await verifyPassword(password, HASH_SEÑUELO), false);

  if (!ok || !user) {
    anotarFallo(clave);
    // El mismo mensaje para usuario inexistente y contraseña incorrecta: decir
    // cual de las dos fallo confirma la mitad de la credencial.
    res.status(401).json({ error: 'usuario o contraseña incorrectos' });
    return;
  }

  intentos.delete(clave);
  user.lastLoginAt = new Date();
  await user.save();

  res.json({
    token: signToken({ sub: user._id.toString(), username: user.username }),
    user: perfil(user),
  });
}));

function perfil(user: { username: string; role?: string | null; email?: string | null }) {
  return { username: user.username, role: user.role ?? 'viewer', email: user.email ?? null };
}

// Sirve para que el cliente valide al arrancar el token que tiene guardado, en
// vez de descubrir que vencio al primer pedido de datos. El rol sale de la
// base, no del token: es lo que decide que pestañas y botones se muestran.
authRouter.get('/auth/me', requireAuth, asincrono(async (req, res) => {
  const user = await User.findById(req.user?.sub).lean();
  if (!user) { res.status(401).json({ error: 'no autorizado' }); return; }
  res.json({ user: perfil(user), correo: correoDisponible() });
}));

/**
 * Cambio de la propia contraseña. Pide la actual: un token robado (o una
 * sesion abierta en una compu ajena) no alcanza para quedarse con la cuenta.
 * Revoca las demas sesiones y devuelve un token nuevo para esta.
 */
authRouter.post('/auth/password', requireAuth, asincrono(async (req, res) => {
  const actual = typeof req.body?.current === 'string' ? req.body.current : '';
  const user = await User.findById(req.user?.sub);
  if (!user) { res.status(401).json({ error: 'no autorizado' }); return; }

  const clave = `clave|${user._id.toString()}`;
  if (limitado(clave)) { res.status(429).json({ error: 'demasiados intentos, espera unos minutos' }); return; }
  if (!(await verifyPassword(actual, user.passwordHash))) {
    anotarFallo(clave);
    res.status(400).json({ error: 'la contraseña actual no es correcta' });
    return;
  }
  try {
    await fijarClave(user._id.toString(), validarClave(req.body?.password));
  } catch (error) {
    if (error instanceof ClaveInvalida) { res.status(400).json({ error: error.message }); return; }
    throw error;
  }
  intentos.delete(clave);
  res.json({ token: signToken({ sub: user._id.toString(), username: user.username }) });
}));

/**
 * "Olvide mi contraseña". Responde SIEMPRE lo mismo, exista o no la cuenta y
 * tenga o no correo: cualquier diferencia le diria a quien prueba que nombres
 * de usuario o correos estan registrados.
 */
authRouter.post('/auth/forgot', asincrono(async (req, res) => {
  const quien = typeof req.body?.username === 'string' ? req.body.username.trim().toLowerCase() : '';
  const respuesta = {
    correo: correoDisponible(),
    mensaje: correoDisponible()
      ? 'Si la cuenta existe y tiene un correo cargado, te llega un enlace para fijar una contraseña nueva. ' +
        'Si no te llega, pedile a un administrador del monitor que te genere uno.'
      : 'Este monitor no tiene configurado el envio de correos: pedile a un administrador que te genere ' +
        'un enlace para fijar una contraseña nueva desde la pagina Usuarios.',
  };
  if (!quien) { res.status(400).json({ error: 'falta el usuario o el correo' }); return; }

  const clave = `olvido|${req.ip}`;
  if (limitado(clave)) { res.status(429).json({ error: 'demasiados pedidos, espera unos minutos' }); return; }
  anotarFallo(clave);   // cuenta todos los pedidos, no solo los fallidos

  if (correoDisponible()) {
    const user = await User.findOne({ $or: [{ username: quien }, { email: quien }] }).lean();
    if (user?.email) {
      const { token } = await crearTokenDeClave(user._id, HORAS_RECUPERACION);
      // El correo sale en segundo plano: esperar al SMTP haria que la
      // respuesta tarde mas cuando la cuenta existe, y eso tambien delata.
      enviarCorreoDeClave(user.email, user.username, token, HORAS_RECUPERACION)
        .catch((error) => console.error('[auth] no se pudo mandar el correo de recuperacion:', error));
    }
  }
  res.json(respuesta);
}));

/** Fija la contraseña con un enlace de recuperacion o de invitacion. */
authRouter.post('/auth/reset', asincrono(async (req, res) => {
  const token = typeof req.body?.token === 'string' ? req.body.token : '';
  const clave = `reset|${req.ip}`;
  if (limitado(clave)) { res.status(429).json({ error: 'demasiados intentos, espera unos minutos' }); return; }

  let password: string;
  try {
    password = validarClave(req.body?.password);
  } catch (error) {
    if (error instanceof ClaveInvalida) { res.status(400).json({ error: error.message }); return; }
    throw error;
  }
  const username = await usarTokenDeClave(token, password);
  if (!username) {
    anotarFallo(clave);
    res.status(400).json({ error: 'el enlace no es valido o ya vencio: pedi uno nuevo' });
    return;
  }
  res.json({ ok: true, username });
}));
