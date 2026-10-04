import { Suspense, lazy, useEffect, useState } from 'react';
import { BrowserRouter, Link, NavLink, Route, Routes, useNavigate } from 'react-router-dom';
import { authFetch, clearToken, getToken, onSesionPerdida } from './lib/auth.js';
import { useLive } from './lib/useLive.js';
import type { Perfil } from './lib/types.js';
import { SesionContext } from './lib/sesion.js';
import { EstadoPlaca } from './components/EstadoPlaca.js';
import { Historial } from './pages/Historial.js';
import { Live } from './pages/Live.js';
import { Login } from './pages/Login.js';
import { Restablecer } from './pages/Restablecer.js';

// El detalle carga aparte: los graficos (recharts) son mas de la mitad del
// bundle y solo se usan ahi y en el grafico de En curso, que tambien los pide
// aparte. La pantalla En curso, que es la que se abre desde el celular en
// medio del campo, muestra sus numeros sin esperarlos.
const DetalleCorrida = lazy(() =>
  import('./pages/Detalle.js').then((m) => ({ default: m.DetalleCorrida })));
// Configuracion de la placa (tarjeta 24): tambien aparte, se usa poco.
const Configuracion = lazy(() =>
  import('./pages/Configuracion.js').then((m) => ({ default: m.Configuracion })));
const Usuarios = lazy(() => import('./pages/Usuarios.js').then((m) => ({ default: m.Usuarios })));
const Cuenta = lazy(() => import('./pages/Cuenta.js').then((m) => ({ default: m.Cuenta })));

export function App() {
  const [sesion, setSesion] = useState<Perfil | null>(null);
  // "comprobando" evita el parpadeo del login en cada recarga: con un token
  // guardado, mostrar la pantalla de ingreso mientras se valida daria la
  // impresion de que la sesion se perdio.
  const [comprobando, setComprobando] = useState(true);
  // El enlace de recuperacion / invitacion se abre sin sesion, antes del login.
  const [tokenClave, setTokenClave] = useState<string | null>(() =>
    window.location.pathname === '/restablecer'
      ? new URLSearchParams(window.location.search).get('token')
      : null);

  useEffect(() => {
    onSesionPerdida(() => setSesion(null));

    if (!getToken()) { setComprobando(false); return; }

    // El token guardado se valida contra el servidor al arrancar, en vez de
    // descubrir que vencio al primer pedido de datos.
    authFetch('/api/auth/me')
      .then((r) => (r.ok ? r.json() : null))
      .then((data) => { if (data?.user) setSesion(data.user); })
      .catch(() => { /* sin servidor se entra al login */ })
      .finally(() => setComprobando(false));
  }, []);

  if (tokenClave !== null) {
    return (
      <Restablecer token={tokenClave} onListo={() => {
        window.history.replaceState(null, '', '/');
        clearToken();
        setSesion(null);
        setTokenClave(null);
      }} />
    );
  }
  if (comprobando) return <main className="login"><p className="empty">Cargando...</p></main>;
  if (!sesion) return <Login onEntrar={setSesion} />;

  return (
    <SesionContext.Provider value={sesion}>
      <BrowserRouter>
        <Contenido sesion={sesion} onSalir={() => { clearToken(); setSesion(null); }} />
      </BrowserRouter>
    </SesionContext.Provider>
  );
}

function Contenido({ sesion, onSalir }: { sesion: Perfil; onSalir: () => void }) {
  // El socket vive en el nivel de la app, no de la pantalla En curso: asi
  // navegar al historial y volver no reconecta ni vuelve a pedir el snapshot.
  const estado = useLive();
  const navigate = useNavigate();

  return (
    <main>
      <header className="cabecera">
        <div>
          <Link to="/" className="marca"><h1>BioSoft</h1></Link>
          <p className="subtitle">Monitor remoto{estado.deviceId ? ` · ${estado.deviceId}` : ''}</p>
        </div>
        <div className="cabecera-derecha">
          <span className={`enlace ${estado.conectado && estado.brokerOk ? 'enlace-ok' : 'enlace-caido'}`}>
            {estado.conectado ? (estado.brokerOk ? 'En linea' : 'Sin broker') : 'Sin servidor'}
          </span>
          {estado.conectado && <EstadoPlaca placa={estado.placa} brokerOk={estado.brokerOk} />}
          <NavLink to="/cuenta" className="usuario-actual" title="Mi cuenta">
            {sesion.username}{sesion.role === 'admin' ? ' · admin' : ''}
          </NavLink>
          <button type="button" className="salir" onClick={onSalir}>Salir</button>
        </div>
      </header>

      <nav className="navegacion">
        <NavLink to="/" end>En curso</NavLink>
        <NavLink to="/historial">Historial</NavLink>
        <NavLink to="/configuracion">Configuracion</NavLink>
        {sesion.role === 'admin' && <NavLink to="/usuarios">Usuarios</NavLink>}
      </nav>

      <Suspense fallback={<p className="empty panel">Cargando...</p>}>
        <Routes>
          <Route path="/" element={<Live estado={estado} onVerCorrida={(id) => navigate(`/corrida/${id}`)} />} />
          <Route path="/historial" element={<Historial />} />
          <Route path="/corrida/:id" element={<DetalleCorrida />} />
          <Route path="/configuracion" element={<Configuracion />} />
          <Route path="/usuarios" element={sesion.role === 'admin'
            ? <Usuarios />
            : <p className="empty panel">Solo los administradores gestionan usuarios.</p>} />
          <Route path="/cuenta" element={<Cuenta />} />
          <Route path="*" element={<p className="empty panel">Esa pagina no existe. <Link to="/">Ir al inicio</Link></p>} />
        </Routes>
      </Suspense>
    </main>
  );
}
