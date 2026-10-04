import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { authFetch } from '../lib/auth.js';
import { duracion, esCampoNulo, nombreModo, nombreMotivo } from '../lib/format.js';
import { useEsAdmin } from '../lib/sesion.js';
import { ConRelajaciones, TipoCorrida } from '../components/TipoCorrida.js';

// Lista de corridas. Lo que se busca al recorrerla es casi siempre lo mismo:
// que corridas fueron control (campo nulo) y cuales se cortaron solas. Por eso
// el modo y el motivo estan en la fila y no hay que abrir cada una.
//
// Las PRUEBAS tienen su propia pestaña, con su cuenta a la vista. Se graban
// igual que los experimentos, pero no se mezclan con ellos salvo que se pidan
// (un promedio hecho desde el historial no tiene que arrastrar datos de
// banco). Antes eran una opcion escondida en un desplegable, y desde afuera
// parecia que las pruebas no se guardaban.

interface RunRow {
  id: string;
  startedAt: string;
  endedAt: string | null;
  state: string;
  runType: string;
  relaxations: number | null;
  mode: string | null;
  reason: string | null;
  durationMinutes: number | null;
  measureCount: number;
  alertCount: number;
}

const POR_PAGINA = 25;

// Vacio = lo que el backend da sin `type`: todo menos las pruebas.
const PESTAÑAS: Array<{ valor: string; nombre: string; cuenta: (t: Cuentas) => number }> = [
  { valor: '', nombre: 'Experimentos', cuenta: (t) => t.normal + t.unknown },
  { valor: 'test', nombre: 'Pruebas', cuenta: (t) => t.test },
  { valor: 'all', nombre: 'Todas', cuenta: (t) => t.normal + t.unknown + t.test },
];

interface Cuentas { normal: number; test: number; unknown: number }

