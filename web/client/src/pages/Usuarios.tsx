import { useCallback, useEffect, useState, type FormEvent } from 'react';
import { authFetch, enlaceDeClave } from '../lib/auth.js';
import { useSesion } from '../lib/sesion.js';
import type { Rol } from '../lib/types.js';

// Gestion de usuarios (solo administradores).
//
// Al crear una cuenta lo normal es NO ponerle contraseña: se genera un enlace
// de invitacion para que la persona elija la suya, y el administrador nunca
// conoce la contraseña de nadie. El enlace se muestra una sola vez, para
// copiarlo y mandarlo por el medio que se use (o sale por correo si el
// servidor tiene SMTP y la cuenta tiene correo).

interface Usuario {
  id: string; username: string; email: string | null; role: Rol;
  createdAt: string | null; lastLoginAt: string | null; enlacePendienteHasta: string | null;
}

interface Enlace { para: string; url: string; vence: string; enviada: boolean }

async function pedir(metodo: string, url: string, cuerpo?: unknown) {
  const respuesta = await authFetch(url, {
    method: metodo,
    headers: { 'content-type': 'application/json' },
    body: cuerpo === undefined ? undefined : JSON.stringify(cuerpo),
  });
  const data = await respuesta.json().catch(() => ({}));
  if (!respuesta.ok) throw new Error(data.error ?? `error ${respuesta.status}`);
  return data;
}

function fecha(iso: string | null): string {
  return iso ? new Date(iso).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'short' }) : '--';
}

export function Usuarios() {
  const yo = useSesion();
  const [usuarios, setUsuarios] = useState<Usuario[]>([]);
  const [correo, setCorreo] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [enlace, setEnlace] = useState<Enlace | null>(null);

  const cargar = useCallback(() => {
    pedir('GET', '/api/users')
      .then((data) => { setUsuarios(data.items); setCorreo(data.correo); })
      .catch((causa) => setError(causa.message));
  }, []);
  useEffect(cargar, [cargar]);

  async function accion(promesa: Promise<unknown>) {
    setError(null);
    try { await promesa; cargar(); } catch (causa) { setError(causa instanceof Error ? causa.message : String(causa)); }
  }

  function mostrarEnlace(para: string, data: { token: string; expiresAt: string; enviada: boolean }) {
    setEnlace({ para, url: enlaceDeClave(data.token), vence: data.expiresAt, enviada: data.enviada });
  }

  return (
    <>
      {error && <p className="aviso panel-aviso">{error}</p>}
      {enlace && <EnlaceGenerado enlace={enlace} onCerrar={() => setEnlace(null)} />}

      <div className="columnas-usuarios">
        <section className="panel">
          <h3>Usuarios ({usuarios.length})</h3>
          <div className="tabla-scroll">
            <table className="tabla tabla-usuarios">
              <thead>
                <tr><th>Usuario</th><th>Correo</th><th>Rol</th><th>Ultimo ingreso</th><th /></tr>
              </thead>
              <tbody>
                {usuarios.map((u) => (
                  <FilaUsuario key={u.id} usuario={u} esYo={u.username === yo?.username}
                    onRol={(role) => accion(pedir('PATCH', `/api/users/${u.id}`, { role }))}
                    onCorreo={(email) => accion(pedir('PATCH', `/api/users/${u.id}`, { email }))}
                    onEnlace={() => accion(pedir('POST', `/api/users/${u.id}/reset-link`, { enviar: correo && !!u.email })
                      .then((data) => mostrarEnlace(u.username, data)))}
                    onBorrar={() => {
                      if (!window.confirm(`¿Borrar la cuenta "${u.username}"? Sus sesiones abiertas se cierran al instante.`)) return;
                      void accion(pedir('DELETE', `/api/users/${u.id}`));
                    }} />
                ))}
              </tbody>
            </table>
          </div>
          <p className="tenue">
            Administrador: envia configuracion a la placa, borra corridas y gestiona usuarios. Solo lectura: ve todo lo
            demas. Siempre queda al menos un administrador.
          </p>
        </section>

        <NuevoUsuario correo={correo} onCreado={(username, invitacion) => {
          cargar();
          if (invitacion) mostrarEnlace(username, invitacion);
        }} />
      </div>
    </>
  );
}

