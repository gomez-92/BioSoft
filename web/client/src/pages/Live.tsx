import { Suspense, lazy, useEffect, useState } from 'react';
import {
  antiguedad, avanceFinal, duracion, duracionPedida, esCampoNulo, esViejo, horaCorta, nombreModo, nombreMotivo,
  nombreSalud, numero, relajacionesConAviso,
} from '../lib/format.js';
import { bobinasHabilitadas, filtrarBobinas, sinCorriente } from '../lib/bobinas.js';
import { useConfigDeCorrida } from '../lib/useConfig.js';
import type { LiveState } from '../lib/useLive.js';
import { ListaAlertas } from '../components/ListaAlertas.js';
import { TipoCorrida } from '../components/TipoCorrida.js';
import { multiplicadorDe, rangosCampo, rangosTemperatura, type Rangos } from '../lib/rangos.js';
import { TEXTO_ZONA, escalaDe, zonaDe } from '../lib/zonas.js';

// Los graficos traen recharts: se cargan aparte, y los numeros de arriba se
// ven sin esperarlos.
const GraficosEnVivo = lazy(() => import('../components/GraficosEnVivo.js'));

// Pantalla En curso. La que se mira desde afuera del laboratorio, casi siempre
// desde un celular, para contestar una sola pregunta: el experimento, ¿va bien?

export function Live({ estado, onVerCorrida }: {
  estado: LiveState; onVerCorrida: (id: string) => void;
}) {
  // Un reloj propio para poder envejecer los datos en pantalla. Sin esto, un
  // valor de hace diez minutos se ve igual de fresco que uno de recien.
  const [ahora, setAhora] = useState(() => Date.now());
  useEffect(() => {
    const id = setInterval(() => setAhora(Date.now()), 1000);
    return () => clearInterval(id);
  }, []);

  const { run, targets, measures, coils, status, alerts } = estado;
  const enCurso = run !== null;
  // La configuracion con la que corre: dice que bobinas estan habilitadas y
  // los umbrales de las reglas (para el detalle de las alertas).
  const config = useConfigDeCorrida(targets?.configId);
  const habilitadas = bobinasHabilitadas(config);

  return (
    <>
      <Avisos estado={estado} ahora={ahora} />

      {!enCurso && <SinExperimento estado={estado} onVerCorrida={onVerCorrida} />}

      {enCurso && (
        <>
          <section className="panel cabecera-run">
            <div className="cabecera-run-titulo">
              <span className={`latido latido-${status?.health ?? 'desconocido'}`} aria-hidden="true" />
              <h2>{run.runType === 'test' ? 'Prueba en curso' : 'Experimento en curso'}</h2>
              <TipoCorrida runType={run.runType} />
            </div>
            {/* Cada chip con su rotulo: "Normal" suelto no decia que era la
                salud, ni "CAMPO X" que era el modo de exposicion. */}
            <div className="encabezado-run">
              <span className="rotulo">Salud <Salud health={status?.health} /></span>
              {targets?.mode && <span className="rotulo">Modo <Modo mode={targets.mode} /></span>}
            </div>
            <Progreso status={status} startedAt={run.startedAt} />
          </section>

          <div className="en-curso">
            <div className="en-curso-principal">
              <section className="grilla-mediciones">
                <Medicion
                  titulo="Campo magnetico"
                  valor={numero(measures?.magneticField, 3)}
                  crudo={measures?.magneticField}
                  rangos={rangosCampo(targets, multiplicadorDe(config))}
                  piso={0}
                  decimales={3}
                  unidad="mT"
                  objetivo={targets?.cem !== undefined
                    ? (esCampoNulo(targets.mode)
                      ? `objetivo 0 mT (campo nulo) ±${numero((targets.cem * (targets.tol ?? 0)) / 100, 3)} mT`
                      : `objetivo ${numero(targets.cem, 2)} mT ±${numero(targets.tol, 0)}%`)
                    : undefined}
                  ts={measures?.ts}
                  ahora={ahora}
                />
                <Medicion
                  titulo="Temperatura"
                  valor={numero(measures?.temperature, 1)}
                  crudo={measures?.temperature}
                  rangos={rangosTemperatura(targets)}
                  decimales={0}
                  unidad="°C"
                  objetivo={targets?.tnmin !== undefined
                    ? `normal ${numero(targets.tnmin, 0)}–${numero(targets.tnmax, 0)} °C`
                    : undefined}
                  ts={measures?.ts}
                  ahora={ahora}
                />
              </section>

              <section className="panel">
                <h3>Evolucion <span className="h3-nota">banda verde: rango normal · lineas rojas: limite critico</span></h3>
                <Suspense fallback={<p className="empty">Cargando graficos...</p>}>
                  <GraficosEnVivo runId={run.id} startedAt={run.startedAt} targets={targets} config={config}
                    measures={measures} coils={coils} alerts={alerts} habilitadas={habilitadas} ahora={ahora} />
                </Suspense>
                <p className="tenue">
                  Cada punto es una alerta, en el grafico de su magnitud: pasa por encima (o tocalo) para ver
                  el detalle. Un anillo marca la que corto el experimento.
                </p>
              </section>
            </div>

            <div className="en-curso-lateral">
              <Bobinas coils={coils} habilitadas={habilitadas} ahora={ahora} />
              <section className="panel">
                <h3>Alertas{alerts.length > 0 ? ` (${alerts.length})` : ''}</h3>
                <ListaAlertas alertas={alerts} targets={targets} config={config} inicio={run.startedAt}
                  vacia="Sin alertas en esta corrida." />
              </section>
              <Objetivos targets={targets} />
            </div>
          </div>
        </>
      )}
    </>
  );
}

