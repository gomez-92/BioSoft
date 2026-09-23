import { useEffect, useState } from 'react';
import { Link, useParams } from 'react-router-dom';
import {
  GraficoBobinas, GraficoCampo, GraficoTemperatura,
  type AlertaMarca, type PuntoMedicion,
} from '../components/Graficos.js';
import { authFetch } from '../lib/auth.js';
import { duracion, horaCorta, nombreMotivo, nombreTipoAlerta, numero } from '../lib/format.js';
import type { Targets } from '../lib/types.js';

// Detalle de una corrida: que se pidio, que paso y por que termino.

interface Detalle {
  id: string; startedAt: string; endedAt: string | null; state: string;
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
  const [alertas, setAlertas] = useState<Array<AlertaMarca & { count: number; limit: number }>>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!id) return;
    let vigente = true;

    authFetch(`/api/runs/${id}`)
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(r.status === 404 ? 'No existe esa corrida.' : 'No se pudo cargar.'))))
      .then((data) => { if (vigente) setDetalle(data); })
      .catch((cause) => { if (vigente) setError(cause.message); });

    authFetch(`/api/runs/${id}/measures`).then((r) => r.json())
      .then((data) => { if (vigente) setMediciones(data); }).catch(() => {});
    authFetch(`/api/runs/${id}/coils`).then((r) => r.json())
      .then((data) => { if (vigente) setBobinas(data); }).catch(() => {});
    authFetch(`/api/runs/${id}/alerts`).then((r) => r.json())
      .then((data) => { if (vigente) setAlertas(data.items); }).catch(() => {});

    return () => { vigente = false; };
  }, [id]);

  if (error) return <p className="empty panel">{error} <Link to="/historial">Volver</Link></p>;
  if (!detalle) return <p className="empty panel">Cargando...</p>;

  const inicio = new Date(detalle.startedAt).getTime();
  const fin = detalle.endedAt ? new Date(detalle.endedAt).getTime() : Date.now();
  const esNulo = detalle.targets?.mode?.toLowerCase().includes('nulo') ?? false;

  return (
    <>
      <section className="panel">
        <div className="encabezado-run">
          {detalle.targets?.mode && (
            <span className={`chip chip-modo ${esNulo ? 'chip-nulo' : ''}`}>
              {detalle.targets.mode.toUpperCase()}
            </span>
          )}
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
        {!detalle.result && detalle.state === 'orphan' && (
          <p className="aviso">
            Esta corrida nunca recibio su mensaje de cierre: el monitor no sabe como termino.
            Las mediciones que si llegaron son reales.
          </p>
        )}
      </section>

      <Objetivos targets={detalle.targets} resultado={detalle.result} />

      {mediciones && mediciones.points.length > 0 ? (
        <section className="panel">
          <GraficoCampo puntos={mediciones.points} bucket={mediciones.bucket}
            targets={detalle.targets} alertas={alertas} inicio={inicio} fin={fin} />
          <GraficoTemperatura puntos={mediciones.points} bucket={mediciones.bucket}
            targets={detalle.targets} alertas={alertas} inicio={inicio} fin={fin} />
          {alertas.length > 0 && (
            <p className="tenue">
              Las lineas punteadas verticales marcan cuando se levanto cada alerta.
            </p>
          )}
        </section>
      ) : (
        <p className="empty panel">Esta corrida no tiene mediciones guardadas.</p>
      )}

      {bobinas && bobinas.points.length > 0 && (
        <section className="panel">
          <GraficoBobinas puntos={bobinas.points} bucket={bobinas.bucket} bobinas={bobinas.coils}
            magnitud="corriente" inicio={inicio} fin={fin} />
          <GraficoBobinas puntos={bobinas.points} bucket={bobinas.bucket} bobinas={bobinas.coils}
            magnitud="duty" inicio={inicio} fin={fin} />
        </section>
      )}

      <Alertas alertas={alertas} />

      <section className="acciones">
        <BotonCsv id={detalle.id} />
      </section>
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
        {targets?.dur !== undefined && <D t="Duracion pedida" v={`${numero(targets.dur, 0)} min`} />}
        {targets?.tol !== undefined && <D t="Tolerancia" v={`±${numero(targets.tol, 0)} %`} />}
        {targets?.tnmin !== undefined && (
          <D t="Temp. normal" v={`${numero(targets.tnmin, 0)}–${numero(targets.tnmax, 0)} °C`} />
        )}
        {targets?.tcmin !== undefined && (
          <D t="Temp. critica" v={`${numero(targets.tcmin, 0)}–${numero(targets.tcmax, 0)} °C`} />
        )}
        {resultado?.progressPercent !== undefined && <D t="Avance" v={`${resultado.progressPercent} %`} />}
        {/* El paro fisico y el Detener en pantalla llegan los dos como
            "stopped": sin esto no hay forma de distinguirlos. */}
        {resultado?.emergency !== undefined && (
          <D t="Detencion" v={resultado.emergency ? 'Paro de emergencia' : 'Boton en pantalla'} />
        )}
        {resultado?.source && (
          <D t="Corto por" v={`${resultado.source} (${nombreTipoAlerta(resultado.type ?? '')} ${resultado.count}/${resultado.limit})`} />
        )}
      </dl>
    </section>
  );
}

function D({ t, v }: { t: string; v: string }) {
  return (<><dt>{t}</dt><dd>{v}</dd></>);
}

function Alertas({ alertas }: { alertas: Array<AlertaMarca & { count: number; limit: number }> }) {
  if (alertas.length === 0) return null;
  return (
    <section className="panel">
      <h3>Alertas ({alertas.length})</h3>
      <ul className="alertas">
        {alertas.map((alerta, index) => (
          <li key={index} className={`alerta alerta-${alerta.type}`}>
            <span className="alerta-fuente">{alerta.source}</span>
            <span className="alerta-tipo">{nombreTipoAlerta(alerta.type)}</span>
            <span className="alerta-cuenta">{alerta.count}/{alerta.limit}</span>
            <time className="tenue">{horaCorta(alerta.ts)}</time>
          </li>
        ))}
      </ul>
    </section>
  );
}
