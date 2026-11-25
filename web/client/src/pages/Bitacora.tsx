import { useCallback, useEffect, useMemo, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { authFetch } from '../lib/auth.js';
import {
  CODIGOS, FILTROS_ALCANCE, FILTROS_NIVEL, nombreCorrida, pasaFiltro, type EventoPlaca,
} from '../lib/bitacora.js';
import { ListaEventos } from '../components/ListaEventos.js';

// Bitacora de la placa: lo que paso con el sistema, no con el experimento
// (reinicios, enlaces caidos, broker, configuracion ignorada). Es lo primero
// que se mira cuando el monitor muestra algo raro: un hueco en un grafico
// suele tener al lado un "broker caido" o un "el Mega se reinicio".
//
// Arranca de la API y suma los que llegan en vivo (useLive). Los eventos que
// la placa tuvo encolados con el broker caido llegan tarde y con su hora
// real, asi que la lista se ordena por fecha y no por orden de llegada.
//
// Cada evento dice en que corrida paso (lo calcula el servidor por fecha).
// ?corrida=<id> deja solo los de esa corrida: es a donde lleva "Ver en la
// bitacora" desde el detalle de una corrida.

const POR_PAGINA = 50;

export function Bitacora({ enVivo }: { enVivo: EventoPlaca[] }) {
  const [items, setItems] = useState<EventoPlaca[]>([]);
  const [hayMas, setHayMas] = useState(false);
  const [niveles, setNiveles] = useState('');
  const [codigo, setCodigo] = useState('');
  const [alcance, setAlcance] = useState('');
  const [params, setParams] = useSearchParams();
  const corrida = params.get('corrida') ?? '';
  // Buscador "Corrida #": el numero que muestran En curso, el Historial y los
  // logs USB. Mientras haya un numero valido manda sobre el resto de los
  // filtros de corrida.
  const [numero, setNumero] = useState('');
  const numeroValido = /^#?\d+$/.test(numero.trim()) ? numero.trim().replace(/^#/, '') : '';
  // El id de la corrida que encontro el numero (para la etiqueta y el filtro
  // de los que llegan en vivo).
  const [corridaDelNumero, setCorridaDelNumero] = useState<EventoPlaca['run']>(null);
  // Nombre de la corrida filtrada, para el chip (sale del primer evento que
  // la trae; sin eventos, se pide la corrida).
  const [corridaInfo, setCorridaInfo] = useState<{ number: number | null; startedAt: string; runType: string } | null>(null);
  const [cargando, setCargando] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const pedir = useCallback(async (antesDe?: string) => {
    const params = new URLSearchParams({ limit: String(POR_PAGINA) });
    if (niveles) params.set('level', niveles);
    if (codigo) params.set('code', codigo);
    if (numeroValido) params.set('number', numeroValido);
    else if (corrida) params.set('run', corrida);
    else if (alcance) params.set('scope', alcance);
    if (antesDe) params.set('before', antesDe);
    const respuesta = await authFetch(`/api/events?${params}`);
    if (respuesta.status === 404) {
      throw new Error(numeroValido ? `No hay ninguna corrida #${numeroValido}.` : 'Esa corrida no existe (puede haberse borrado).');
    }
    if (!respuesta.ok) throw new Error('No se pudo cargar la bitácora.');
    return respuesta.json() as Promise<{ items: EventoPlaca[]; hasMore: boolean; run?: EventoPlaca['run'] }>;
  }, [niveles, codigo, alcance, corrida, numeroValido]);

  useEffect(() => {
    setCorridaInfo(null);
    if (!corrida) return;
    let vigente = true;
    authFetch(`/api/runs/${corrida}`)
      .then((r) => (r.ok ? r.json() : null))
      .then((run) => {
        if (vigente && run) setCorridaInfo({ number: run.number ?? null, startedAt: run.startedAt, runType: run.runType });
      })
      .catch(() => {});
    return () => { vigente = false; };
  }, [corrida]);

  useEffect(() => {
    let vigente = true;
    setCargando(true);
    setError(null);
    pedir()
      .then((data) => {
        if (!vigente) return;
        setItems(data.items); setHayMas(data.hasMore); setCorridaDelNumero(data.run ?? null);
      })
      .catch((cause) => { if (vigente) { setError(cause.message); setItems([]); setHayMas(false); } })
      .finally(() => { if (vigente) setCargando(false); });
    return () => { vigente = false; };
  }, [pedir]);

  async function cargarMas() {
    const ultimo = items[items.length - 1];
    if (!ultimo) return;
    setCargando(true);
    try {
      const data = await pedir(ultimo.ts);
      setItems((previos) => [...previos, ...data.items]);
      setHayMas(data.hasMore);
    } catch (cause) {
      setError((cause as Error).message);
    } finally {
      setCargando(false);
    }
  }

  // Los en vivo que pasan el filtro, sin repetir los que ya trajo la API.
  const eventos = useMemo(() => {
    const porId = new Map<string, EventoPlaca>();
    const filtros = {
      niveles, codigo, alcance,
      // Con un numero, los en vivo se filtran por la corrida que encontro;
      // mientras no la encontro, ninguno.
      corrida: numeroValido ? (corridaDelNumero?.id ?? '-') : corrida,
    };
    for (const evento of [...enVivo.filter((e) => pasaFiltro(e, filtros)), ...items]) {
      porId.set(evento.id, evento);
    }
    return [...porId.values()].sort((a, b) => b.ts.localeCompare(a.ts));
  }, [enVivo, items, niveles, codigo, alcance, corrida, numeroValido, corridaDelNumero]);

  return (
    <>
      <section className="filtros">
        <input type="search" className="buscar-numero" placeholder="Corrida #" value={numero}
          inputMode="numeric" aria-label="Buscar los eventos de una corrida por su número"
          onChange={(e) => setNumero(e.target.value)} />
        <select value={niveles} onChange={(e) => setNiveles(e.target.value)} aria-label="Nivel">
          {FILTROS_NIVEL.map((f) => <option key={f.valor} value={f.valor}>{f.nombre}</option>)}
        </select>
        <select value={codigo} onChange={(e) => setCodigo(e.target.value)} aria-label="Tipo de evento">
          <option value="">Todos los eventos</option>
          {Object.entries(CODIGOS).map(([valor, nombre]) => <option key={valor} value={valor}>{nombre}</option>)}
        </select>
        {numeroValido ? (
          corridaDelNumero && (
            <span className="chip chip-corrida filtro-corrida">
              Corrida {nombreCorrida(corridaDelNumero)}
              <Link to={`/corrida/${corridaDelNumero.id}`} className="enlace-texto">ver detalle</Link>
              <button type="button" className="enlace-texto" aria-label="Quitar el filtro de corrida"
                onClick={() => setNumero('')}>quitar ✕</button>
            </span>
          )
        ) : corrida ? (
          <span className="chip chip-corrida filtro-corrida">
            Corrida {corridaInfo ? nombreCorrida(corridaInfo) : ''}
            <Link to={`/corrida/${corrida}`} className="enlace-texto">ver detalle</Link>
            <button type="button" className="enlace-texto" aria-label="Quitar el filtro de corrida"
              onClick={() => { params.delete('corrida'); setParams(params); }}>quitar ✕</button>
          </span>
        ) : (
          <select value={alcance} onChange={(e) => setAlcance(e.target.value)} aria-label="Corridas">
            {FILTROS_ALCANCE.map((f) => <option key={f.valor} value={f.valor}>{f.nombre}</option>)}
          </select>
        )}
      </section>

      {error && <p className="aviso panel-aviso">{error}</p>}
      <p className="tenue">Los eventos se guardan una semana; después se borran solos.</p>

      <section className="panel panel-lleno">
        {cargando && eventos.length === 0 ? (
          <p className="empty">Cargando...</p>
        ) : eventos.length === 0 ? (
          <p className="empty">
            {corrida || numeroValido
              ? 'La placa no registró eventos durante esta corrida (o ya pasaron más de 7 días).'
              : niveles || codigo || alcance
              ? 'No hay eventos que coincidan.'
              : 'La placa todavía no registró eventos. Aparecen acá cuando arranca, cuando se cae un enlace o se reinicia el Mega.'}
          </p>
        ) : (
          <ListaEventos eventos={eventos} />
        )}
        {hayMas && (
          <nav className="paginado">
            <button type="button" disabled={cargando} onClick={cargarMas}>Cargar más</button>
          </nav>
        )}
      </section>
    </>
  );
}
