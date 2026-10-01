import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { authFetch } from '../lib/auth.js';
import { duracion, esCampoNulo, nombreModo, nombreMotivo } from '../lib/format.js';
import { TipoCorrida } from '../components/TipoCorrida.js';

// Lista de corridas. Lo que se busca al recorrerla es casi siempre lo mismo:
// que corridas fueron control (campo nulo) y cuales se cortaron solas. Por eso
// el modo y el motivo estan en la fila y no hay que abrir cada una.

interface RunRow {
  id: string;
  startedAt: string;
  endedAt: string | null;
  state: string;
  runType: string;
  mode: string | null;
  reason: string | null;
  durationMinutes: number | null;
  measureCount: number;
  alertCount: number;
}

const POR_PAGINA = 20;

export function Historial() {
  const [items, setItems] = useState<RunRow[]>([]);
  const [total, setTotal] = useState(0);
  const [pagina, setPagina] = useState(1);
  const [motivo, setMotivo] = useState('');
  const [modo, setModo] = useState('');
  // Vacio = lo que el backend da sin `type`: todo menos las corridas de
  // prueba. Las pruebas no se mezclan con los experimentos salvo que se pidan.
  const [tipo, setTipo] = useState('');
  const [cargando, setCargando] = useState(true);

  useEffect(() => {
    const params = new URLSearchParams({ page: String(pagina), limit: String(POR_PAGINA) });
    if (motivo) params.set('reason', motivo);
    if (modo) params.set('mode', modo);
    if (tipo) params.set('type', tipo);

    setCargando(true);
    authFetch(`/api/runs?${params}`)
      .then((r) => r.json())
      .then((data) => { setItems(data.items); setTotal(data.total); })
      .finally(() => setCargando(false));
  }, [pagina, motivo, modo, tipo]);

  const paginas = Math.max(1, Math.ceil(total / POR_PAGINA));

  return (
    <>
      {/* Los filtros van en una fila arriba de la tabla. */}
      <section className="filtros">
        <select value={motivo} onChange={(e) => { setMotivo(e.target.value); setPagina(1); }}>
          <option value="">Todos los motivos</option>
          <option value="completed">Completados</option>
          <option value="critical">Cortados por alerta</option>
          <option value="stopped">Detenidos</option>
        </select>
        <select value={modo} onChange={(e) => { setModo(e.target.value); setPagina(1); }}>
          <option value="">Todos los modos</option>
          {/* Los valores son los que publica la placa ("x" / "null", el
              campo `value` de optionsFieldMode), no las etiquetas. */}
          <option value="x">Campo X</option>
          <option value="null">Campo nulo</option>
        </select>
        <select value={tipo} onChange={(e) => { setTipo(e.target.value); setPagina(1); }}>
          <option value="">Sin pruebas</option>
          <option value="normal">Solo experimentos marcados</option>
          <option value="test">Solo pruebas</option>
          <option value="unknown">Sin marca</option>
          <option value="all">Todas</option>
        </select>
        <span className="filtros-total">{total} corrida{total === 1 ? '' : 's'}</span>
      </section>

      {cargando && items.length === 0 ? (
        <p className="empty panel">Cargando...</p>
      ) : items.length === 0 ? (
        <p className="empty panel">No hay corridas que coincidan.</p>
      ) : (
        <ul className="lista-corridas">
          {items.map((run) => <Fila key={run.id} run={run} />)}
        </ul>
      )}

      {paginas > 1 && (
        <nav className="paginado">
          <button type="button" disabled={pagina <= 1} onClick={() => setPagina((p) => p - 1)}>
            Anterior
          </button>
          <span>{pagina} de {paginas}</span>
          <button type="button" disabled={pagina >= paginas} onClick={() => setPagina((p) => p + 1)}>
            Siguiente
          </button>
        </nav>
      )}
    </>
  );
}

function Fila({ run }: { run: RunRow }) {
  const inicio = new Date(run.startedAt);
  const segundos = run.endedAt
    ? (new Date(run.endedAt).getTime() - inicio.getTime()) / 1000
    : undefined;
  const esNulo = esCampoNulo(run.mode);

  return (
    <li>
      <Link to={`/corrida/${run.id}`} className="fila-corrida">
        <div className="fila-principal">
          <span className="fila-fecha">
            {inicio.toLocaleDateString('es-AR')} {inicio.toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit' })}
          </span>
          {run.mode && (
            <span className={`chip chip-modo ${esNulo ? 'chip-nulo' : ''}`}>{nombreModo(run.mode)}</span>
          )}
          <TipoCorrida runType={run.runType} />
          <Motivo reason={run.reason} state={run.state} />
        </div>
        <div className="fila-datos">
          <span>{duracion(segundos)}</span>
          <span>{run.measureCount} mediciones</span>
          {run.alertCount > 0 && <span className="fila-alertas">{run.alertCount} alertas</span>}
        </div>
      </Link>
    </li>
  );
}

// Una corrida huerfana no tiene motivo porque nunca llego su result: decirlo
// es mas util que dejar la celda vacia, porque explica por que le faltan datos.
function Motivo({ reason, state }: { reason: string | null; state: string }) {
  if (reason) return <span className={`chip chip-motivo-${reason}`}>{nombreMotivo(reason)}</span>;
  if (state === 'running') return <span className="chip chip-normal">En curso</span>;
  return <span className="chip chip-huerfana">Sin cierre</span>;
}
