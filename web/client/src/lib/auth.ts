import type { Perfil } from './types.js';

// Sesion del navegador.
//
// El token se guarda en localStorage para que abrir el monitor desde el
// celular no pida la contraseña cada vez -- que en la practica llevaria a
// elegir una contraseña corta. La contracara conocida es que un XSS podria
// leerlo; se acepta porque lo que protege es un tablero de SOLO LECTURA (la
// placa publica y nadie le manda comandos desde aca), asi que el peor caso es
// que alguien vea mediciones, no que encienda una bobina.

const CLAVE = 'biosoft.token';

let token: string | null = leerGuardado();
let alPerderSesion: (() => void) | null = null;

function leerGuardado(): string | null {
  try {
    return localStorage.getItem(CLAVE);
  } catch {
    // Modo privado o almacenamiento bloqueado: se sigue sin persistir.
    return null;
  }
}

export function getToken(): string | null {
  return token;
}

export function setToken(nuevo: string): void {
  token = nuevo;
  try { localStorage.setItem(CLAVE, nuevo); } catch { /* sesion solo en memoria */ }
}

export function clearToken(): void {
  token = null;
  try { localStorage.removeItem(CLAVE); } catch { /* nada que limpiar */ }
}

export function onSesionPerdida(callback: () => void): void {
  alPerderSesion = callback;
}

/**
 * fetch con el token puesto. Un 401 limpia la sesion y avisa, en vez de
 * dejar la pantalla cargando para siempre: un token vencido es el caso
 * normal despues de una semana, no un error raro.
 */
export async function authFetch(url: string, init: RequestInit = {}): Promise<Response> {
  const headers = new Headers(init.headers);
  if (token) headers.set('authorization', `Bearer ${token}`);

  const respuesta = await fetch(url, { ...init, headers });
  if (respuesta.status === 401) {
    clearToken();
    alPerderSesion?.();
  }
  return respuesta;
}

export async function login(username: string, password: string): Promise<Perfil> {
  const respuesta = await fetch('/api/auth/login', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });

  if (!respuesta.ok) {
    const data = await respuesta.json().catch(() => ({}));
    throw new Error(data.error ?? 'no se pudo iniciar sesion');
  }

  const data = await respuesta.json();
  setToken(data.token);
  return data.user;
}

async function postJson(url: string, cuerpo: unknown, conToken = false): Promise<Record<string, unknown>> {
  const init: RequestInit = {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(cuerpo),
  };
  const respuesta = conToken ? await authFetch(url, init) : await fetch(url, init);
  const data = await respuesta.json().catch(() => ({}));
  if (!respuesta.ok) throw new Error(data.error ?? `error ${respuesta.status}`);
  return data;
}

/** "Olvide mi contraseña". Devuelve el mensaje a mostrar (no dice si la cuenta existe). */
export async function pedirRecuperacion(usuario: string): Promise<string> {
  const data = await postJson('/api/auth/forgot', { username: usuario });
  return String(data.mensaje ?? '');
}

/** Fija la contraseña con un enlace de recuperacion o invitacion. */
export async function restablecerClave(token: string, password: string): Promise<string> {
  const data = await postJson('/api/auth/reset', { token, password });
  return String(data.username ?? '');
}

/**
 * Cambio de la propia contraseña. El servidor revoca las demas sesiones y
 * devuelve un token nuevo para esta, que se guarda en lugar del anterior.
 */
export async function cambiarClave(actual: string, nueva: string): Promise<void> {
  const data = await postJson('/api/auth/password', { current: actual, password: nueva }, true);
  if (typeof data.token === 'string') setToken(data.token);
}

/** El enlace que se le pasa a alguien para que fije su contraseña. */
export function enlaceDeClave(token: string): string {
  return `${window.location.origin}/restablecer?token=${encodeURIComponent(token)}`;
}
