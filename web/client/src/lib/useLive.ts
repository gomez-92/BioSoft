import { useEffect, useRef, useState } from 'react';
import { io, type Socket } from 'socket.io-client';
import type {
  AlertItem, Coils, Measures, ResultItem, Snapshot, Status, Targets,
} from './types.js';

// Estado en vivo del experimento.
//
// Arranca del `snapshot` que el backend manda apenas se conecta el socket
// (lo que reemplaza al retain del broker para el navegador) y despues lo va
// pisando con cada evento. Sin el snapshot, una pantalla recien abierta se
// quedaria en guiones hasta la proxima tanda: hasta 30 segundos mirando una
// pantalla vacia durante un experimento que corre perfectamente.

export interface LiveState {
  conectado: boolean;
  brokerOk: boolean;
  deviceId: string | null;
  run: { id: string; startedAt: string; state: string } | null;
  targets: Targets | null;
  measures: Measures | null;
  coils: Coils | null;
  status: Status | null;
  alerts: AlertItem[];
  ultimoResultado: ResultItem | null;
}

const VACIO: LiveState = {
  conectado: false, brokerOk: false, deviceId: null, run: null, targets: null,
  measures: null, coils: null, status: null, alerts: [], ultimoResultado: null,
};

const MAX_ALERTAS = 5;

export function useLive(): LiveState {
  const [state, setState] = useState<LiveState>(VACIO);
  const socketRef = useRef<Socket | null>(null);

  useEffect(() => {
    const socket = io();
    socketRef.current = socket;

    socket.on('connect', () => setState((s) => ({ ...s, conectado: true })));
    socket.on('disconnect', () => setState((s) => ({ ...s, conectado: false })));

    socket.on('snapshot', (snap: Snapshot) => {
      setState((s) => ({
        ...s,
        deviceId: snap.deviceId,
        brokerOk: snap.link.broker,
        run: snap.run ? { id: snap.run.id, startedAt: snap.run.startedAt, state: snap.run.state } : null,
        targets: snap.run?.targets ?? null,
        measures: snap.measures,
        coils: snap.coils,
        status: snap.status,
        alerts: snap.alerts,
      }));
    });

    socket.on('link:status', ({ broker }: { broker: boolean }) =>
      setState((s) => ({ ...s, brokerOk: broker })));

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
        run: { id: data.runId, startedAt: data.ts, state: 'running' },
        targets: data,
        measures: null, coils: null, status: null, alerts: [], ultimoResultado: null,
      })));

    socket.on('telemetry:alert', (data: AlertItem) =>
      setState((s) => ({ ...s, alerts: [data, ...s.alerts].slice(0, MAX_ALERTAS) })));

    socket.on('telemetry:result', (data: ResultItem) =>
      setState((s) => ({ ...s, run: null, ultimoResultado: data })));

    return () => { socket.close(); socketRef.current = null; };
  }, []);

  return state;
}
