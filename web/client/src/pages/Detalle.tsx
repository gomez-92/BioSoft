import { useEffect, useState } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import {
  GraficoBobinas, GraficoCampo, GraficoTemperatura, type PuntoMedicion,
} from '../components/Graficos.js';
import { ListaAlertas } from '../components/ListaAlertas.js';
import { ListaEventos } from '../components/ListaEventos.js';
import type { EventoPlaca } from '../lib/bitacora.js';
import { useEsAdmin } from '../lib/sesion.js';
import { authFetch } from '../lib/auth.js';
import { bobinasHabilitadas, filtrarBobinas } from '../lib/bobinas.js';
import {
  avanceFinal, duracion, duracionPedida, esCampoNulo, nombreModo, nombreMotivo, nombreTipoAlerta, numero,
  numeroCorrida, relajacionesConAviso, relajacionesDe,
} from '../lib/format.js';
import { useConfigDeCorrida } from '../lib/useConfig.js';
import type { AlertItem, RunType, Targets } from '../lib/types.js';
import { ConRelajaciones, TipoCorrida } from '../components/TipoCorrida.js';

// Detalle de una corrida: que se pidio, que paso y por que termino.

interface Detalle {
  id: string;
  number: number | null; startedAt: string; endedAt: string | null; state: string;
  runType: RunType;
  targets: Targets | null;
  result: {
    reason: string; description?: string; progressPercent?: number;
    elapsedSeconds?: number; meanMagneticField?: number;
    source?: string; type?: string; count?: number; limit?: number; emergency?: boolean;
  } | null;
  stats: { measureCount?: number; alertCount?: number } | null;
}

interface SerieMediciones { bucket: string; points: PuntoMedicion[] }
interface SerieBobinas {
  bucket: string; coils: number[]; points: Array<Record<string, unknown> & { ts: string }>;
}

