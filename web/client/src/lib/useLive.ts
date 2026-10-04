import { useEffect, useRef, useState } from 'react';
import { io, type Socket } from 'socket.io-client';
import { authFetch, clearToken, getToken } from './auth.js';
import type {
  AlertItem, Coils, Measures, PlacaStatus, ResultItem, RunType, Snapshot, Status, Targets,
} from './types.js';
import { tipoDesdeMarca } from './format.js';

// Estado en vivo del experimento.
//
// Arranca del `snapshot` que el backend manda apenas se conecta el socket
// (lo que reemplaza al retain del broker para el navegador) y despues lo va
// pisando con cada evento. Sin el snapshot, una pantalla recien abierta se
// quedaria en guiones hasta la proxima tanda (segundos con los defaults, pero
// la cadencia la elige la SD) durante un experimento que corre perfectamente.

export interface LiveState {
  conectado: boolean;
  brokerOk: boolean;
  deviceId: string | null;
  run: { id: string; startedAt: string; state: string; runType: RunType } | null;
  targets: Targets | null;
  measures: Measures | null;
  coils: Coils | null;
  status: Status | null;
  alerts: AlertItem[];
  ultimoResultado: ResultItem | null;
  /** Ultimo resultado del ping a la placa. */
  placa: PlacaStatus | null;
}

const VACIO: LiveState = {
  conectado: false, brokerOk: false, deviceId: null, run: null, targets: null,
  measures: null, coils: null, status: null, alerts: [], ultimoResultado: null, placa: null,
};

// Las que se guardan para la lista de En curso. El grafico pide todas las de
// la corrida aparte (GraficosEnVivo).
const MAX_ALERTAS = 50;

export function useLive(): LiveState {
  const [state, setState] = useState<LiveState>(VACIO);
  const socketRef = useRef<Socket | null>(null);

  useEffect(() => {
    // El mismo token que el REST: el WebSocket es la otra puerta al mismo
    // dato, y dejarla abierta haria inutil cerrar la primera.
    const socket = io({ auth: { token: getToken() } });
    socketRef.current = socket;

    socket.on('connect', () => {
      setState((s) => ({ ...s, conectado: true }));
      // El ping corre en el servidor; al conectar se pide el ultimo
      // resultado para no esperar hasta el proximo.
      authFetch('/api/board')
        .then((r) => (r.ok ? r.json() : null))
        .then((placa: PlacaStatus | null) => { if (placa) setState((s) => ({ ...s, placa })); })
        .catch(() => {});
    });
    socket.on('disconnect', () => setState((s) => ({ ...s, conectado: false })));

    // El servidor rechaza el handshake si el token vencio. Reintentar seria
    // un bucle infinito de conexiones rechazadas: se limpia la sesion y la
    // app manda al login.
    socket.on('connect_error', (error) => {
      setState((s) => ({ ...s, conectado: false }));
      if (error.message === 'no autorizado') { socket.close(); clearToken(); window.location.reload(); }
    });

    socket.on('snapshot', (snap: Snapshot) => {
      setState((s) => ({
        ...s,
        deviceId: snap.deviceId,
        brokerOk: snap.link.broker,
        run: snap.run
          ? { id: snap.run.id, startedAt: snap.run.startedAt, state: snap.run.state, runType: snap.run.runType }
          : null,
        targets: snap.run?.targets ?? null,
        measures: snap.measures,
        coils: snap.coils,
        status: snap.status,
        alerts: snap.alerts,
        // Sin corrida en curso, el resultado de la ultima: antes vivia solo en
        // la memoria del navegador que lo vio llegar, y recargar la pagina
        // despues de una prueba borraba como habia terminado.
        ultimoResultado: !snap.run && snap.lastRun?.result
          ? {
              ...snap.lastRun.result,
              ts: snap.lastRun.endedAt ?? snap.lastRun.startedAt,
              runId: snap.lastRun.id,
              runType: snap.lastRun.runType,
            }
          : s.ultimoResultado,
      }));
    });

    socket.on('link:status', ({ broker }: { broker: boolean }) =>
      setState((s) => ({ ...s, brokerOk: broker })));

    socket.on('board:status', (placa: PlacaStatus) => setState((s) => ({ ...s, placa })));

    socket.on('telemetry:measures', (data: Measures) =>
      setState((s) => ({ ...s, measures: data })));

    socket.on('telemetry:coils', (data: Coils) =>
      setState((s) => ({ ...s, coils: data })));

    socket.on('telemetry:status', (data: Status) =>
      setState((s) => ({ ...s, status: data })));

    // Un targets nuevo es un experimento NUEVO: se limpia todo lo del
    // anterior. Si no, las alertas y las mediciones de la corrida que termino
    // se quedarian en pantalla como si fueran de esta.
    socket.on('telemetry:targets', (data: Targets & { ts: string; runId: string }) =>
      setState((s) => ({
        ...s,
        run: { id: data.runId, startedAt: data.ts, state: 'running', runType: tipoDesdeMarca(data.test) },
        targets: data,
        measures: null, coils: null, status: null, alerts: [], ultimoResultado: null,
      })));

    socket.on('telemetry:alert', (data: AlertItem) =>
      setState((s) => ({ ...s, alerts: [data, ...s.alerts].slice(0, MAX_ALERTAS) })));

    socket.on('telemetry:result', (data: ResultItem) =>
      setState((s) => ({ ...s, run: null, ultimoResultado: { ...data, runType: s.run?.runType } })));

    return () => { socket.close(); socketRef.current = null; };
  }, []);

  return state;
}
