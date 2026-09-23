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

export async function login(username: string, password: string): Promise<string> {
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
  return data.user.username;
}
