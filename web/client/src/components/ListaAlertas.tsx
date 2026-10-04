import { useState } from 'react';
import { detalleAlerta, nombreFuente } from '../lib/alertas.js';
import { horaCorta, nombreTipoAlerta } from '../lib/format.js';
import type { AlertItem, Targets } from '../lib/types.js';

// Lista de alertas con su detalle desplegable. La misma en En curso y en el
// detalle de una corrida; el texto del detalle sale de lib/alertas.ts, el
// mismo que muestra el globo del grafico.

export function ListaAlertas({ alertas, targets, config, inicio, vacia }: {
  alertas: AlertItem[]; targets: Targets | null; config?: unknown; inicio?: string | number; vacia?: string;
}) {
  const [abierta, setAbierta] = useState<string | null>(null);

  if (alertas.length === 0) return <p className="empty">{vacia ?? 'Sin alertas.'}</p>;

  return (
    <ul className="alertas">
      {alertas.map((alerta, index) => {
        const id = `${alerta.ts}|${alerta.source}|${alerta.type}|${index}`;
        const detalle = abierta === id ? detalleAlerta(alerta, { targets, config, inicio }) : null;
        return (
          <li key={id} className={`alerta alerta-${alerta.type} ${alerta.count >= alerta.limit ? 'alerta-corte' : ''}`}>
            <button type="button" className="alerta-cabecera" aria-expanded={abierta === id}
              onClick={() => setAbierta(abierta === id ? null : id)}>
              <span className="alerta-fuente" title={nombreFuente(alerta.source)}>{alerta.source}</span>
              <span className="alerta-tipo">{nombreTipoAlerta(alerta.type)}</span>
              {alerta.count >= alerta.limit && <span className="chip chip-critical chip-chico">corto</span>}
              <span className="alerta-cuenta">{alerta.count}/{alerta.limit}</span>
              <time className="tenue">{horaCorta(alerta.ts)}</time>
              <span className="alerta-flecha" aria-hidden>{abierta === id ? '▴' : '▾'}</span>
            </button>
            {detalle && (
              <div className="alerta-detalle">
                <p className="alerta-detalle-titulo">{detalle.titulo}</p>
                {detalle.lineas.map((linea) => <p key={linea}>{linea}</p>)}
              </div>
            )}
          </li>
        );
      })}
    </ul>
  );
}