export function DetalleCorrida() {
  const { id } = useParams<{ id: string }>();
  const [detalle, setDetalle] = useState<Detalle | null>(null);
  const [mediciones, setMediciones] = useState<SerieMediciones | null>(null);
  const [bobinas, setBobinas] = useState<SerieBobinas | null>(null);
  const [alertas, setAlertas] = useState<AlertItem[]>([]);
  const [eventos, setEventos] = useState<EventoPlaca[]>([]);
  // La bitacora es solo de admin: a otro usuario la API le contesta 403.
  const esAdmin = useEsAdmin();
  const [error, setError] = useState<string | null>(null);
  const config = useConfigDeCorrida(detalle?.targets?.configId);

  useEffect(() => {
    if (!id) return;
    let vigente = true;

    authFetch(`/api/runs/${id}`)
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(r.status === 404 ? 'No existe esa corrida.' : 'No se pudo cargar.'))))
      .then((data: Detalle) => {
        if (!vigente) return;
        setDetalle(data);
        if (!esAdmin) return;
        // La bitacora no va atada a corridas: se buscan los eventos de su
        // ventana de tiempo, con un minuto de margen a cada lado (un enlace
        // caido justo antes del inicio explica muchas cosas).
        const desde = new Date(new Date(data.startedAt).getTime() - 60_000).toISOString();
        const hasta = new Date((data.endedAt ? new Date(data.endedAt).getTime() : Date.now()) + 60_000).toISOString();
        authFetch(`/api/events?${new URLSearchParams({ from: desde, to: hasta, limit: '200' })}`)
          .then((r) => (r.ok ? r.json() : { items: [] }))
          .then((ev) => { if (vigente) setEventos(ev.items); })
          .catch(() => {});
      })
      .catch((cause) => { if (vigente) setError(cause.message); });

    authFetch(`/api/runs/${id}/measures`).then((r) => r.json())
      .then((data) => { if (vigente) setMediciones(data); }).catch(() => {});
    authFetch(`/api/runs/${id}/coils`).then((r) => r.json())
      .then((data) => { if (vigente) setBobinas(data); }).catch(() => {});
    authFetch(`/api/runs/${id}/alerts`).then((r) => r.json())
      .then((data) => { if (vigente) setAlertas(data.items); }).catch(() => {});

    return () => { vigente = false; };
  }, [id, esAdmin]);

  if (error) return <p className="empty panel">{error} <Link to="/historial">Volver</Link></p>;
  if (!detalle) return <p className="empty panel">Cargando...</p>;

  const inicio = new Date(detalle.startedAt).getTime();
  const fin = detalle.endedAt ? new Date(detalle.endedAt).getTime() : Date.now();
  const esNulo = esCampoNulo(detalle.targets?.mode);
  const contexto = { targets: detalle.targets, config, inicio };
  // Solo las bobinas habilitadas en la configuracion con la que corrio (ver
  // lib/bobinas.ts): el Mega informa tambien las registradas y apagadas.
  const bobinasVisibles = bobinas
    ? filtrarBobinas(bobinas.coils.map((n) => ({ n })), bobinasHabilitadas(config)).map((b) => b.n)
    : [];

  return (
    <>
      <section className="panel">
        <div className="encabezado-run">
          {numeroCorrida(detalle.number) && <span className="chip chip-numero">Corrida {numeroCorrida(detalle.number)}</span>}
          {detalle.targets?.mode && (
            <span className={`chip chip-modo ${esNulo ? 'chip-nulo' : ''}`}>
              {nombreModo(detalle.targets.mode)}
            </span>
          )}
          <TipoCorrida runType={detalle.runType} />
          <ConRelajaciones runType={detalle.runType} relaxations={detalle.targets?.relaxations} />
          {detalle.result && (
            <span className={`chip chip-motivo-${detalle.result.reason}`}>
              {nombreMotivo(detalle.result.reason)}
            </span>
          )}
          {!detalle.result && detalle.state === 'orphan' && (
            <span className="chip chip-huerfana">Sin cierre</span>
          )}
        </div>
        <p className="detalle-fecha">
          {new Date(detalle.startedAt).toLocaleString('es-AR')} · {duracion((fin - inicio) / 1000)}
        </p>
        {detalle.result?.description && <p className="resultado-detalle">{detalle.result.description}</p>}
        {/* Una corrida huerfana no es basura: sus mediciones son reales. Pero
            hay que decir que le falta el cierre, o sus numeros se leerian como
            si describieran un experimento completo. */}
        {/* Un experimento con protecciones apagadas o datos simulados: los
            numeros pueden ser reales, pero no los de un experimento en
            condiciones, y eso tiene que leerse antes que los graficos. */}
        {detalle.runType === 'normal' && relajacionesConAviso(detalle.targets?.relaxations).length > 0 && (
          <p className="aviso">
            Declarada como experimento, pero corrió con: {relajacionesConAviso(detalle.targets?.relaxations).join(', ')}.
          </p>
        )}
        {!detalle.result && detalle.state === 'orphan' && (
          <p className="aviso">
            Esta corrida nunca recibió su mensaje de cierre: el monitor no sabe cómo terminó.
            Las mediciones que sí llegaron son reales.
          </p>
        )}
      </section>

      <Objetivos targets={detalle.targets} resultado={detalle.result} />
      {detalle.targets?.configId && <ConfiguracionDeLaCorrida configId={detalle.targets.configId} />}

      {mediciones && mediciones.points.length > 0 ? (
        <section className="panel">
          {/* Campo con el duty justo debajo (se leen juntos); sin bobinas
              quedan campo y temperatura lado a lado, como antes. */}
          {bobinas && bobinas.points.length > 0 ? (
            <div className="grilla-graficos grilla-campo-duty">
              <GraficoCampo puntos={mediciones.points} bucket={mediciones.bucket}
                contexto={contexto} alertas={alertas} fin={fin} />
              <GraficoBobinas puntos={bobinas.points} bucket={bobinas.bucket} bobinas={bobinasVisibles}
                magnitud="duty" inicio={inicio} fin={fin} />
              <GraficoTemperatura puntos={mediciones.points} bucket={mediciones.bucket}
                contexto={contexto} alertas={alertas} fin={fin} />
              <GraficoBobinas puntos={bobinas.points} bucket={bobinas.bucket} bobinas={bobinasVisibles}
                magnitud="corriente" inicio={inicio} fin={fin} />
            </div>
          ) : (
            <div className="grilla-graficos">
              <GraficoCampo puntos={mediciones.points} bucket={mediciones.bucket}
                contexto={contexto} alertas={alertas} fin={fin} />
              <GraficoTemperatura puntos={mediciones.points} bucket={mediciones.bucket}
                contexto={contexto} alertas={alertas} fin={fin} />
            </div>
          )}
          {alertas.length > 0 && (
            <p className="tenue">
              Cada alerta es un punto en el gráfico de su magnitud (CEM1 en el campo, TEMP1 en la temperatura), a la
              altura de la última medición antes de levantarse; pasa por encima para ver el detalle. Un anillo marca la
              que cortó el experimento.
            </p>
          )}
        </section>
      ) : (
        <p className="empty panel">Esta corrida no tiene mediciones guardadas.</p>
      )}

      {alertas.length > 0 && (
        <section className="panel">
          <h3>Alertas ({alertas.length})</h3>
          <ListaAlertas alertas={alertas} targets={detalle.targets} config={config} inicio={detalle.startedAt} />
        </section>
      )}

      {esAdmin && eventos.length === 0 && Date.now() - fin > 7 * 24 * 3600_000 && (
        <p className="tenue panel">
          Sin eventos de la placa: la bitácora guarda una semana y esta corrida es anterior.
        </p>
      )}

      {eventos.length > 0 && (
        <section className="panel">
          <h3>Eventos de la placa ({eventos.length})</h3>
          <p className="tenue">
            De la bitácora, entre un minuto antes del inicio y un minuto después del fin: reinicios, enlaces caídos,
            broker. Explican huecos en los gráficos o un cierre sin motivo.
          </p>
          <ListaEventos eventos={eventos} conFecha={false} conCorrida={false} />
          <p className="tenue"><Link to={`/bitacora?corrida=${detalle.id}`}>Ver en la bitácora</Link> (solo los de
            esta corrida, sin el minuto de margen).</p>
        </section>
      )}

      <section className="acciones">
        <BotonCsv id={detalle.id} />
        <BotonBorrar id={detalle.id} enCurso={detalle.state === 'running'} />
      </section>
    </>
  );
}