// Los avisos van arriba de todo porque cambian el significado de TODO lo que
// esta debajo: con el enlace caido, los numeros siguen ahi pero ya no
// describen lo que esta pasando.
function Avisos({ estado, ahora }: { estado: LiveState; ahora: number }) {
  const avisos: string[] = [];
  if (!estado.conectado) {
    avisos.push('Sin conexion con el servidor. Los valores son los ultimos recibidos.');
  } else if (!estado.brokerOk) {
    avisos.push('El servidor perdio la conexion con el broker: no esta llegando telemetria.');
  }
  // El enlace serie entre la ESP32 y el Mega: desde afuera es la diferencia
  // entre "el experimento va bien" y "hace rato que no sabemos nada de la
  // placa que lo controla".
  if (estado.status?.megaOk === false) {
    avisos.push('La pantalla perdio el enlace con la placa de control (Mega).');
  }
  // Un experimento declarado que corre con protecciones apagadas o datos
  // simulados: quien mira desde afuera tiene que saberlo antes de leer los
  // numeros.
  const relajaciones = relajacionesConAviso(estado.targets?.relaxations);
  if (estado.run?.runType === 'normal' && relajaciones.length > 0) {
    avisos.push(`Experimento con relajaciones activas: ${relajaciones.join(', ')}.`);
  }
  if (estado.run && esViejo(estado.status?.ts, ahora)) {
    avisos.push(`No llegan datos ${antiguedad(estado.status?.ts, ahora)}.`);
  }

  if (avisos.length === 0) return null;
  return (
    <div className="avisos">
      {avisos.map((aviso) => <p key={aviso} className="aviso">{aviso}</p>)}
    </div>
  );
}

