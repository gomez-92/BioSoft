import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { authFetch } from '../lib/auth.js';
import { nombreCodigo } from '../lib/bitacora.js';
import {
  avanceFinal, calidadSenal, duracion, esCampoNulo, horaCorta, motivoConfig, nombreModo, nombreMotivo,
  nombreTipoAlerta, numero, numeroCorrida,
} from '../lib/format.js';
import { useSesion } from '../lib/sesion.js';
import type { Estadisticas } from '../lib/types.js';
import type { LiveState } from '../lib/useLive.js';
import { Icono } from '../components/Icono.js';
import { TipoCorrida } from '../components/TipoCorrida.js';
import { Avisos, Progreso, Salud } from './Live.js';

// Inicio: el estado del sistema de un vistazo. Lo que esta pasando ahora
// (corrida abierta, monitor, placa), como termino la ultima corrida y los
// numeros acumulados. La corrida en vivo tiene su propia pagina (En curso),
// que solo aparece en el menu mientras hay una.

export function Inicio({ estado }: { estado: LiveState }) {
  const sesion = useSesion();
  const esAdmin = sesion?.role === 'admin';
  const [ahora, setAhora] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setAhora(Date.now()), 1000);
    return () => clearInterval(id);
  }, []);

  // Las estadisticas cambian cuando empieza o termina una corrida: se piden
  // al entrar y en cada uno de esos dos momentos, no con un sondeo.
  const [stats, setStats] = useState<Estadisticas | null>(null);
  const [errorStats, setErrorStats] = useState<string | null>(null);
  const claveCorrida = `${estado.run?.id ?? ''}|${estado.ultimoResultado?.runId ?? ''}`;
  useEffect(() => {
    let vigente = true;
    authFetch('/api/stats')
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`error ${r.status}`))))
      .then((data: Estadisticas) => { if (vigente) { setStats(data); setErrorStats(null); } })
      .catch(() => { if (vigente) setErrorStats('No se pudieron cargar las estadísticas.'); });
    return () => { vigente = false; };
  }, [claveCorrida]);

  return (
    <>
      <Avisos estado={estado} ahora={ahora} />
      {estado.run && <CorridaAbierta estado={estado} />}

      <div className="columnas-2">
        <EstadoSistema estado={estado} />
        {!estado.run && <UltimaCorrida estado={estado} />}
        {estado.run && stats && <ResumenExperimentos stats={stats} />}
      </div>

      {errorStats && <p className="aviso panel-aviso">{errorStats}</p>}
      {stats && (
        <>
          {!estado.run && <ResumenExperimentos stats={stats} />}
          <div className="columnas-2">
            <Desenlaces stats={stats} />
            <AlertasFrecuentes stats={stats} />
          </div>
          {esAdmin && stats.equipo && <SaludEquipo equipo={stats.equipo} />}
        </>
      )}
    </>
  );
}

function CorridaAbierta({ estado }: { estado: LiveState }) {
  const { run, status, targets } = estado;
  if (!run) return null;
  return (
    <section className="panel cabecera-run inicio-corrida">
      <div className="cabecera-run-titulo">
        <span className={`latido latido-${status?.health ?? 'desconocido'}`} aria-hidden="true" />
        <h2>
          {run.runType === 'test' ? 'Prueba en curso' : 'Experimento en curso'}
          {numeroCorrida(run.number) && <span className="numero-corrida">{numeroCorrida(run.number)}</span>}
        </h2>
        <TipoCorrida runType={run.runType} />
        <Link to="/en-curso" className="boton boton-primario inicio-ver">Ver en curso</Link>
      </div>
      <div className="encabezado-run">
        <span className="rotulo">Salud <Salud health={status?.health} /></span>
        {targets?.mode && (
          <span className="rotulo">Modo{' '}
            <span className={`chip chip-modo ${esCampoNulo(targets.mode) ? 'chip-nulo' : ''}`}>{nombreModo(targets.mode)}</span>
          </span>
        )}
      </div>
      <Progreso status={status} startedAt={run.startedAt} />
    </section>
  );
}

const NOMBRE_APP: Record<string, string> = {
  idle: 'iniciando', ready: 'lista', starting: 'arrancando', running: 'experimento en curso',
  stopping: 'deteniendo',
};

/** Una fila de estado: punto de color, que es y como esta. */
function Fila({ ok, titulo, valor, detalle }: {
  ok: boolean | null; titulo: string; valor: string; detalle?: string;
}) {
  const clase = ok === null ? 'punto-desconocido' : ok ? 'punto-ok' : 'punto-caido';
  return (
    <div className="estado-fila">
      <span className={`punto ${clase}`} aria-hidden="true" />
      <div>
        <p className="estado-titulo">{titulo} <span className="estado-valor">{valor}</span></p>
        {detalle && <p className="tenue estado-detalle">{detalle}</p>}
      </div>
    </div>
  );
}

