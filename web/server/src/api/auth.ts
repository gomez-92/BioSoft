import { Router } from 'express';
import { asincrono } from './asincrono.js';
import { requireAuth } from '../auth/middleware.js';
import { hashPassword, verifyPassword } from '../auth/password.js';
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
    user: { username: user.username },
  });
}));

// Sirve para que el cliente valide al arrancar el token que tiene guardado, en
// vez de descubrir que vencio al primer pedido de datos.
authRouter.get('/auth/me', requireAuth, (req, res) => {
  res.json({ user: req.user ? { username: req.user.username } : null });
});
