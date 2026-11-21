import { Suspense, lazy, useEffect, useState } from 'react';
import { BrowserRouter, Link, NavLink, Route, Routes, useNavigate } from 'react-router-dom';
import { authFetch, clearToken, getToken, onSesionPerdida } from './lib/auth.js';
import { useLive } from './lib/useLive.js';
import type { Perfil } from './lib/types.js';
import { SesionContext } from './lib/sesion.js';
import { EstadoPlaca } from './components/EstadoPlaca.js';
import { Icono } from './components/Icono.js';
import { useTema, type Preferencia } from './lib/tema.js';
import { Historial } from './pages/Historial.js';
import { Live } from './pages/Live.js';
import { Login } from './pages/Login.js';
import { Restablecer } from './pages/Restablecer.js';
import { BotonArriba } from './components/BotonArriba.js';

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
        <Link to="/" className="marca">
          <span className="marca-logo" aria-hidden="true"><Icono nombre="campo" /></span>
          <span>
            <h1>BioSoft</h1>
            <span className="subtitle">Monitor remoto{estado.deviceId ? ` · ${estado.deviceId}` : ''}</span>
          </span>
        </Link>
        <div className="cabecera-derecha">
          {/* Cada estado dice DE QUIEN es: "Monitor" es este servidor
              escuchando al broker; "Placa" es la ESP32 contestando el ping. */}
          <div className="conexiones">
            <span className="conexion" title={
              !estado.conectado ? 'Este navegador perdio la conexion con el servidor del monitor.'
                : estado.brokerOk ? 'El servidor del monitor esta conectado al broker y recibe la telemetria.'
                  : 'El servidor del monitor no llega al broker: no entra telemetria.'}>
              <span className={`punto ${estado.conectado && estado.brokerOk ? 'punto-ok' : 'punto-caido'}`} />
              <b>Monitor</b>{estado.conectado ? (estado.brokerOk ? 'en linea' : 'sin broker') : 'sin servidor'}
            </span>
            {estado.conectado && <EstadoPlaca placa={estado.placa} brokerOk={estado.brokerOk} />}
          </div>
          <SelectorTema />
          <NavLink to="/cuenta" className="usuario-actual" title="Mi cuenta">
            {sesion.username}{sesion.role === 'admin' ? ' · admin' : ''}
          </NavLink>
          <button type="button" className="salir" onClick={onSalir}>Salir</button>
        </div>
      </header>

      <nav className="navegacion">
        <NavLink to="/" end><Icono nombre="vivo" />En curso</NavLink>
        <NavLink to="/historial"><Icono nombre="historial" />Historial</NavLink>
        {/* Configuracion muestra redes WiFi y broker de la placa: solo admin. */}
        {sesion.role === 'admin' && <NavLink to="/configuracion"><Icono nombre="config" />Configuracion</NavLink>}
        {sesion.role === 'admin' && <NavLink to="/usuarios"><Icono nombre="usuarios" />Usuarios</NavLink>}
      </nav>

      <BotonArriba />
      <Suspense fallback={<p className="empty panel">Cargando...</p>}>
        <Routes>
          <Route path="/" element={<Live estado={estado} onVerCorrida={(id) => navigate(`/corrida/${id}`)} />} />
          <Route path="/historial" element={<Historial />} />
          <Route path="/corrida/:id" element={<DetalleCorrida />} />
          <Route path="/configuracion" element={sesion.role === 'admin'
            ? <Configuracion placa={estado.placa} />
            : <p className="empty panel">Solo los administradores ven y cambian la configuracion de la placa.</p>} />
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

// Automatico / claro / oscuro. Es una preferencia de este navegador.
const OPCIONES_TEMA: Array<{ valor: Preferencia; icono: string; nombre: string }> = [
  { valor: 'auto', icono: 'auto', nombre: 'Automatico (como el sistema)' },
  { valor: 'claro', icono: 'sol', nombre: 'Claro' },
  { valor: 'oscuro', icono: 'luna', nombre: 'Oscuro' },
];

function SelectorTema() {
  const { preferencia, elegir } = useTema();
  return (
    <div className="selector-tema" role="group" aria-label="Tema">
      {OPCIONES_TEMA.map((o) => (
        <button key={o.valor} type="button" title={`Tema: ${o.nombre}`} aria-label={`Tema ${o.nombre}`}
          aria-pressed={preferencia === o.valor} onClick={() => elegir(o.valor)}>
          <Icono nombre={o.icono} tamaño={14} />
        </button>
      ))}
    </div>
  );
}
