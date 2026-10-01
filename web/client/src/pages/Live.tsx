import { useEffect, useState } from 'react';
import {
  antiguedad, duracion, esCampoNulo, esViejo, horaCorta, nombreModo, nombreMotivo,
  nombreSalud, nombreTipoAlerta, numero,
} from '../lib/format.js';
import type { LiveState } from '../lib/useLive.js';
import { TipoCorrida } from '../components/TipoCorrida.js';

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

  return (
    <>
      <Avisos estado={estado} ahora={ahora} />

      {!enCurso && <SinExperimento estado={estado} onVerCorrida={onVerCorrida} />}

      {enCurso && (
        <>
          <section className="panel">
            <div className="encabezado-run">
              <Salud health={status?.health} />
              <Modo mode={targets?.mode} />
              <TipoCorrida runType={run.runType} />
            </div>
            <Progreso status={status} startedAt={run.startedAt} />
          </section>

          <section className="grilla-mediciones">
            <Medicion
              titulo="Campo magnetico"
              valor={numero(measures?.magneticField, 3)}
              unidad="mT"
              objetivo={targets?.cem !== undefined
                ? `objetivo ${numero(targets.cem, 2)} mT ±${numero(targets.tol, 0)}%`
                : undefined}
              ts={measures?.ts}
              ahora={ahora}
            />
            <Medicion
              titulo="Temperatura"
              valor={numero(measures?.temperature, 1)}
              unidad="°C"
              objetivo={targets?.tnmin !== undefined
                ? `normal ${numero(targets.tnmin, 0)}–${numero(targets.tnmax, 0)} °C`
                : undefined}
              ts={measures?.ts}
              ahora={ahora}
            />
          </section>

          <Bobinas coils={coils} ahora={ahora} />
          <Objetivos targets={targets} />
        </>
      )}

      <Alertas alerts={alerts} enCurso={enCurso} />
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
  return (
    <section className="panel vacio">
      <h2>Sin experimento en curso</h2>
      {resultado ? (
        <div className={`resultado resultado-${resultado.reason}`}>
          <p className="resultado-motivo">{nombreMotivo(resultado.reason)}</p>
          {resultado.description && <p className="resultado-detalle">{resultado.description}</p>}
          <p className="resultado-datos">
            {resultado.progressPercent !== undefined && `${resultado.progressPercent}% completado`}
            {resultado.elapsedSeconds !== undefined && ` · ${duracion(resultado.elapsedSeconds)}`}
            {resultado.meanMagneticField !== undefined
              && ` · campo medio ${numero(resultado.meanMagneticField, 3)} mT`}
          </p>
          {resultado.runId && (
            <button type="button" className="boton"
              onClick={() => onVerCorrida(resultado.runId!)}>
              Ver el detalle de esta corrida
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
      <div className="barra"><div className="barra-relleno" style={{ width: `${porcentaje}%` }} /></div>
      <div className="progreso-datos">
        <span><strong>{status?.progress ?? '--'}%</strong></span>
        <span>transcurrido <strong>{duracion(status?.elapsedSeconds)}</strong></span>
        <span>restante <strong>{duracion(status?.remainingSeconds)}</strong></span>
        <span className="tenue">inicio {horaCorta(startedAt)}</span>
      </div>
    </div>
  );
}

function Medicion({ titulo, valor, unidad, objetivo, ts, ahora }: {
  titulo: string; valor: string; unidad: string; objetivo?: string; ts?: string; ahora: number;
}) {
  return (
    <article className={`panel medicion ${esViejo(ts, ahora) ? 'viejo' : ''}`}>
      <h3>{titulo}</h3>
      <p className="medicion-valor">{valor}<span className="medicion-unidad">{unidad}</span></p>
      {objetivo && <p className="medicion-objetivo">{objetivo}</p>}
      <p className="tenue">{antiguedad(ts, ahora)}</p>
    </article>
  );
}

// Una fila por bobina que REPORTO, no siempre cuatro: cuantas bobinas hay
// montadas es un hecho de la placa de control, y mostrar cuatro filas en cero
// inventaria bobinas que no existen.
function Bobinas({ coils, ahora }: { coils: LiveState['coils']; ahora: number }) {
  if (!coils || coils.coils.length === 0) return null;
  return (
    <section className={`panel ${esViejo(coils.ts, ahora) ? 'viejo' : ''}`}>
      <h3>Bobinas</h3>
      <table className="tabla">
        <thead><tr><th>Bobina</th><th>Corriente</th><th>Duty</th></tr></thead>
        <tbody>
          {coils.coils.map((coil) => (
            <tr key={coil.n}>
              <td>B{coil.n}</td>
              <td>{numero(coil.current, 2)} A</td>
              <td>{numero(coil.duty, 1)} %</td>
            </tr>
          ))}
        </tbody>
      </table>
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
        <Dato termino="Duracion" valor={targets.dur !== undefined ? `${numero(targets.dur, 0)} min` : '--'} />
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

function Alertas({ alerts, enCurso }: { alerts: LiveState['alerts']; enCurso: boolean }) {
  if (!enCurso && alerts.length === 0) return null;
  return (
    <section className="panel">
      <h3>Alertas</h3>
      {alerts.length === 0 ? (
        <p className="empty">Sin alertas en esta corrida.</p>
      ) : (
        <ul className="alertas">
          {alerts.map((alert, index) => (
            <li key={`${alert.ts}-${index}`} className={`alerta alerta-${alert.type}`}>
              <span className="alerta-fuente">{alert.source}</span>
              <span className="alerta-tipo">{nombreTipoAlerta(alert.type)}</span>
              <span className="alerta-cuenta">{alert.count}/{alert.limit}</span>
              <time className="tenue">{horaCorta(alert.ts)}</time>
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