/** Borrar la corrida (solo administradores). Irreversible: lo confirma. */
function BotonBorrar({ id, enCurso }: { id: string; enCurso: boolean }) {
  const esAdmin = useEsAdmin();
  const navigate = useNavigate();
  const [error, setError] = useState<string | null>(null);
  if (!esAdmin) return null;

  async function borrar() {
    if (!window.confirm('¿Borrar esta corrida con todas sus mediciones y alertas? No se puede deshacer.')) return;
    setError(null);
    const respuesta = await authFetch(`/api/runs/${id}`, { method: 'DELETE' });
    if (respuesta.ok) { navigate('/historial'); return; }
    const data = await respuesta.json().catch(() => ({}));
    setError(data.error ?? 'no se pudo borrar');
  }

  return (
    <>
      <button type="button" className="boton boton-peligro" onClick={borrar} disabled={enCurso}
        title={enCurso ? 'La corrida está en curso: se puede borrar cuando termine' : undefined}>
        Borrar corrida
      </button>
      {error && <p className="login-error">{error}</p>}
    </>
  );
}

/**
 * La descarga NO puede ser un <a href>: una navegacion del navegador no lleva
 * el encabezado Authorization, asi que con la API cerrada el link devolveria
 * 401 y el usuario veria "no autorizado" en vez de su archivo. Se pide con
 * authFetch y se arma la descarga desde el blob.
 */
function BotonCsv({ id }: { id: string }) {
  const [bajando, setBajando] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function descargar() {
    setBajando(true);
    setError(null);
    try {
      const respuesta = await authFetch(`/api/runs/${id}/export.csv`);
      if (!respuesta.ok) throw new Error('no se pudo descargar');

      const blob = await respuesta.blob();
      const url = URL.createObjectURL(blob);
      const enlace = document.createElement('a');
      enlace.href = url;
      // El nombre lo decide el servidor (lleva la fecha de la corrida); si no
      // viene, uno razonable de este lado.
      const cabecera = respuesta.headers.get('content-disposition') ?? '';
      enlace.download = /filename="([^"]+)"/.exec(cabecera)?.[1] ?? `biosoft-${id}.csv`;
      enlace.click();
      URL.revokeObjectURL(url);
    } catch {
      setError('No se pudo descargar el archivo.');
    } finally {
      setBajando(false);
    }
  }

  return (
    <>
      <button type="button" className="boton" onClick={descargar} disabled={bajando}>
        {bajando ? 'Preparando...' : 'Descargar CSV'}
      </button>
      {error && <p className="login-error">{error}</p>}
    </>
  );
}

