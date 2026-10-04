import { useState, type FormEvent } from 'react';
import { restablecerClave } from '../lib/auth.js';

// Fijar contraseña desde un enlace: el de "olvide mi contraseña" o la
// invitacion de una cuenta nueva. Se abre SIN sesion (es la forma de entrar
// de quien no puede), asi que App la muestra antes del login.

export function Restablecer({ token, onListo }: { token: string; onListo: () => void }) {
  const [clave, setClave] = useState('');
  const [repetida, setRepetida] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [listo, setListo] = useState<string | null>(null);
  const [enviando, setEnviando] = useState(false);

  async function enviar(evento: FormEvent) {
    evento.preventDefault();
    setError(null);
    if (clave !== repetida) { setError('las dos contraseñas no coinciden'); return; }
    setEnviando(true);
    try {
      setListo(await restablecerClave(token, clave));
    } catch (causa) {
      setError(causa instanceof Error ? causa.message : 'no se pudo fijar la contraseña');
    } finally {
      setEnviando(false);
    }
  }

  return (
    <main className="login">
      <form className="panel login-caja" onSubmit={enviar}>
        <h1>BioSoft</h1>
        <p className="subtitle">Fijar contraseña</p>

        {listo !== null ? (
          <>
            <p className="login-aviso" role="status">
              Listo{listo ? `, ${listo}` : ''}: la contraseña quedo guardada. Las sesiones que hubiera
              abiertas con la anterior se cerraron.
            </p>
            <button type="button" className="boton boton-primario" onClick={onListo}>Ir a ingresar</button>
          </>
        ) : (
          <>
            <label htmlFor="nueva">Contraseña nueva</label>
            <input id="nueva" type="password" autoComplete="new-password" minLength={8}
              value={clave} onChange={(e) => setClave(e.target.value)} required autoFocus />
            <label htmlFor="repetida">Repetila</label>
            <input id="repetida" type="password" autoComplete="new-password" minLength={8}
              value={repetida} onChange={(e) => setRepetida(e.target.value)} required />
            <p className="tenue login-nota">Al menos 8 caracteres. El enlace sirve una sola vez.</p>
            {error && <p className="login-error">{error}</p>}
            <button type="submit" className="boton boton-primario" disabled={enviando}>
              {enviando ? 'Guardando...' : 'Guardar contraseña'}
            </button>
            <button type="button" className="enlace-texto" onClick={onListo}>Volver a ingresar</button>
          </>
        )}
      </form>
    </main>
  );
}