function SinExperimento({ estado, onVerCorrida }: {
  estado: LiveState; onVerCorrida: (id: string) => void;
}) {
  const resultado = estado.ultimoResultado;
  // Los objetivos siguen en el estado tras el cierre: de ahi sale la duracion.
  const final = avanceFinal(resultado, estado.targets?.dur);
  return (
    <section className="panel vacio">
      <h2>Sin experimento en curso</h2>
      {resultado ? (
        <div className={`resultado resultado-${resultado.reason}`}>
          <p className="tenue">Ultima corrida{resultado.ts ? `, terminada ${new Date(resultado.ts).toLocaleString('es-AR')}` : ''}</p>
          {resultado.runType && <div className="encabezado-run centrado"><TipoCorrida runType={resultado.runType} /></div>}
          <p className="resultado-motivo">{nombreMotivo(resultado.reason)}</p>
          {resultado.description && <p className="resultado-detalle">{resultado.description}</p>}
          <p className="resultado-datos">
            {final.porcentaje !== undefined && `${final.porcentaje}% completado`}
            {final.segundos !== undefined && ` · ${duracion(final.segundos)}`}
            {resultado.meanMagneticField !== undefined
              && ` · campo medio ${numero(resultado.meanMagneticField, 3)} mT`}
          </p>
          {resultado.runId && (
            <button type="button" className="boton"
              onClick={() => onVerCorrida(resultado.runId!)}>
              Ver el detalle y los graficos de esta corrida
            </button>
          )}
        </div>
      ) : (
        <p className="empty">Cuando arranque uno, esta pantalla se actualiza sola.</p>
      )}
    </section>
  );
}

function Salud({ health }: { health?: string }) {
  const clase = health ?? 'desconocido';
  return <span className={`chip chip-${clase}`}>{nombreSalud(health)}</span>;
}

// El modo es lo unico que distingue el grupo tratado del grupo control, y
// desde afuera no hay ninguna otra forma de saberlo: un campo nulo se ve, en
// todo lo demas, igual que un experimento normal.
function Modo({ mode }: { mode?: string }) {
  if (!mode) return null;
  const esNulo = esCampoNulo(mode);
  return <span className={`chip chip-modo ${esNulo ? 'chip-nulo' : ''}`}>{nombreModo(mode)}</span>;
}

function Progreso({ status, startedAt }: { status: LiveState['status']; startedAt: string }) {
  const porcentaje = Math.min(100, Math.max(0, status?.progress ?? 0));
  return (
    <div className="progreso">
      <div className="barra" role="progressbar" aria-valuenow={porcentaje} aria-valuemin={0} aria-valuemax={100}>
        <div className="barra-relleno" style={{ width: `${porcentaje}%` }} />
      </div>
      <dl className="progreso-datos">
        <div><dt>Avance</dt><dd>{status?.progress ?? '--'}%</dd></div>
        <div><dt>Transcurrido</dt><dd>{duracion(status?.elapsedSeconds)}</dd></div>
        <div><dt>Restante</dt><dd>{duracion(status?.remainingSeconds)}</dd></div>
        <div><dt>Inicio</dt><dd>{horaCorta(startedAt)}</dd></div>
      </dl>
    </div>
  );
}

function Medicion({ titulo, valor, crudo, rangos, piso, decimales, unidad, objetivo, ts, ahora }: {
  titulo: string; valor: string; crudo?: number; rangos: Rangos; piso?: number; decimales: number;
  unidad: string; objetivo?: string; ts?: string; ahora: number;
}) {
  const zona = zonaDe(crudo, rangos);
  const escala = escalaDe(rangos, piso);
  const fmt = (v: number) => numero(v, decimales);
  return (
    <article className={`panel medicion ${zona ? `medicion-${zona}` : ''} ${esViejo(ts, ahora) ? 'viejo' : ''}`}>
      <h3>{titulo}</h3>
      <p className="medicion-valor">{valor}<span className="medicion-unidad">{unidad}</span></p>
      {zona && <p className={`medicion-zona zona-${zona}`}>{TEXTO_ZONA[zona]}</p>}
      {escala && (
        // La regla: zonas critica / advertencia / normal y una marca donde
        // esta el valor. Se entiende sin saber que es un "rango derivado".
        <div className="escala" aria-hidden="true">
          <div className="escala-pista">
            <span className="escala-tramo tramo-critico" style={{ left: 0, width: `${escala.pos(escala.critico[0])}%` }} />
            <span className="escala-tramo tramo-advertencia" style={{ left: `${escala.pos(escala.critico[0])}%`, width: `${escala.pos(escala.normal[0]) - escala.pos(escala.critico[0])}%` }} />
            <span className="escala-tramo tramo-normal" style={{ left: `${escala.pos(escala.normal[0])}%`, width: `${escala.pos(escala.normal[1]) - escala.pos(escala.normal[0])}%` }} />
            <span className="escala-tramo tramo-advertencia" style={{ left: `${escala.pos(escala.normal[1])}%`, width: `${escala.pos(escala.critico[1]) - escala.pos(escala.normal[1])}%` }} />
            <span className="escala-tramo tramo-critico" style={{ left: `${escala.pos(escala.critico[1])}%`, right: 0 }} />
            {crudo !== undefined && <span className="escala-marca" style={{ left: `${escala.pos(crudo)}%` }} />}
          </div>
          <div className="escala-etiquetas">
            <span style={{ left: `${escala.pos(escala.normal[0])}%` }}>{fmt(escala.normal[0])}</span>
            <span style={{ left: `${escala.pos(escala.normal[1])}%` }}>{fmt(escala.normal[1])}</span>
          </div>
        </div>
      )}
      {objetivo && <p className="medicion-objetivo">{objetivo}</p>}
      <p className="tenue">{antiguedad(ts, ahora)}</p>
    </article>
  );
}

