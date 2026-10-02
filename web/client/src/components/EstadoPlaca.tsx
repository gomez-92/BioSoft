import { useState } from 'react';
import { authFetch } from '../lib/auth.js';
import { duracion, horaCorta, motivoConfig } from '../lib/format.js';
import type { PlacaStatus } from '../lib/types.js';

// Estado de la placa segun el ping (server/src/domain/boardping.ts). Va al
// lado de "En linea", que solo dice que el monitor escucha al broker: esto
// dice si del otro lado hay una placa que contesta.

const NOMBRE_APP: Record<string, string> = {
  idle: 'iniciando', ready: 'lista', starting: 'arrancando', running: 'experimento en curso',
  stopping: 'deteniendo',
};

function detalle(placa: PlacaStatus): string {
  const lineas: string[] = [];
  if (placa.appState) lineas.push(`Estado: ${NOMBRE_APP[placa.appState] ?? placa.appState}`);
  if (placa.mega !== null) lineas.push(`Placa de control (Mega): ${placa.mega ? 'conectada' : 'NO conectada'}`);
  if (placa.configId) {
    lineas.push(`Configuracion: ${placa.configId === 'default' ? 'valores de fabrica' : placa.configId}`);
    lineas.push(`  ${motivoConfig(placa.configId, placa.configStatus).texto}`);
  }
  if (placa.uptimeSeconds !== null) lineas.push(`Encendida hace ${duracion(placa.uptimeSeconds)}`);
  if (placa.ultimaRespuesta) lineas.push(`Ultima respuesta: ${horaCorta(placa.ultimaRespuesta)}`);
  if (placa.ultimoPing) lineas.push(`Ultimo ping: ${horaCorta(placa.ultimoPing)}`);
  return lineas.join('\n');
}

export function EstadoPlaca({ placa, brokerOk }: { placa: PlacaStatus | null; brokerOk: boolean }) {
  const [esperando, setEsperando] = useState(false);
  const [local, setLocal] = useState<PlacaStatus | null>(null);
  // El socket trae el resultado igual; `local` cubre el caso de un socket
  // caido, para que el boton nunca parezca no haber hecho nada.
  const actual = placa ?? local;

  async function ping() {
    setEsperando(true);
    try {
      const r = await authFetch('/api/board/ping', { method: 'POST' });
      const data = await r.json().catch(() => null);
      if (data?.estado) setLocal(data);
    } finally {
      setEsperando(false);
    }
  }

  let clase = 'enlace-desconocido';
  let texto = 'Placa sin verificar';
  if (actual?.estado === 'conectada') {
    clase = actual.mega === false ? 'enlace-aviso' : 'enlace-ok';
    texto = `Placa conectada${actual.latenciaMs !== null ? ` · ${actual.latenciaMs} ms` : ''}`;
    if (actual.mega === false) texto += ' · sin Mega';
    // Una configuracion ignorada (o sin tarjeta) tambien se avisa aca: la
    // placa contesta, pero no corre lo que se le grabo.
    if (motivoConfig(actual.configId, actual.configStatus).aviso) {
      clase = 'enlace-aviso';
      texto += ' · revisar SD';
    }
  } else if (actual?.estado === 'sin_respuesta') {
    clase = 'enlace-caido';
    texto = 'Placa sin respuesta';
  }

  return (
    <span className="estado-placa">
      <span className={`enlace ${clase}`} title={actual ? detalle(actual) : undefined}>
        {esperando ? 'Ping...' : texto}
      </span>
      <button
        type="button"
        className="boton-ping"
        onClick={() => void ping()}
        disabled={esperando || !brokerOk}
        title={brokerOk ? 'Mandar un ping a la placa ahora' : 'Sin broker no se puede llegar a la placa'}
      >
        Ping
      </button>
    </span>
  );
}
