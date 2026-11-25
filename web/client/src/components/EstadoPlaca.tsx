import { useState } from 'react';
import { authFetch } from '../lib/auth.js';
import { calidadSenal, duracion, horaCorta, motivoConfig } from '../lib/format.js';
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
  if (placa.network) {
    const senal = calidadSenal(placa.rssi);
    lineas.push(`Red WiFi: ${placa.network}`);
    if (senal) lineas.push(`Señal: ${placa.rssi} dBm (${senal.texto})`);
  }
  if (placa.mega !== null) lineas.push(`Placa de control (Mega): ${placa.mega ? 'conectada' : 'NO conectada'}`);
  if (placa.configId) {
    lineas.push(`Configuración: ${placa.configId === 'default' ? 'valores de fábrica' : placa.configId}`);
    lineas.push(`  ${motivoConfig(placa.configId, placa.configStatus).texto}`);
  }
  if (placa.uptimeSeconds !== null) lineas.push(`Encendida hace ${duracion(placa.uptimeSeconds)}`);
  if (placa.ultimaRespuesta) lineas.push(`Última respuesta: ${horaCorta(placa.ultimaRespuesta)}`);
  if (placa.ultimoPing) lineas.push(`Último ping: ${horaCorta(placa.ultimoPing)}`);
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

  let clase = 'punto';
  let texto = 'sin verificar';
  if (actual?.estado === 'conectada') {
    clase = actual.mega === false ? 'punto-aviso' : 'punto-ok';
    texto = `conectada${actual.latenciaMs !== null ? ` · ${actual.latenciaMs} ms` : ''}`;
    if (actual.mega === false) texto += ' · sin Mega';
    // La red por la que esta y su senal: en la pantalla de la placa solo se
    // ve si hay WiFi o no.
    if (actual.network) {
      const senal = calidadSenal(actual.rssi);
      texto += ` · ${actual.network}${senal ? ` (señal ${senal.texto}, ${actual.rssi} dBm)` : ''}`;
      if (senal?.debil) clase = 'punto-aviso';
    }
    // Una configuracion ignorada (o sin tarjeta) tambien se avisa aca: la
    // placa contesta, pero no corre lo que se le grabo.
    if (motivoConfig(actual.configId, actual.configStatus).aviso) {
      clase = 'punto-aviso';
      texto += ' · revisar SD';
    }
  } else if (actual?.estado === 'sin_respuesta') {
    clase = 'punto-caido';
    texto = 'sin respuesta';
  }

  return (
    <span className="estado-placa">
      <span className="conexion" title={actual ? detalle(actual)
        : 'Todavía no se le preguntó a la placa si está ahí. "Probar" le manda un ping.'}>
        <span className={`punto ${clase}`} />
        <b>Placa</b>{esperando ? 'probando...' : texto}
      </span>
      <button
        type="button"
        className="boton-ping"
        onClick={() => void ping()}
        disabled={esperando || !brokerOk}
        title={brokerOk ? 'Preguntarle a la placa si está ahí (ping)' : 'Sin broker no se puede llegar a la placa'}
      >
        Probar
      </button>
    </span>
  );
}