export function Historial() {
  const esAdmin = useEsAdmin();
  const [items, setItems] = useState<RunRow[]>([]);
  const [total, setTotal] = useState(0);
  const [cuentas, setCuentas] = useState<Cuentas | null>(null);
  const [pagina, setPagina] = useState(1);
  const [motivo, setMotivo] = useState('');
  const [modo, setModo] = useState('');
  const [tipo, setTipo] = useState('');
  const [cargando, setCargando] = useState(true);
  const [seleccion, setSeleccion] = useState<Set<string>>(new Set());
  const [error, setError] = useState<string | null>(null);

  const cargar = useCallback(() => {
    const params = new URLSearchParams({ page: String(pagina), limit: String(POR_PAGINA) });
    if (motivo) params.set('reason', motivo);
    if (modo) params.set('mode', modo);
    if (tipo) params.set('type', tipo);

    setCargando(true);
    authFetch(`/api/runs?${params}`)
      .then((r) => r.json())
      .then((data) => { setItems(data.items); setTotal(data.total); setCuentas(data.byType ?? null); })
      .finally(() => setCargando(false));
  }, [pagina, motivo, modo, tipo]);

  useEffect(cargar, [cargar]);
  // Cambiar de pagina o de filtro descarta la seleccion: borrar algo que ya
  // no se ve en pantalla seria una sorpresa.
  useEffect(() => setSeleccion(new Set()), [pagina, motivo, modo, tipo]);

  const paginas = Math.max(1, Math.ceil(total / POR_PAGINA));
  const borrables = items.filter((run) => run.state !== 'running');

  function alternar(id: string) {
    setSeleccion((previa) => {
      const nueva = new Set(previa);
      if (nueva.has(id)) nueva.delete(id); else nueva.add(id);
      return nueva;
    });
  }

  async function borrarSeleccion() {
    const ids = [...seleccion];
    const pruebas = items.filter((run) => seleccion.has(run.id) && run.runType === 'test').length;
    const resto = ids.length - pruebas;
    const detalle = resto > 0
      ? `\n\nOjo: ${resto === 1 ? 'una NO es prueba' : `${resto} NO son pruebas`} (experimento o sin marca).`
      : '';
    if (!window.confirm(`¿Borrar ${ids.length} corrida${ids.length === 1 ? '' : 's'} con todas sus mediciones y alertas? No se puede deshacer.${detalle}`)) return;
    setError(null);
    const respuesta = await authFetch('/api/runs/delete', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ ids }),
    });
    const data = await respuesta.json().catch(() => ({}));
    if (!respuesta.ok) { setError(data.error ?? 'no se pudo borrar'); return; }
    if (data.skippedRunning) setError('La corrida en curso no se borro: se puede borrar cuando termine.');
    setSeleccion(new Set());
    cargar();
  }

  return (
    <>
      <nav className="pestañas" aria-label="Tipo de corrida">
        {PESTAÑAS.map((p) => (
          <button key={p.valor} type="button" className={tipo === p.valor ? 'activa' : ''}
            onClick={() => { setTipo(p.valor); setPagina(1); }}>
            {p.nombre}{cuentas ? <span className="pestaña-cuenta">{p.cuenta(cuentas)}</span> : null}
          </button>
        ))}
      </nav>

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
        {esAdmin && borrables.length > 0 && (
          <label className="chk-todas">
            <input type="checkbox"
              checked={borrables.length > 0 && borrables.every((run) => seleccion.has(run.id))}
              onChange={(e) => setSeleccion(e.target.checked ? new Set(borrables.map((run) => run.id)) : new Set())} />
            Seleccionar pagina
          </label>
        )}
        {esAdmin && seleccion.size > 0 && (
          <button type="button" className="boton boton-peligro boton-chico" onClick={borrarSeleccion}>
            Borrar {seleccion.size}
          </button>
        )}
        <span className="filtros-total">{total} corrida{total === 1 ? '' : 's'}</span>
      </section>

      {error && <p className="aviso panel-aviso">{error}</p>}

      {cargando && items.length === 0 ? (
        <p className="empty panel">Cargando...</p>
      ) : items.length === 0 ? (
        <p className="empty panel">
          {tipo === 'test' ? 'No hay pruebas guardadas que coincidan.' : 'No hay corridas que coincidan.'}
          {tipo === '' && cuentas && cuentas.test > 0 && (
            <> Hay {cuentas.test} prueba{cuentas.test === 1 ? '' : 's'} guardada{cuentas.test === 1 ? '' : 's'}:{' '}
              <button type="button" className="enlace-texto enlace-en-linea" onClick={() => { setTipo('test'); setPagina(1); }}>
                ver pruebas
              </button>.
            </>
          )}
        </p>
      ) : (
        <ul className="lista-corridas">
          {items.map((run) => (
            <Fila key={run.id} run={run}
              seleccionable={esAdmin && run.state !== 'running'}
              seleccionada={seleccion.has(run.id)}
              onAlternar={() => alternar(run.id)} />
          ))}
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

function Fila({ run, seleccionable, seleccionada, onAlternar }: {
  run: RunRow; seleccionable: boolean; seleccionada: boolean; onAlternar: () => void;
}) {
  const inicio = new Date(run.startedAt);
  const segundos = run.endedAt
    ? (new Date(run.endedAt).getTime() - inicio.getTime()) / 1000
    : undefined;
  const esNulo = esCampoNulo(run.mode);

  return (
    <li className={`fila-con-seleccion ${seleccionada ? 'seleccionada' : ''}`}>
      {seleccionable && (
        <input type="checkbox" className="fila-check" checked={seleccionada} onChange={onAlternar}
          aria-label={`Seleccionar corrida del ${inicio.toLocaleString('es-AR')}`} />
      )}
      <Link to={`/corrida/${run.id}`} className="fila-corrida">
        <div className="fila-principal">
          <span className="fila-fecha">
            {inicio.toLocaleDateString('es-AR')} {inicio.toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit' })}
          </span>
          {run.mode && (
            <span className={`chip chip-modo ${esNulo ? 'chip-nulo' : ''}`}>{nombreModo(run.mode)}</span>
          )}
          <TipoCorrida runType={run.runType} />
          <ConRelajaciones runType={run.runType} relaxations={run.relaxations} />
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