// La configuracion completa con la que corrio (tarjetas 23 y 24): la placa
// publica la vigente y el monitor la guarda por id. Se pide recien al abrirla.
function ConfiguracionDeLaCorrida({ configId }: { configId: string }) {
  const [contenido, setContenido] = useState<unknown>(undefined);
  const [estado, setEstado] = useState<'cerrado' | 'cargando' | 'listo' | 'sin-datos'>('cerrado');

  if (configId === 'default') return null;

  function abrir() {
    if (estado !== 'cerrado') return;
    setEstado('cargando');
    authFetch(`/api/config/snapshots/${configId}`)
      .then((r) => (r.ok ? r.json() : null))
      .then((data) => {
        if (data?.content) { setContenido(data.content); setEstado('listo'); }
        else setEstado('sin-datos');
      })
      .catch(() => setEstado('sin-datos'));
  }

  return (
    <section className="panel">
      <details onToggle={abrir}>
        <summary>Configuración completa ({configId})</summary>
        {estado === 'cargando' && <p className="empty">Cargando...</p>}
        {estado === 'sin-datos' && (
          <p className="empty">
            El monitor no tiene guardada esta configuración: la placa nunca la informó mientras estaba
            conectado.
          </p>
        )}
        {estado === 'listo' && <pre className="config-json">{JSON.stringify(contenido, null, 2)}</pre>}
      </details>
    </section>
  );
}

function Objetivos({ targets, resultado }: { targets: Targets | null; resultado: Detalle['result'] }) {
  if (!targets && !resultado) return null;
  return (
    <section className="panel">
      <h3>Objetivos y resultado</h3>
      <dl className="datos">
        {targets?.cem !== undefined && <D t="Intensidad" v={`${numero(targets.cem, 2)} mT`} />}
        {/* El campo medio medido, al lado del objetivo: es la comparacion que
            dice si la corrida sirve, y la unica forma de verla es esta. */}
        {resultado?.meanMagneticField !== undefined && (
          <D t="Campo medio medido" v={`${numero(resultado.meanMagneticField, 3)} mT`} />
        )}
        {targets?.freq !== undefined && <D t="Frecuencia" v={`${numero(targets.freq, 0)} Hz`} />}
        {targets?.dur !== undefined && <D t="Duración pedida" v={duracionPedida(targets.dur)} />}
        {targets?.tol !== undefined && <D t="Tolerancia" v={`±${numero(targets.tol, 0)} %`} />}
        {targets?.tnmin !== undefined && (
          <D t="Temp. normal" v={`${numero(targets.tnmin, 0)}–${numero(targets.tnmax, 0)} °C`} />
        )}
        {targets?.tcmin !== undefined && (
          <D t="Temp. crítica" v={`${numero(targets.tcmin, 0)}–${numero(targets.tcmax, 0)} °C`} />
        )}
        {avanceFinal(resultado, targets?.dur).porcentaje !== undefined && (
          <D t="Avance" v={`${avanceFinal(resultado, targets?.dur).porcentaje} %`} />
        )}
        {/* Con que configuracion de la tarjeta SD corrio: el id lo calcula
            la placa ("default" = valores de fabrica, sin tarjeta). */}
        {targets?.configId && <D t="Configuración" v={targets.configId} />}
        {targets?.relaxations !== undefined && (
          <D t="Relajaciones" v={relajacionesDe(targets.relaxations).join(', ') || 'ninguna'} />
        )}
        {/* El paro fisico y el Detener en pantalla llegan los dos como
            "stopped": sin esto no hay forma de distinguirlos. */}
        {resultado?.emergency !== undefined && (
          <D t="Detención" v={resultado.emergency ? 'Paro de emergencia' : 'Botón en pantalla'} />
        )}
        {resultado?.source && (
          <D t="Cortó por" v={`${resultado.source} (${nombreTipoAlerta(resultado.type ?? '')} ${resultado.count}/${resultado.limit})`} />
        )}
      </dl>
    </section>
  );
}

function D({ t, v }: { t: string; v: string }) {
  return (<><dt>{t}</dt><dd>{v}</dd></>);
}
