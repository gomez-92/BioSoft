import { useEffect, useRef, useState } from 'react';
import { authFetch } from '../lib/auth.js';
import { filtrarBobinas } from '../lib/bobinas.js';
import { recortarVentana } from '../lib/series.js';
import type { AlertItem, Coils, Measures, Targets } from '../lib/types.js';
import { GraficoBobinas, GraficoCampo, GraficoTemperatura, type PuntoMedicion } from './Graficos.js';

// Evolucion temporal de la corrida en curso.
//
// Arranca con la serie que ya esta guardada (sin agregar: `bucket=raw`) y le
// va sumando cada muestra que llega por el socket. Asi abrir la pantalla a
// mitad de un experimento muestra la corrida entera, no solo lo que llego
// desde que se abrio, y lo que se ve en vivo es exactamente lo que quedo en
// la base (el servidor emite despues de guardar).
//
// Carga aparte (lazy, desde Live): son los graficos los que traen recharts, y
// la pantalla En curso muestra sus numeros sin esperarlos.

type PuntoBobinas = Record<string, unknown> & { ts: string };

// Ventana deslizante: los graficos muestran los ultimos N minutos y no la
// corrida entera. Con la corrida entera, cada muestra nueva comprimia todo lo
// anterior y en una corrida de una hora el ultimo minuto -- lo que se mira en
// vivo -- quedaba en un puñado de pixeles.
//
// La ventana es de TIEMPO, no de cantidad de muestras, y su borde derecho es
// "ahora", no la ultima muestra: si deja de llegar telemetria, el hueco se ve
// crecer a la derecha en vez de quedar disimulado por un eje que se detiene
// en el ultimo dato. La corrida entera sigue a un click ("Todo") y en el
// detalle de la corrida.
const VENTANAS: Array<{ minutos: number | null; nombre: string }> = [
  { minutos: 1, nombre: '1 min' },
  { minutos: 5, nombre: '5 min' },
  { minutos: 15, nombre: '15 min' },
  { minutos: 60, nombre: '1 h' },
  { minutos: null, nombre: 'Todo' },
];
const VENTANA_DEFAULT = 5;
const CLAVE_VENTANA = 'biosoft.ventanaEnVivo';

// La eleccion se recuerda por navegador (comodidad, no estado del monitor).
function leerVentana(): number | null {
  try {
    const guardada = localStorage.getItem(CLAVE_VENTANA);
    if (guardada === 'todo') return null;
    const minutos = Number(guardada);
    return VENTANAS.some((v) => v.minutos === minutos) ? minutos : VENTANA_DEFAULT;
  } catch {
    return VENTANA_DEFAULT;
  }
}

function guardarVentana(minutos: number | null): void {
  try { localStorage.setItem(CLAVE_VENTANA, minutos === null ? 'todo' : String(minutos)); } catch { /* sin persistir */ }
}

