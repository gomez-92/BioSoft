import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';
import { hashPassword, verifyPassword } from '../src/auth/password.js';
import { signToken, tokenDeHeader, verifyToken } from '../src/auth/tokens.js';

// Autenticacion. Una falla aca no se ve: el sistema sigue andando igual, solo
// que dejando entrar a quien no deberia.

const { User } = await import('../src/models/user.js');
const hayMongo = await connectTestDb('auth');

let baseUrl = '';
let server: import('node:http').Server | undefined;

beforeAll(async () => {
  if (!hayMongo) return;
  const express = (await import('express')).default;
  const { authRouter } = await import('../src/api/auth.js');
  const { requireAuth } = await import('../src/auth/middleware.js');

  const app = express();
  app.use(express.json());
  app.get('/api/ping', (_req, res) => { res.json({ ok: true }); });
  app.use('/api', authRouter);
  app.get('/api/secreto', requireAuth, (_req, res) => { res.json({ dato: 'sensible' }); });

  server = app.listen(0);
  const address = server.address();
  baseUrl = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;

  await User.deleteMany({});
  await User.create({ username: 'mario', passwordHash: await hashPassword('clave-de-prueba') });
}, 30000);

afterAll(async () => {
  server?.close();
  await disconnectTestDb(hayMongo);
});

const login = (username: string, password: string) =>
  fetch(`${baseUrl}/api/auth/login`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });

describe('hash de contraseñas', () => {
  it('verifica la correcta y rechaza la incorrecta', async () => {
    const hash = await hashPassword('una-clave-larga');
    expect(await verifyPassword('una-clave-larga', hash)).toBe(true);
    expect(await verifyPassword('otra-clave-larga', hash)).toBe(false);
  });

  // Dos usuarios con la misma contraseña no pueden tener el mismo hash: si lo
  // tuvieran, quien vea la base sabe que comparten clave sin romper nada.
  it('usa un salt distinto cada vez', async () => {
    const a = await hashPassword('misma-clave');
    const b = await hashPassword('misma-clave');
    expect(a).not.toBe(b);
    expect(await verifyPassword('misma-clave', a)).toBe(true);
    expect(await verifyPassword('misma-clave', b)).toBe(true);
  });

  it('no se cae con un hash corrupto: devuelve false', async () => {
    for (const roto of ['', 'cualquier-cosa', 'scrypt$16384$xx', 'md5$1$aa$bb', 'scrypt$1$aa$bb']) {
      expect(await verifyPassword('clave', roto)).toBe(false);
    }
  });
});

describe('tokens', () => {
  it('firma y verifica', () => {
    const token = signToken({ sub: 'abc123', username: 'mario' });
    // `iat` lo agrega la libreria al firmar: es lo que usa la revocacion.
    expect(verifyToken(token)).toEqual({ sub: 'abc123', username: 'mario', iat: expect.any(Number) });
  });

  it('rechaza un token manoseado', () => {
    const token = signToken({ sub: 'abc123', username: 'mario' });
    expect(verifyToken(token.slice(0, -2) + 'xx')).toBeNull();
    expect(verifyToken('no.es.un.token')).toBeNull();
    expect(verifyToken('')).toBeNull();
  });

  // El agujero clasico de JWT: un token que declara alg "none" no puede
  // validarse nunca, aunque su payload sea perfecto.
  it('rechaza un token con algoritmo "none"', () => {
    const cabecera = Buffer.from(JSON.stringify({ alg: 'none', typ: 'JWT' })).toString('base64url');
    const payload = Buffer.from(JSON.stringify({ sub: 'intruso', username: 'intruso' })).toString('base64url');
    expect(verifyToken(`${cabecera}.${payload}.`)).toBeNull();
  });

  it('lee el encabezado Authorization', () => {
    expect(tokenDeHeader('Bearer abc')).toBe('abc');
    expect(tokenDeHeader('bearer abc')).toBe('abc');
    expect(tokenDeHeader('Basic abc')).toBeNull();
    expect(tokenDeHeader('Bearer')).toBeNull();
    expect(tokenDeHeader(undefined)).toBeNull();
  });
});

describe.skipIf(!hayMongo)('login', () => {
  it('devuelve un token con las credenciales correctas', async () => {
    const respuesta = await login('mario', 'clave-de-prueba');
    expect(respuesta.status).toBe(200);
    const data = await respuesta.json();
    expect(data.user.username).toBe('mario');
    expect(verifyToken(data.token)?.username).toBe('mario');
  });

  it('acepta el usuario sin importar mayusculas ni espacios', async () => {
    expect((await login('  MARIO ', 'clave-de-prueba')).status).toBe(200);
  });

  it('rechaza la contraseña incorrecta', async () => {
    expect((await login('mario', 'otra-cosa')).status).toBe(401);
  });

  // El mismo mensaje en los dos casos: decir cual de los dos fallo le confirma
  // a quien prueba la mitad de la credencial.
  it('no distingue usuario inexistente de contraseña incorrecta', async () => {
    const malaClave = await login('mario', 'otra-cosa');
    const noExiste = await login('fantasma', 'otra-cosa');
    expect(noExiste.status).toBe(malaClave.status);
    expect((await noExiste.json()).error).toBe((await malaClave.json()).error);
  });

  it('exige usuario y contraseña', async () => {
    expect((await login('', '')).status).toBe(400);
    expect((await login('mario', '')).status).toBe(400);
  });

  // La respuesta nunca puede incluir el hash, ni siquiera por descuido al
  // serializar el documento entero.
  it('no devuelve el hash en la respuesta', async () => {
    const texto = await (await login('mario', 'clave-de-prueba')).text();
    expect(texto).not.toContain('scrypt$');
    expect(texto).not.toContain('passwordHash');
  });
});

describe.skipIf(!hayMongo)('rutas protegidas', () => {
  it('sin token no se accede', async () => {
    expect((await fetch(`${baseUrl}/api/secreto`)).status).toBe(401);
  });

  it('con un token invalido tampoco', async () => {
    const respuesta = await fetch(`${baseUrl}/api/secreto`, {
      headers: { authorization: 'Bearer no-es-un-token' },
    });
    expect(respuesta.status).toBe(401);
  });

  it('con un token valido si', async () => {
    const { token } = await (await login('mario', 'clave-de-prueba')).json();
    const respuesta = await fetch(`${baseUrl}/api/secreto`, {
      headers: { authorization: `Bearer ${token}` },
    });
    expect(respuesta.status).toBe(200);
    expect((await respuesta.json()).dato).toBe('sensible');
  });

  // El latido queda abierto a proposito, para que un chequeo de
  // disponibilidad no necesite credenciales. No dice nada del experimento.
  it('el ping sigue siendo publico', async () => {
    expect((await fetch(`${baseUrl}/api/ping`)).status).toBe(200);
  });
});