function FilaUsuario({ usuario, esYo, onRol, onCorreo, onEnlace, onBorrar }: {
  usuario: Usuario; esYo: boolean;
  onRol: (rol: Rol) => void; onCorreo: (email: string) => void; onEnlace: () => void; onBorrar: () => void;
}) {
  return (
    <tr>
      <td>
        <strong>{usuario.username}</strong>{esYo && <span className="tenue"> (vos)</span>}
        {usuario.enlacePendienteHasta && (
          <div className="tenue">enlace sin usar hasta {fecha(usuario.enlacePendienteHasta)}</div>
        )}
      </td>
      <td>
        <button type="button" className="enlace-texto" onClick={() => {
          const nuevo = window.prompt(`Correo de ${usuario.username} (vacio para quitarlo):`, usuario.email ?? '');
          if (nuevo !== null) onCorreo(nuevo.trim());
        }}>{usuario.email ?? 'agregar'}</button>
      </td>
      <td>
        <select value={usuario.role} disabled={esYo} onChange={(e) => onRol(e.target.value as Rol)}
          title={esYo ? 'No podes cambiarte el rol a vos mismo' : undefined}>
          <option value="admin">Administrador</option>
          <option value="viewer">Solo lectura</option>
        </select>
      </td>
      <td className="tenue-celda">{fecha(usuario.lastLoginAt)}</td>
      <td className="acciones-fila">
        <button type="button" className="boton boton-chico" onClick={onEnlace}
          title="Genera un enlace de un solo uso (2 h) para que fije una contraseña nueva">
          Enlace de clave
        </button>
        {!esYo && <button type="button" className="boton boton-chico boton-peligro" onClick={onBorrar}>Borrar</button>}
      </td>
    </tr>
  );
}

function NuevoUsuario({ correo, onCreado }: {
  correo: boolean;
  onCreado: (username: string, invitacion: { token: string; expiresAt: string; enviada: boolean } | null) => void;
}) {
  const [username, setUsername] = useState('');
  const [email, setEmail] = useState('');
  const [role, setRole] = useState<Rol>('viewer');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [enviando, setEnviando] = useState(false);

  async function crear(evento: FormEvent) {
    evento.preventDefault();
    setError(null);
    setEnviando(true);
    try {
      const data = await pedir('POST', '/api/users', { username, email, role, password });
      onCreado(data.user.username, data.invitacion);
      setUsername(''); setEmail(''); setPassword(''); setRole('viewer');
    } catch (causa) {
      setError(causa instanceof Error ? causa.message : String(causa));
    } finally {
      setEnviando(false);
    }
  }

  return (
    <form className="panel formulario" onSubmit={crear}>
      <h3>Nuevo usuario</h3>
      <label htmlFor="nu-usuario">Usuario</label>
      <input id="nu-usuario" value={username} onChange={(e) => setUsername(e.target.value)}
        autoCapitalize="none" required pattern="[A-Za-z0-9._\-]{3,32}"
        title="De 3 a 32 caracteres: letras, numeros, punto, guion y guion bajo" />
      <label htmlFor="nu-correo">Correo (opcional)</label>
      <input id="nu-correo" type="email" value={email} onChange={(e) => setEmail(e.target.value)} />
      <label htmlFor="nu-rol">Rol</label>
      <select id="nu-rol" value={role} onChange={(e) => setRole(e.target.value as Rol)}>
        <option value="viewer">Solo lectura</option>
        <option value="admin">Administrador</option>
      </select>
      <label htmlFor="nu-clave">Contraseña (opcional)</label>
      <input id="nu-clave" type="password" autoComplete="new-password" minLength={8}
        value={password} onChange={(e) => setPassword(e.target.value)} />
      <p className="tenue">
        Sin contraseña se genera un enlace de invitacion (72 h) para que la persona elija la suya
        {correo ? ', y si tiene correo se le manda.' : '. Este servidor no tiene correo configurado: el enlace hay que pasarlo a mano.'}
      </p>
      {error && <p className="login-error">{error}</p>}
      <div><button type="submit" className="boton boton-primario" disabled={enviando}>
        {enviando ? 'Creando...' : 'Crear usuario'}
      </button></div>
    </form>
  );
}

function EnlaceGenerado({ enlace, onCerrar }: { enlace: Enlace; onCerrar: () => void }) {
  const [copiado, setCopiado] = useState(false);
  return (
    <section className="panel enlace-generado" role="status">
      <h3>Enlace para {enlace.para}</h3>
      <p>
        {enlace.enviada ? 'Se mando por correo. ' : ''}
        Sirve una sola vez, hasta el {fecha(enlace.vence)}. Se muestra solo ahora: copialo y pasaselo.
      </p>
      <div className="enlace-fila">
        <input readOnly value={enlace.url} onFocus={(e) => e.target.select()} />
        <button type="button" className="boton" onClick={() => {
          navigator.clipboard?.writeText(enlace.url).then(() => setCopiado(true)).catch(() => {});
        }}>{copiado ? 'Copiado' : 'Copiar'}</button>
        <button type="button" className="boton" onClick={onCerrar}>Cerrar</button>
      </div>
    </section>
  );
}
