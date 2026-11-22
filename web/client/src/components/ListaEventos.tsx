import { Link } from 'react-router-dom';
import { NIVELES, nombreCodigo, nombreCorrida, type EventoPlaca } from '../lib/bitacora.js';

// Eventos de la bitacora de la placa. La misma lista en la pagina Bitacora y
// en el detalle de una corrida (los eventos de su ventana de tiempo).

function fechaHora(iso: string, conFecha: boolean): string {
  const date = new Date(iso);
  const hora = date.toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  return conFecha ? `${date.toLocaleDateString('es-AR')} ${hora}` : hora;
}

// `conCorrida`: la etiqueta con la corrida en la que paso cada evento (en el
// detalle de una corrida sobra, ya se sabe cual es).
export function ListaEventos({ eventos, conFecha = true, conCorrida = true }: {
  eventos: EventoPlaca[]; conFecha?: boolean; conCorrida?: boolean;
}) {
  return (
    <ul className="eventos">
      {eventos.map((evento) => {
        const nivel = NIVELES[evento.level] ?? NIVELES.I;
        return (
          <li key={evento.id} className={`evento evento-${evento.level}`}>
            <div className="evento-cabecera">
              <span className={`chip chip-chico ${nivel.clase}`}>{nivel.nombre}</span>
              <span className="evento-nombre" title={evento.code}>{nombreCodigo(evento.code)}</span>
              {conCorrida && evento.run && (
                <Link to={`/corrida/${evento.run.id}`} className="chip chip-chico chip-corrida"
                  title="Paso durante esta corrida: abrir su detalle">
                  Corrida {nombreCorrida(evento.run)}{evento.run.state === 'running' ? ' (en curso)' : ''}
                </Link>
              )}
              <time className="tenue-celda" dateTime={evento.ts}>{fechaHora(evento.ts, conFecha)}</time>
            </div>
            {evento.message && <p className="evento-texto">{evento.message}</p>}
            {evento.dropped > 0 && (
              <p className="evento-perdidos">
                Antes de este, la placa descarto {evento.dropped} evento{evento.dropped === 1 ? '' : 's'} por tener
                el buffer lleno (estuvo mucho tiempo sin broker).
              </p>
            )}
          </li>
        );
      })}
    </ul>
  );
}
