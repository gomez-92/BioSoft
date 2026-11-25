import { Suspense, lazy, useEffect, useState } from 'react';
import { BrowserRouter, Link, NavLink, Route, Routes, useLocation } from 'react-router-dom';
import { authFetch, clearToken, getToken, onSesionPerdida } from './lib/auth.js';
import { useLive } from './lib/useLive.js';
import type { Perfil } from './lib/types.js';
import { SesionContext } from './lib/sesion.js';
import { EstadoPlaca } from './components/EstadoPlaca.js';
import { Icono } from './components/Icono.js';
import { useTema, type Preferencia } from './lib/tema.js';
import { Historial } from './pages/Historial.js';
import { Live } from './pages/Live.js';
import { Inicio } from './pages/Inicio.js';
import { Login } from './pages/Login.js';
import { Restablecer } from './pages/Restablecer.js';
import { BotonArriba } from './components/BotonArriba.js';
import { Acerca } from './pages/Acerca.js';

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
const Bitacora = lazy(() => import('./pages/Bitacora.js').then((m) => ({ default: m.Bitacora })));

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
  if (!sesion) {
    // "Acerca de" se puede leer sin cuenta (enlace en el login). Va por
    // pathname, como /restablecer, porque sin sesion no hay router.
    if (window.location.pathname === '/acerca') return <main className="main-publica"><Acerca publica /></main>;
    return <Login onEntrar={setSesion} />;
  }

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
  const ubicacion = useLocation();
  const esAdmin = sesion.role === 'admin';
  // Menu lateral: fijo en pantallas anchas, cajon que se abre con el boton
  // en el celular. Solo el cajon usa este estado.
  const [menuAbierto, setMenuAbierto] = useState(false);

  // Elegir una seccion cierra el cajon: en el celular tapa la pagina.
  useEffect(() => { setMenuAbierto(false); }, [ubicacion.pathname]);

  // Con el cajon abierto: Escape lo cierra y la pagina de atras no scrollea.
  useEffect(() => {
    if (!menuAbierto) return;
    const alTeclear = (e: KeyboardEvent) => { if (e.key === 'Escape') setMenuAbierto(false); };
    window.addEventListener('keydown', alTeclear);
    document.body.classList.add('sin-scroll');
    return () => {
      window.removeEventListener('keydown', alTeclear);
      document.body.classList.remove('sin-scroll');
    };
  }, [menuAbierto]);

  return (
    <div className={`app${menuAbierto ? ' menu-abierto' : ''}`}>
      <aside className="lateral" id="menu-lateral" aria-label="Menú">
        <div className="lateral-cabeza">
          <Marca deviceId={estado.deviceId} />
          <button type="button" className="boton-icono lateral-cerrar" aria-label="Cerrar el menú"
            onClick={() => setMenuAbierto(false)}>
            <Icono nombre="cerrar" tamaño={20} />
          </button>
        </div>

        <nav className="lateral-nav" aria-label="Secciones">
          <NavLink to="/" end><Icono nombre="inicio" />Inicio</NavLink>
          <p className="lateral-grupo">Experimentos</p>
          {/* En curso existe solo mientras hay una corrida abierta: sin ella
              no hay nada que mirar y la pagina manda a Inicio. */}
          {estado.run && (
            <NavLink to="/en-curso" className="lateral-en-curso">
              <Icono nombre="vivo" />En curso
              <span className={`latido latido-${estado.status?.health ?? 'normal'}`} aria-label="corrida abierta" />
            </NavLink>
          )}
          <NavLink to="/historial"><Icono nombre="historial" />Historial</NavLink>
          {/* Configuracion muestra redes WiFi y broker de la placa, y la
              bitacora es diagnostico del sistema: las dos, solo admin. */}
          {esAdmin && <>
            <p className="lateral-grupo">Placa</p>
            <NavLink to="/configuracion"><Icono nombre="config" />Configuración</NavLink>
            <NavLink to="/bitacora"><Icono nombre="bitacora" />Bitácora</NavLink>
            <p className="lateral-grupo">Administración</p>
            <NavLink to="/usuarios"><Icono nombre="usuarios" />Usuarios</NavLink>
          </>}
          <p className="lateral-grupo">Proyecto</p>
          <NavLink to="/acerca"><Icono nombre="info" />Acerca de</NavLink>
        </nav>

        <div className="lateral-pie">
          <NavLink to="/cuenta" className="lateral-usuario" title="Mi cuenta">
            <Icono nombre="cuenta" />
            <span>
              <span className="lateral-usuario-nombre">{sesion.username}</span>
              <span className="lateral-usuario-rol">{esAdmin ? 'Administrador' : 'Solo lectura'}</span>
            </span>
          </NavLink>
          <div className="lateral-acciones">
            <SelectorTema />
            <button type="button" className="salir" onClick={onSalir}>
              <Icono nombre="salir" tamaño={14} />Salir
            </button>
          </div>
        </div>
      </aside>
      {/* Fondo oscuro detras del cajon: tocarlo lo cierra. */}
      <div className="fondo-menu" aria-hidden="true" onClick={() => setMenuAbierto(false)} />

      <div className="contenido">
        {/* Solo en el celular: boton del menu y la marca. En pantallas anchas
            la marca esta en el lateral. */}
        <header className="barra-superior">
          <button type="button" className="boton-icono" aria-label="Abrir el menú"
            aria-expanded={menuAbierto} aria-controls="menu-lateral" onClick={() => setMenuAbierto(true)}>
            <Icono nombre="menu" tamaño={22} />
          </button>
          <Marca deviceId={estado.deviceId} />
        </header>

        <main>
          {/* Cada estado dice DE QUIEN es: "Monitor" es este servidor
              escuchando al broker; "Placa" es la ESP32 contestando el ping. */}
          <div className="barra-estado">
            <div className="conexiones">
              <span className="conexion" title={
                !estado.conectado ? 'Este navegador perdió la conexión con el servidor del monitor.'
                  : estado.brokerOk ? 'El servidor del monitor está conectado al broker y recibe la telemetría.'
                    : 'El servidor del monitor no llega al broker: no entra telemetría.'}>
                <span className={`punto ${estado.conectado && estado.brokerOk ? 'punto-ok' : 'punto-caido'}`} />
                <b>Monitor</b>{estado.conectado ? (estado.brokerOk ? 'en línea' : 'sin broker') : 'sin servidor'}
              </span>
              {estado.conectado && <EstadoPlaca placa={estado.placa} brokerOk={estado.brokerOk} />}
            </div>
          </div>

      <Suspense fallback={<p className="empty panel">Cargando...</p>}>
        <Routes>
          <Route path="/" element={<Inicio estado={estado} />} />
          <Route path="/en-curso" element={<Live estado={estado} />} />
          <Route path="/historial" element={<Historial />} />
          <Route path="/corrida/:id" element={<DetalleCorrida />} />
          <Route path="/bitacora" element={sesion.role === 'admin'
            ? <Bitacora enVivo={estado.eventos} />
            : <p className="empty panel">Solo los administradores ven la bitácora de la placa.</p>} />
          <Route path="/configuracion" element={sesion.role === 'admin'
            ? <Configuracion placa={estado.placa} />
            : <p className="empty panel">Solo los administradores ven y cambian la configuración de la placa.</p>} />
          <Route path="/usuarios" element={sesion.role === 'admin'
            ? <Usuarios />
            : <p className="empty panel">Solo los administradores gestionan usuarios.</p>} />
          <Route path="/cuenta" element={<Cuenta />} />
          <Route path="/acerca" element={<Acerca />} />
          <Route path="*" element={<p className="empty panel">Esa página no existe. <Link to="/">Ir al inicio</Link></p>} />
        </Routes>
      </Suspense>
        </main>
      </div>
      {/* Fuera de <main>: main es contenedor de consultas (container-type),
          y un position: fixed adentro podria quedar atado a el. */}
      <BotonArriba />
    </div>
  );
}

function Marca({ deviceId }: { deviceId?: string | null }) {
  return (
    <Link to="/" className="marca">
      <span className="marca-logo" aria-hidden="true"><Icono nombre="campo" /></span>
      <span>
        <span className="marca-nombre">SEMBAF</span>
        <span className="subtitle marca-sub">Monitor remoto{deviceId ? ` · ${deviceId}` : ''}</span>
      </span>
    </Link>
  );
}

// Automatico / claro / oscuro. Es una preferencia de este navegador.
const OPCIONES_TEMA: Array<{ valor: Preferencia; icono: string; nombre: string }> = [
  { valor: 'auto', icono: 'auto', nombre: 'Automático (como el sistema)' },
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