export default function GraficosEnVivo({ runId, startedAt, targets, config, measures, coils, alerts, habilitadas, ahora }: {
  runId: string; startedAt: string; targets: Targets | null; config: unknown;
  measures: Measures | null; coils: Coils | null; alerts: AlertItem[];
  habilitadas: Set<number> | null; ahora: number;
}) {
  const [puntos, setPuntos] = useState<PuntoMedicion[]>([]);
  const [puntosBobinas, setPuntosBobinas] = useState<PuntoBobinas[]>([]);
  const [alertas, setAlertas] = useState<AlertItem[]>([]);
  const [ventana, setVentana] = useState<number | null>(leerVentana);
  const ultimaMedicion = useRef<string | null>(null);
  const ultimaBobinas = useRef<string | null>(null);

  // La historia guardada de ESTA corrida, una vez por corrida.
  useEffect(() => {
    let vigente = true;
    setPuntos([]); setPuntosBobinas([]); setAlertas([]);
    ultimaMedicion.current = null; ultimaBobinas.current = null;

    authFetch(`/api/runs/${runId}/measures?bucket=raw`).then((r) => r.json())
      .then((data: { points: PuntoMedicion[] }) => {
        if (!vigente) return;
        // Lo que haya llegado en vivo mientras se pedia la historia se
        // conserva: se une sin duplicar por fecha.
        setPuntos((vivos) => unir(data.points ?? [], vivos));
      }).catch(() => {});
    authFetch(`/api/runs/${runId}/coils?bucket=raw`).then((r) => r.json())
      .then((data: { points: PuntoBobinas[] }) => {
        if (vigente) setPuntosBobinas((vivos) => unir(data.points ?? [], vivos));
      }).catch(() => {});
    authFetch(`/api/runs/${runId}/alerts`).then((r) => r.json())
      .then((data: { items: AlertItem[] }) => {
        if (vigente) setAlertas((vivas) => unir(data.items ?? [], vivas));
      }).catch(() => {});

    return () => { vigente = false; };
  }, [runId]);

  // Cada medicion nueva que trae el socket se suma a la serie.
  useEffect(() => {
    if (!measures || measures.ts === ultimaMedicion.current) return;
    if (new Date(measures.ts).getTime() < new Date(startedAt).getTime()) return;
    ultimaMedicion.current = measures.ts;
    setPuntos((previos) => unir(previos, [{ ts: measures.ts, field: measures.magneticField, temp: measures.temperature }]));
  }, [measures, startedAt]);

  useEffect(() => {
    if (!coils || coils.ts === ultimaBobinas.current) return;
    if (new Date(coils.ts).getTime() < new Date(startedAt).getTime()) return;
    ultimaBobinas.current = coils.ts;
    const punto: PuntoBobinas = { ts: coils.ts };
    for (const coil of coils.coils) { punto[`c${coil.n}`] = coil.current; punto[`d${coil.n}`] = coil.duty; }
    setPuntosBobinas((previos) => unir(previos, [punto]));
  }, [coils, startedAt]);

  // Las alertas del socket llegan de a una al frente de la lista corta de
  // useLive; aca se juntan todas las de la corrida.
  useEffect(() => {
    if (alerts.length > 0) setAlertas((previas) => unir(previas, alerts));
  }, [alerts]);

  const inicio = new Date(startedAt).getTime();
  // Sin ventana (Todo): desde el inicio, con al menos un minuto de eje. Con
  // ventana: los ultimos N minutos hasta ahora, salvo que la corrida sea mas
  // corta que la ventana -- ahi se ve entera, sin un tramo vacio a la
  // izquierda del inicio.
  const fin = ventana === null ? Math.max(ahora, inicio + 60_000) : Math.max(ahora, inicio + ventana * 60_000);
  const desde = ventana === null ? null : Math.max(inicio, fin - ventana * 60_000);
  const contexto = { targets, config, inicio };
  const enVentana = recortarVentana(puntos, desde);
  const bobinasEnVentana = recortarVentana(puntosBobinas, desde);
  const alertasEnVentana = desde === null
    ? alertas
    : alertas.filter((alerta) => new Date(alerta.ts).getTime() >= desde);
  const ocultas = alertas.length - alertasEnVentana.length;
  const presentes = new Set<number>();
  for (const punto of puntosBobinas) {
    for (const clave of Object.keys(punto)) {
      const m = /^[cd](\d)$/.exec(clave);
      if (m) presentes.add(Number(m[1]));
    }
  }
  const bobinas = filtrarBobinas([...presentes].sort().map((n) => ({ n })), habilitadas).map((b) => b.n);

  if (puntos.length === 0 && puntosBobinas.length === 0) {
    return <p className="empty">Todavia no hay mediciones de esta corrida para graficar.</p>;
  }

  const ejeDesde = desde ?? inicio;

  return (
    <>
      <div className="ventana-selector" role="group" aria-label="Ventana de tiempo de los graficos">
        <span className="tenue-celda">Mostrar</span>
        {VENTANAS.map((v) => (
          <button key={v.nombre} type="button" className={ventana === v.minutos ? 'activa' : ''}
            aria-pressed={ventana === v.minutos}
            onClick={() => { setVentana(v.minutos); guardarVentana(v.minutos); }}>
            {v.nombre}
          </button>
        ))}
        {ocultas > 0 && (
          <span className="tenue-celda">
            {ocultas} alerta{ocultas === 1 ? '' : 's'} anterior{ocultas === 1 ? '' : 'es'} fuera de la ventana (ver la lista)
          </span>
        )}
      </div>
      <div className="grilla-graficos">
        <GraficoCampo puntos={enVentana} bucket="raw" contexto={contexto} alertas={alertasEnVentana}
          desde={ejeDesde} fin={fin} alto={220} />
        <GraficoTemperatura puntos={enVentana} bucket="raw" contexto={contexto} alertas={alertasEnVentana}
          desde={ejeDesde} fin={fin} alto={220} />
        <GraficoBobinas puntos={bobinasEnVentana} bucket="raw" bobinas={bobinas} magnitud="corriente"
          inicio={ejeDesde} fin={fin} alto={180} />
        <GraficoBobinas puntos={bobinasEnVentana} bucket="raw" bobinas={bobinas} magnitud="duty"
          inicio={ejeDesde} fin={fin} alto={180} />
      </div>
    </>
  );
}

/** Une dos listas ordenadas por fecha sin repetir una misma marca de tiempo. */
function unir<T extends { ts: string }>(a: T[], b: T[]): T[] {
  const porFecha = new Map<string, T>();
  for (const item of a) porFecha.set(clave(item), item);
  for (const item of b) porFecha.set(clave(item), item);
  return [...porFecha.values()].sort((x, y) => new Date(x.ts).getTime() - new Date(y.ts).getTime());
}

// Dos alertas pueden compartir fecha (CEM1 y TEMP1 en el mismo segundo):
// la clave incluye fuente y tipo cuando los hay.
function clave(item: { ts: string } & Partial<Pick<AlertItem, 'source' | 'type'>>): string {
  return `${new Date(item.ts).getTime()}|${item.source ?? ''}|${item.type ?? ''}`;
}
