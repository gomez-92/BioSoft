import { useState, type FormEvent } from 'react';
import { login, pedirRecuperacion } from '../lib/auth.js';
import type { Perfil } from '../lib/types.js';

export function Login({ onEntrar }: { onEntrar: (perfil: Perfil) => void }) {
  const [modo, setModo] = useState<'entrar' | 'olvide'>('entrar');
  const [usuario, setUsuario] = useState('');
  const [clave, setClave] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [aviso, setAviso] = useState<string | null>(null);
  const [enviando, setEnviando] = useState(false);

  async function entrar(evento: FormEvent) {
    evento.preventDefault();
    setEnviando(true);
    setError(null);
    try {
      onEntrar(await login(usuario, clave));
    } catch (causa) {
      setError(causa instanceof Error ? causa.message : 'no se pudo iniciar sesion');
      // La contraseña se limpia, el usuario no: quien se equivoco al tipear la
      // clave no tiene por que escribir las dos cosas de nuevo.
      setClave('');
    } finally {
      setEnviando(false);
    }
  }

  async function olvide(evento: FormEvent) {
    evento.preventDefault();
    setEnviando(true);
    setError(null);
    try {
      setAviso(await pedirRecuperacion(usuario));
    } catch (causa) {
      setError(causa instanceof Error ? causa.message : 'no se pudo pedir la recuperacion');
    } finally {
      setEnviando(false);
    }
  }

  function cambiarModo(nuevo: 'entrar' | 'olvide') {
    setModo(nuevo);
    setError(null);
    setAviso(null);
  }

  return (
    <main className="login">
      <form className="panel login-caja" onSubmit={modo === 'entrar' ? entrar : olvide}>
        <h1>BioSoft</h1>
        <p className="subtitle">{modo === 'entrar' ? 'Monitor remoto' : 'Recuperar contraseña'}</p>

        <label htmlFor="usuario">{modo === 'entrar' ? 'Usuario' : 'Usuario o correo'}</label>
        <input id="usuario" name="username" autoComplete="username" autoCapitalize="none"
          value={usuario} onChange={(e) => setUsuario(e.target.value)} required autoFocus />

        {modo === 'entrar' && (
          <>
            <label htmlFor="clave">Contraseña</label>
            <input id="clave" name="password" type="password" autoComplete="current-password"
              value={clave} onChange={(e) => setClave(e.target.value)} required />
          </>
        )}

        {error && <p className="login-error">{error}</p>}
        {aviso && <p className="login-aviso" role="status">{aviso}</p>}

        {!(modo === 'olvide' && aviso) && (
          <button type="submit" className="boton boton-primario" disabled={enviando}>
            {modo === 'entrar'
              ? (enviando ? 'Entrando...' : 'Entrar')
              : (enviando ? 'Enviando...' : 'Enviar enlace')}
          </button>
        )}

        <button type="button" className="enlace-texto"
          onClick={() => cambiarModo(modo === 'entrar' ? 'olvide' : 'entrar')}>
          {modo === 'entrar' ? '¿Olvidaste tu contraseña?' : 'Volver a ingresar'}
        </button>
      </form>
    </main>
  );
}
