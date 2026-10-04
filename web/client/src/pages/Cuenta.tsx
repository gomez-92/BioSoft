import { useState, type FormEvent } from 'react';
import { cambiarClave } from '../lib/auth.js';
import { useSesion } from '../lib/sesion.js';

// La cuenta propia: quien soy, que rol tengo y cambiar la contraseña.

export function Cuenta() {
  const sesion = useSesion();
  const [actual, setActual] = useState('');
  const [nueva, setNueva] = useState('');
  const [repetida, setRepetida] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [ok, setOk] = useState(false);
  const [enviando, setEnviando] = useState(false);

  async function enviar(evento: FormEvent) {
    evento.preventDefault();
    setError(null);
    setOk(false);
    if (nueva !== repetida) { setError('las dos contraseñas nuevas no coinciden'); return; }
    setEnviando(true);
    try {
      await cambiarClave(actual, nueva);
      setOk(true);
      setActual(''); setNueva(''); setRepetida('');
    } catch (causa) {
      setError(causa instanceof Error ? causa.message : 'no se pudo cambiar la contraseña');
    } finally {
      setEnviando(false);
    }
  }

  return (
    <div className="columnas-2">
      <section className="panel">
        <h3>Mi cuenta</h3>
        <dl className="datos">
          <dt>Usuario</dt><dd>{sesion?.username}</dd>
          <dt>Rol</dt><dd>{sesion?.role === 'admin' ? 'Administrador' : 'Solo lectura'}</dd>
          <dt>Correo</dt><dd>{sesion?.email ?? 'sin cargar (lo carga un administrador)'}</dd>
        </dl>
        <p className="tenue">
          {sesion?.role === 'admin'
            ? 'Podes enviar configuracion a la placa, borrar corridas y gestionar usuarios.'
            : 'Podes ver todo el monitor. Enviar configuracion, borrar corridas y gestionar usuarios queda para los administradores.'}
        </p>
      </section>

      <form className="panel formulario" onSubmit={enviar}>
        <h3>Cambiar contraseña</h3>
        <label htmlFor="actual">Contraseña actual</label>
        <input id="actual" type="password" autoComplete="current-password"
          value={actual} onChange={(e) => setActual(e.target.value)} required />
        <label htmlFor="nueva">Contraseña nueva</label>
        <input id="nueva" type="password" autoComplete="new-password" minLength={8}
          value={nueva} onChange={(e) => setNueva(e.target.value)} required />
        <label htmlFor="repetida">Repetila</label>
        <input id="repetida" type="password" autoComplete="new-password" minLength={8}
          value={repetida} onChange={(e) => setRepetida(e.target.value)} required />
        <p className="tenue">Se cierran las demas sesiones abiertas con esta cuenta (otros navegadores, el celular).</p>
        {error && <p className="login-error">{error}</p>}
        {ok && <p className="login-aviso" role="status">Contraseña cambiada.</p>}
        <div><button type="submit" className="boton boton-primario" disabled={enviando}>
          {enviando ? 'Guardando...' : 'Cambiar contraseña'}
        </button></div>
      </form>
    </div>
  );
}
