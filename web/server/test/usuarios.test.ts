import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { connectTestDb, disconnectTestDb } from './support/db.js';
import { hashPassword } from '../src/auth/password.js';

// Usuarios, roles, enlaces de clave y revocacion de sesiones. Lo que se pinea
// aca son invariantes que, rotos, no dan ningun error visible: un viewer que
// puede borrar corridas, un usuario borrado que sigue entrando, un monitor que
// se queda sin administradores.

const { User } = await import('../src/models/user.js');
const hayMongo = await connectTestDb('usuarios');

let baseUrl = '';
let server: import('node:http').Server | undefined;

beforeAll(async () => {
  if (!hayMongo) return;
  const express = (await import('express')).default;
  const { authRouter } = await import('../src/api/auth.js');
  const { usuariosRouter } = await import('../src/api/usuarios.js');
  const { requireAdmin, requireAuth } = await import('../src/auth/middleware.js');
  const { cargarSesiones } = await import('../src/auth/sesiones.js');

  await User.deleteMany({});
  // Una cuenta "vieja", sin rol: la migracion de arranque la pasa a admin.
  await User.collection.insertOne({
    username: 'mario', passwordHash: await hashPassword('clave-de-mario'), createdAt: new Date(),
  });
  await cargarSesiones();

  const app = express();
  app.use(express.json());
  app.use('/api', authRouter);
  app.use('/api/users', requireAuth, requireAdmin, usuariosRouter);
  app.get('/api/secreto', requireAuth, (_req, res) => { res.json({ ok: true }); });

  server = app.listen(0);
  const address = server.address();
  baseUrl = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;
}, 30000);

afterAll(async () => {
  server?.close();
  await disconnectTestDb(hayMongo);
});

async function pedir(metodo: string, ruta: string, token?: string, cuerpo?: unknown) {
  const respuesta = await fetch(`${baseUrl}${ruta}`, {
    method: metodo,
    headers: {
      'content-type': 'application/json',
      ...(token ? { authorization: `Bearer ${token}` } : {}),
    },
    body: cuerpo === undefined ? undefined : JSON.stringify(cuerpo),
  });
  return { status: respuesta.status, data: await respuesta.json().catch(() => ({})) };
}

async function entrar(username: string, password: string): Promise<string> {
  const { status, data } = await pedir('POST', '/api/auth/login', undefined, { username, password });
  expect(status).toBe(200);
  return data.token;
}

