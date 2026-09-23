import { useState, type FormEvent } from 'react';
import { login } from '../lib/auth.js';

export function Login({ onEntrar }: { onEntrar: (usuario: string) => void }) {
  const [usuario, setUsuario] = useState('');
  const [clave, setClave] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [enviando, setEnviando] = useState(false);

  async function enviar(evento: FormEvent) {
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

  return (
    <main className="login">
      <form className="panel login-caja" onSubmit={enviar}>
        <h1>BioSoft</h1>
        <p className="subtitle">Monitor remoto</p>

        <label htmlFor="usuario">Usuario</label>
        <input id="usuario" name="username" autoComplete="username" autoCapitalize="none"
          value={usuario} onChange={(e) => setUsuario(e.target.value)} required autoFocus />

        <label htmlFor="clave">Contraseña</label>
        <input id="clave" name="password" type="password" autoComplete="current-password"
          value={clave} onChange={(e) => setClave(e.target.value)} required />

        {error && <p className="login-error">{error}</p>}

        <button type="submit" className="boton boton-primario" disabled={enviando}>
          {enviando ? 'Entrando...' : 'Entrar'}
        </button>
      </form>
    </main>
  );
}