// Una fila por bobina que REPORTO y que la configuracion tiene habilitada (ver
// lib/bobinas.ts), no siempre cuatro: cuantas bobinas hay montadas es un
// hecho de la placa de control, y mostrar filas en cero inventaria bobinas
// que no existen.
function Bobinas({ coils, habilitadas, ahora }: {
  coils: LiveState['coils']; habilitadas: Set<number> | null; ahora: number;
}) {
  if (!coils || coils.coils.length === 0) return null;
  const visibles = filtrarBobinas(coils.coils, habilitadas);
  const ocultas = coils.coils.length - visibles.length;
  return (
    <section className={`panel ${esViejo(coils.ts, ahora) ? 'viejo' : ''}`}>
      <h3>Bobinas activas ({visibles.length})</h3>
      <table className="tabla">
        <thead><tr><th>Bobina</th><th>Corriente</th><th>Duty</th></tr></thead>
        <tbody>
          {visibles.map((coil) => (
            <tr key={coil.n}>
              <td>
                B{coil.n}
                {sinCorriente(coil) && (
                  <span className="chip chip-warning chip-chico"
                    title="Se le aplica duty pero no circula corriente: ¿desconectada, sin montar o etapa de potencia apagada?">
                    sin corriente
                  </span>
                )}
              </td>
              <td>{coil.current === undefined ? <span className="tenue">sin sensor</span> : `${numero(coil.current, 2)} A`}</td>
              <td>{numero(coil.duty, 1)} %</td>
            </tr>
          ))}
        </tbody>
      </table>
      {ocultas > 0 && (
        <p className="tenue">
          {ocultas === 1
            ? 'Una bobina registrada en la placa esta deshabilitada en la configuracion y no se muestra.'
            : `${ocultas} bobinas registradas en la placa estan deshabilitadas en la configuracion y no se muestran.`}
        </p>
      )}
      <p className="tenue">{antiguedad(coils.ts, ahora)}</p>
    </section>
  );
}

function Objetivos({ targets }: { targets: LiveState['targets'] }) {
  if (!targets) return null;
  return (
    <section className="panel">
      <h3>Objetivos</h3>
      <dl className="datos">
        <Dato termino="Intensidad" valor={targets.cem !== undefined ? `${numero(targets.cem, 2)} mT` : '--'} />
        <Dato termino="Frecuencia" valor={targets.freq !== undefined ? `${numero(targets.freq, 0)} Hz` : '--'} />
        <Dato termino="Duracion" valor={duracionPedida(targets.dur)} />
        <Dato termino="Tolerancia" valor={targets.tol !== undefined ? `±${numero(targets.tol, 0)} %` : '--'} />
        <Dato termino="Temp. normal" valor={`${numero(targets.tnmin, 0)}–${numero(targets.tnmax, 0)} °C`} />
        <Dato termino="Temp. critica" valor={`${numero(targets.tcmin, 0)}–${numero(targets.tcmax, 0)} °C`} />
      </dl>
    </section>
  );
}

function Dato({ termino, valor }: { termino: string; valor: string }) {
  return (<><dt>{termino}</dt><dd>{valor}</dd></>);
}
