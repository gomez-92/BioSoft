import { useEffect, useState } from 'react';
import { io } from 'socket.io-client';

// FASE 1: sigue siendo la pantalla de diagnostico, no el tablero. Ahora
// muestra ademas cuantos documentos hay guardados de cada cosa, que es la
// forma directa de ver que la ingesta escribe y no solo recibe. Lo que llega
// por WebSocket ya viene parseado y persistido por el backend.

interface Health {
  ok: boolean;
  deviceId: string;
  uptimeSeconds: number;
  mongo: { connected: boolean; state: string };
  mqtt: { connected: boolean; url: string; clientId: string; lastMessageAt: Record<string, string> };
  stored?: Record<string, number>;
  realtime: { clients: number };
  topics: Record<string, string>;
}

interface LiveEvent { group: string; data: Record<string, unknown> }

const GROUPS = ['measures', 'coils', 'status', 'targets', 'alert', 'result'];

export function App() {
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [events, setEvents] = useState<LiveEvent[]>([]);
  const [socketOk, setSocketOk] = useState(false);

  useEffect(() => {
    const load = () =>
      fetch('/api/health')
        .then((response) => response.json())
        .then((data) => { setHealth(data); setError(null); })
        .catch((cause) => setError(String(cause)));
    load();
    const id = setInterval(load, 5000);
    return () => clearInterval(id);
  }, []);

  useEffect(() => {
    const socket = io();
    socket.on('connect', () => setSocketOk(true));
    socket.on('disconnect', () => setSocketOk(false));
    for (const group of GROUPS) {
      socket.on(`telemetry:${group}`, (data: Record<string, unknown>) => {
        // Los ultimos 50 alcanzan para ver que llega; el historico es Mongo.
        setEvents((previous) => [{ group, data }, ...previous].slice(0, 50));
      });
    }
    return () => { socket.close(); };
  }, []);

  return (
    <main>
      <header>
        <h1>BioSoft — Monitor remoto</h1>
        <p className="subtitle">Fase 1 — ingesta y persistencia</p>
      </header>

      <section className="cards">
        <Card label="Backend" ok={!error && health !== null} detail={error ?? `uptime ${health?.uptimeSeconds ?? 0}s`} />
        <Card label="Mongo" ok={!!health?.mongo.connected} detail={health?.mongo.state ?? '—'} />
        <Card label="Broker MQTT" ok={!!health?.mqtt.connected} detail={health?.mqtt.url ?? '—'} />
        <Card label="WebSocket" ok={socketOk} detail={socketOk ? 'en vivo' : 'sin conexion'} />
      </section>

      {health?.stored && (
        <section>
          <h2>Guardado en Mongo</h2>
          <ul className="counters">
            {Object.entries(health.stored).map(([name, count]) => (
              <li key={name}>
                <span className="counter-value">{count}</span>
                <span className="counter-label">{name}</span>
              </li>
            ))}
          </ul>
        </section>
      )}

      <section>
        <h2>Telemetria en vivo</h2>
        {events.length === 0 ? (
          <p className="empty">
            Todavia no llego nada. Publica una corrida con el simulador o encende la placa.
          </p>
        ) : (
          <ul className="events">
            {events.map((event, index) => (
              <li key={index}>
                <span className={`tag tag-${event.group}`}>{event.group}</span>
                <time>{new Date(String(event.data.ts)).toLocaleTimeString()}</time>
                <code>{JSON.stringify(event.data)}</code>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  );
}

function Card({ label, ok, detail }: { label: string; ok: boolean; detail: string }) {
  return (
    <article className={ok ? 'card card-ok' : 'card card-down'}>
      <h3>{label}</h3>
      <p className="state">{ok ? 'OK' : 'CAIDO'}</p>
      <p className="detail">{detail}</p>
    </article>
  );
}
