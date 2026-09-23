import { useEffect, useState } from 'react';
import { Live } from './components/Live.js';
import { useLive } from './lib/useLive.js';

interface Health {
  ok: boolean;
  deviceId: string;
  mongo: { connected: boolean; state: string };
  mqtt: { connected: boolean; url: string };
  stored?: Record<string, number>;
  currentRun: string | null;
}

export function App() {
  const estado = useLive();
  const [health, setHealth] = useState<Health | null>(null);
  const [verDiagnostico, setVerDiagnostico] = useState(false);

  // El diagnostico se consulta solo cuando esta abierto: el estado del
  // experimento ya llega por WebSocket y no necesita polling.
  useEffect(() => {
    if (!verDiagnostico) return;
    const load = () => fetch('/api/health').then((r) => r.json()).then(setHealth).catch(() => {});
    load();
    const id = setInterval(load, 5000);
    return () => clearInterval(id);
  }, [verDiagnostico]);

  return (
    <main>
      <header className="cabecera">
        <div>
          <h1>BioSoft</h1>
          <p className="subtitle">Monitor remoto{estado.deviceId ? ` · ${estado.deviceId}` : ''}</p>
        </div>
        <span className={`enlace ${estado.conectado && estado.brokerOk ? 'enlace-ok' : 'enlace-caido'}`}>
          {estado.conectado ? (estado.brokerOk ? 'En linea' : 'Sin broker') : 'Sin servidor'}
        </span>
      </header>

      <Live estado={estado} />

      <section className="diagnostico">
        <button type="button" onClick={() => setVerDiagnostico((v) => !v)}>
          {verDiagnostico ? 'Ocultar diagnostico' : 'Ver diagnostico'}
        </button>
        {verDiagnostico && health && (
          <dl className="datos">
            <dt>Mongo</dt><dd>{health.mongo.state}</dd>
            <dt>Broker</dt><dd>{health.mqtt.connected ? health.mqtt.url : 'desconectado'}</dd>
            <dt>Corrida actual</dt><dd>{health.currentRun ?? 'ninguna'}</dd>
            {health.stored && Object.entries(health.stored).map(([nombre, cantidad]) => (
              <Fragmento key={nombre} termino={nombre} valor={String(cantidad)} />
            ))}
          </dl>
        )}
      </section>
    </main>
  );
}

function Fragmento({ termino, valor }: { termino: string; valor: string }) {
  return (<><dt>{termino}</dt><dd>{valor}</dd></>);
}