describe.skipIf(!hayMongo)('roles y gestion de usuarios', () => {
  it('una cuenta anterior a los roles queda como admin', async () => {
    const token = await entrar('mario', 'clave-de-mario');
    const { data } = await pedir('GET', '/api/auth/me', token);
    expect(data.user.role).toBe('admin');
  });

  it('un admin crea un viewer con contraseña, y el viewer no gestiona usuarios', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    const creado = await pedir('POST', '/api/users', admin,
      { username: 'Ana', role: 'viewer', password: 'clave-de-ana' });
    expect(creado.status).toBe(201);
    expect(creado.data.user.username).toBe('ana');
    expect(creado.data.invitacion).toBeNull();
    expect(JSON.stringify(creado.data)).not.toContain('scrypt$');

    const viewer = await entrar('ana', 'clave-de-ana');
    expect((await pedir('GET', '/api/users', viewer)).status).toBe(403);
    expect((await pedir('GET', '/api/secreto', viewer)).status).toBe(200);
  });

  it('rechaza nombres invalidos y duplicados', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    expect((await pedir('POST', '/api/users', admin, { username: 'a b', role: 'viewer' })).status).toBe(400);
    expect((await pedir('POST', '/api/users', admin,
      { username: 'ana', role: 'viewer', password: 'otra-clave-x' })).status).toBe(409);
    expect((await pedir('POST', '/api/users', admin,
      { username: 'corta', role: 'viewer', password: '123' })).status).toBe(400);
  });

  it('sin contraseña genera una invitacion que fija la clave una sola vez', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    const creado = await pedir('POST', '/api/users', admin, { username: 'beto', role: 'viewer' });
    expect(creado.status).toBe(201);
    const { token } = creado.data.invitacion;
    expect(typeof token).toBe('string');
    // En la base no queda el token, solo su hash.
    const enBase = await User.findOne({ username: 'beto' }).lean();
    expect(enBase?.resetTokenHash).not.toBe(token);

    expect((await pedir('POST', '/api/auth/reset', undefined, { token, password: 'clave-de-beto' })).status).toBe(200);
    await entrar('beto', 'clave-de-beto');
    // Un solo uso.
    expect((await pedir('POST', '/api/auth/reset', undefined, { token, password: 'otra-clave-beto' })).status).toBe(400);
  });

  it('tiene que quedar al menos un administrador, y nadie se quita el rol a si mismo', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    const yo = (await User.findOne({ username: 'mario' }).lean())!._id.toString();
    expect((await pedir('PATCH', `/api/users/${yo}`, admin, { role: 'viewer' })).status).toBe(409);
    expect((await pedir('DELETE', `/api/users/${yo}`, admin)).status).toBe(409);
  });

  it('borrar un usuario corta sus sesiones abiertas al instante', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    const viewer = await entrar('ana', 'clave-de-ana');
    const ana = (await User.findOne({ username: 'ana' }).lean())!._id.toString();
    expect((await pedir('DELETE', `/api/users/${ana}`, admin)).status).toBe(200);
    expect((await pedir('GET', '/api/secreto', viewer)).status).toBe(401);
  });

  it('quitarle el rol a alguien surte efecto sin esperar a que venza su sesion', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    await pedir('POST', '/api/users', admin, { username: 'caro', role: 'admin', password: 'clave-de-caro' });
    const caro = await entrar('caro', 'clave-de-caro');
    expect((await pedir('GET', '/api/users', caro)).status).toBe(200);
    const id = (await User.findOne({ username: 'caro' }).lean())!._id.toString();
    expect((await pedir('PATCH', `/api/users/${id}`, admin, { role: 'viewer' })).status).toBe(200);
    expect((await pedir('GET', '/api/users', caro)).status).toBe(403);
  });
});

describe.skipIf(!hayMongo)('contraseñas', () => {
  it('cambiar la propia exige la actual y revoca las otras sesiones', async () => {
    const vieja = await entrar('beto', 'clave-de-beto');
    // El cambio se hace en un segundo posterior al login, como en la vida real.
    await new Promise((r) => setTimeout(r, 1100));
    expect((await pedir('POST', '/api/auth/password', vieja,
      { current: 'equivocada', password: 'clave-nueva-beto' })).status).toBe(400);
    const cambio = await pedir('POST', '/api/auth/password', vieja,
      { current: 'clave-de-beto', password: 'clave-nueva-beto' });
    expect(cambio.status).toBe(200);
    // El token que devuelve el cambio sigue valiendo (emitido en el mismo
    // segundo de la revocacion); el anterior no.
    expect((await pedir('GET', '/api/secreto', cambio.data.token)).status).toBe(200);
    expect((await pedir('GET', '/api/secreto', vieja)).status).toBe(401);
    await entrar('beto', 'clave-nueva-beto');
  });

  it('el enlace de un admin restablece la clave; vencido no sirve', async () => {
    const admin = await entrar('mario', 'clave-de-mario');
    const id = (await User.findOne({ username: 'beto' }).lean())!._id.toString();
    const { data } = await pedir('POST', `/api/users/${id}/reset-link`, admin, {});
    expect(typeof data.token).toBe('string');

    await User.updateOne({ _id: id }, { $set: { resetTokenExpiresAt: new Date(Date.now() - 1000) } });
    expect((await pedir('POST', '/api/auth/reset', undefined,
      { token: data.token, password: 'clave-vencida-x' })).status).toBe(400);
  });

  // Sin SMTP la respuesta lo dice, pero no depende de si la cuenta existe.
  it('"olvide mi contraseña" no delata que cuentas existen', async () => {
    const existe = await pedir('POST', '/api/auth/forgot', undefined, { username: 'mario' });
    const noExiste = await pedir('POST', '/api/auth/forgot', undefined, { username: 'fantasma' });
    expect(existe.status).toBe(200);
    expect(noExiste.data).toEqual(existe.data);
  });
});