function EstadoSistema({ estado }: { estado: LiveState }) {
  const { placa } = estado;
  const respondio = placa?.estado === 'conectada';
  const senal = calidadSenal(placa?.rssi);
  const config = placa?.configId ? motivoConfig(placa.configId, placa.configStatus) : null;
  return (
    <section className="panel">
      <h3><Icono nombre="placa" /> Estado del sistema</h3>
      <div className="estado-lista">
        <Fila ok={estado.conectado} titulo="Servidor del monitor"
          valor={estado.conectado ? 'conectado' : 'sin conexión'}
          detalle={estado.conectado ? undefined : 'Este navegador no llega al servidor: lo de abajo puede estar desactualizado.'} />
        <Fila ok={estado.conectado ? estado.brokerOk : null} titulo="Broker MQTT"
          valor={estado.brokerOk ? 'recibiendo telemetría' : 'sin conexión'} />
        <Fila ok={placa ? (placa.estado === 'desconocido' ? null : respondio) : null} titulo="Placa (pantalla ESP32)"
          valor={!placa || placa.estado === 'desconocido' ? 'sin verificar'
            : respondio ? (NOMBRE_APP[placa.appState ?? ''] ?? 'conectada') : 'sin respuesta'}
          detalle={[
            placa?.uptimeSeconds != null ? `encendida hace ${duracion(placa.uptimeSeconds)}` : '',
            placa?.ultimaRespuesta ? `última respuesta ${horaCorta(placa.ultimaRespuesta)}` : '',
          ].filter(Boolean).join(' · ') || undefined} />
        <Fila ok={respondio ? placa?.mega ?? null : null} titulo="Placa de control (Mega)"
          valor={!respondio || placa?.mega == null ? 'sin datos' : placa.mega ? 'conectada' : 'NO conectada'} />
        {respondio && placa?.network && (
          <Fila ok={senal ? !senal.debil : null} titulo="Red WiFi"
            valor={placa.network}
            detalle={senal ? `señal ${senal.texto} (${placa.rssi} dBm)` : undefined} />
        )}
        {config && (
          <Fila ok={!config.aviso} titulo="Configuración"
            valor={placa?.configId === 'default' ? 'valores de fábrica' : placa?.configId ?? ''}
            detalle={config.texto} />
        )}
      </div>
    </section>
  );
}

function UltimaCorrida({ estado }: { estado: LiveState }) {
  const resultado = estado.ultimoResultado;
  // Los objetivos siguen en el estado tras el cierre: de ahi sale la duracion.
  const final = avanceFinal(resultado, estado.targets?.dur);
  return (
    <section className="panel">
      <h3><Icono nombre="historial" /> Última corrida</h3>
      {!estado.snapshotRecibido ? <p className="empty">Cargando...</p> : resultado ? (
        <div className={`resultado resultado-${resultado.reason} ultima-corrida`}>
          <div className="encabezado-run">
            {numeroCorrida(resultado.runNumber) && <span className="chip chip-numero">Corrida {numeroCorrida(resultado.runNumber)}</span>}
            {resultado.runType && <TipoCorrida runType={resultado.runType} />}
          </div>
          <p className="resultado-motivo">{nombreMotivo(resultado.reason)}</p>
          {resultado.description && <p className="resultado-detalle">{resultado.description}</p>}
          <p className="resultado-datos">
            {resultado.ts ? `Terminó ${new Date(resultado.ts).toLocaleString('es-AR')}` : ''}
            {final.porcentaje !== undefined && ` · ${final.porcentaje}% completado`}
            {final.segundos !== undefined && ` · ${duracion(final.segundos)}`}
            {resultado.meanMagneticField !== undefined
              && ` · campo medio ${numero(resultado.meanMagneticField, 3)} mT`}
          </p>
          {resultado.runId && (
            <Link to={`/corrida/${resultado.runId}`} className="boton">Ver el detalle y los gráficos</Link>
          )}
        </div>
      ) : (
        <p className="empty">Todavía no hay corridas. Cuando arranque una, aparece En curso en el menú.</p>
      )}
    </section>
  );
}

function Kpi({ valor, rotulo, detalle }: { valor: string; rotulo: string; detalle?: string }) {
  return (
    <div className="kpi">
      <p className="kpi-valor">{valor}</p>
      <p className="kpi-rotulo">{rotulo}</p>
      {detalle && <p className="tenue kpi-detalle">{detalle}</p>}
    </div>
  );
}

function plural(n: number, uno: string, varios: string): string {
  return `${n} ${n === 1 ? uno : varios}`;
}

function ResumenExperimentos({ stats }: { stats: Estadisticas }) {
  const e = stats.experimentos;
  return (
    <section className="panel">
      <h3><Icono nombre="vivo" /> Experimentos</h3>
      <div className="kpis">
        <Kpi valor={String(e.total)} rotulo="experimentos"
          detalle={`${e.recientes} en los últimos ${stats.ventanaDias} días`} />
        <Kpi valor={numero(e.horasExposicion, 1)} rotulo="horas de exposición" />
        <Kpi valor={`${e.tratado} / ${e.control}`} rotulo="tratado / control"
          detalle="campo X / campo nulo" />
        <Kpi valor={String(stats.pruebas.total)} rotulo="pruebas"
          detalle={`aparte; ${stats.pruebas.recientes} en ${stats.ventanaDias} días`} />
      </div>
      <p className="tenue">
        {e.ultimoInicio ? `El último experimento empezó el ${new Date(e.ultimoInicio).toLocaleString('es-AR')}.` : 'Todavía no hay experimentos.'}
        {e.sinMarca > 0 && ` ${plural(e.sinMarca, 'corrida no dice', 'corridas no dicen')} si fue experimento o prueba (firmware anterior a la marca): se cuentan como experimento.`}
        {' '}<Link to="/historial">Ver el historial</Link>
      </p>
    </section>
  );
}

