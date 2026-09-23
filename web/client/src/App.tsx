import { Suspense, lazy } from 'react';
import { BrowserRouter, Link, NavLink, Route, Routes, useNavigate } from 'react-router-dom';
import { Historial } from './pages/Historial.js';

import { Live } from './pages/Live.js';
import { useLive } from './lib/useLive.js';

// El detalle carga aparte: los graficos (recharts) son mas de la mitad del
// bundle y solo se usan ahi. La pantalla En curso, que es la que se abre desde
// el celular en medio del campo, no tiene por que descargarlos.
const DetalleCorrida = lazy(() =>
  import('./pages/Detalle.js').then((m) => ({ default: m.DetalleCorrida })));

export function App() {
  return (
    <BrowserRouter>
      <Contenido />
    </BrowserRouter>
  );
}

function Contenido() {
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
        <span className={`enlace ${estado.conectado && estado.brokerOk ? 'enlace-ok' : 'enlace-caido'}`}>
          {estado.conectado ? (estado.brokerOk ? 'En linea' : 'Sin broker') : 'Sin servidor'}
        </span>
      </header>

      <nav className="navegacion">
        <NavLink to="/" end>En curso</NavLink>
        <NavLink to="/historial">Historial</NavLink>
      </nav>

      <Suspense fallback={<p className="empty panel">Cargando...</p>}>
      <Routes>
        <Route path="/" element={<Live estado={estado} onVerCorrida={(id) => navigate(`/corrida/${id}`)} />} />
        <Route path="/historial" element={<Historial />} />
        <Route path="/corrida/:id" element={<DetalleCorrida />} />
        <Route path="*" element={<p className="empty panel">Esa pagina no existe. <Link to="/">Ir al inicio</Link></p>} />
      </Routes>
      </Suspense>
    </main>
  );
}