// Cada desenlace con su color: completado bien, detenido neutro, cortado y
// perdido mal. Es la forma rapida de ver si el equipo es confiable.
const DESENLACES: Array<{ clave: keyof Estadisticas['experimentos']['desenlaces']; nombre: string; clase: string }> = [
  { clave: 'completed', nombre: 'Completados', clase: 'barra-ok' },
  { clave: 'stopped', nombre: 'Detenidos', clase: 'barra-neutra' },
  { clave: 'critical', nombre: 'Cortados por alerta', clase: 'barra-mal' },
  { clave: 'lost', nombre: 'Placa de control perdida', clase: 'barra-mal' },
  { clave: 'sinCierre', nombre: 'Sin cierre', clase: 'barra-aviso' },
  { clave: 'otro', nombre: 'Sin motivo informado', clase: 'barra-aviso' },
];

function Barras({ filas }: { filas: Array<{ nombre: string; n: number; clase: string }> }) {
  const max = Math.max(1, ...filas.map((f) => f.n));
  return (
    <ul className="barras">
      {filas.map((f) => (
        <li key={f.nombre}>
          <span className="barras-nombre">{f.nombre}</span>
          <span className="barras-pista" aria-hidden="true">
            <span className={`barras-relleno ${f.clase}`} style={{ width: `${(f.n / max) * 100}%` }} />
          </span>
          <span className="barras-n">{f.n}</span>
        </li>
      ))}
    </ul>
  );
}

function Desenlaces({ stats }: { stats: Estadisticas }) {
  const d = stats.experimentos.desenlaces;
  const filas = DESENLACES.map((x) => ({ nombre: x.nombre, n: d[x.clave], clase: x.clase }))
    .filter((f, i) => f.n > 0 || i < 3);
  const cerrados = Object.values(d).reduce((a, b) => a + b, 0);
  return (
    <section className="panel">
      <h3>Cómo terminaron</h3>
      {cerrados === 0 ? <p className="empty">Todavía no terminó ningún experimento.</p> : (
        <>
          <Barras filas={filas} />
          <p className="tenue">
            {Math.round((d.completed / cerrados) * 100)}% de los experimentos terminados llegó al final.
          </p>
        </>
      )}
    </section>
  );
}

function AlertasFrecuentes({ stats }: { stats: Estadisticas }) {
  return (
    <section className="panel">
      <h3>Alertas más frecuentes</h3>
      {stats.alertas.length === 0 ? <p className="empty">Ningún experimento tuvo alertas.</p> : (
        <Barras filas={stats.alertas.map((a) => ({
          nombre: `${a.source} · ${nombreTipoAlerta(a.type)}`,
          n: a.cantidad,
          clase: a.type === 'critical' || a.type === 'silence' ? 'barra-mal' : 'barra-aviso',
        }))} />
      )}
      <p className="tenue">Solo experimentos: las pruebas no cuentan.</p>
    </section>
  );
}

// Los eventos de la bitacora que, si se repiten, dicen que algo del equipo
// anda mal. El arranque solo, no: se muestra la fecha del ultimo.
const EVENTOS_EQUIPO = [
  'mega_reinicio', 'enlace_caido', 'broker_caido', 'corrida_perdida', 'mega_margen_bajo', 'cola_llena', 'config_ignorada',
];

function SaludEquipo({ equipo }: { equipo: NonNullable<Estadisticas['equipo']> }) {
  const total = EVENTOS_EQUIPO.reduce((a, c) => a + (equipo.eventos[c] ?? 0), 0);
  return (
    <section className="panel">
      <h3><Icono nombre="bitacora" /> Salud del equipo <span className="tenue-celda">últimos {equipo.dias} días</span></h3>
      <div className="kpis kpis-chicos">
        {EVENTOS_EQUIPO.map((codigo) => {
          const n = equipo.eventos[codigo] ?? 0;
          return (
            <div key={codigo} className={`kpi ${n > 0 ? 'kpi-aviso' : ''}`}>
              <p className="kpi-valor">{n}</p>
              <p className="kpi-rotulo">{nombreCodigo(codigo)}</p>
            </div>
          );
        })}
      </div>
      <p className="tenue">
        {total === 0 ? 'Sin problemas registrados en la bitácora. ' : ''}
        {equipo.eventos.arranque > 0 && `La placa arrancó ${plural(equipo.eventos.arranque, 'vez', 'veces')}. `}
        {equipo.ultimoArranque && `Último arranque: ${new Date(equipo.ultimoArranque).toLocaleString('es-AR')}. `}
        <Link to="/bitacora">Ver la bitácora</Link>
      </p>
    </section>
  );
}
