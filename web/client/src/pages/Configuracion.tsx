import { useCallback, useEffect, useRef, useState } from 'react';
import { authFetch } from '../lib/auth.js';

// Configuracion de la placa (tarjeta 24): ver la vigente y mandar una nueva.
//
// El formulario NO esta escrito aca: es el mismo generador de tools/,
// embebido. Sus reglas de validacion (enteros vs decimales, rangos,
// combinaciones peligrosas) son las que protegen a la placa de valores que
// descartaria en silencio, y reescribirlas en React haria que el generador y
// el monitor validen distinto con el tiempo. Se comunican por postMessage:
// el monitor le pasa la configuracion vigente y el generador devuelve el
// JSON ya validado. Embebido, el generador no muestra ni produce wifi/broker:
// las credenciales no viajan nunca por MQTT.

interface Vigente { configId: string | null; reportedAt?: string | null; content?: unknown }
interface Pedido {
  requestId: string; user?: string; state: string; message?: string | null;
  chunks?: number; sentChunks?: number; newConfigId?: string | null; createdAt: string;
}

const EN_CURSO = new Set(['enviando', 'parcial']);

const NOMBRE_ESTADO: Record<string, string> = {
  enviando: 'Enviando',
  parcial: 'Enviando',
  aceptada: 'Grabada en la placa',
  invalida: 'Rechazada: configuracion invalida',
  ocupada: 'Rechazada: hay un experimento en curso',
  incompleta: 'Interrumpida: faltaron bloques',
  error: 'Error en la placa',
  no_entregada: 'La placa no respondio',
};

export function Configuracion() {
  const marco = useRef<HTMLIFrameElement>(null);
  const [vigente, setVigente] = useState<Vigente | null>(null);
  const [pedidos, setPedidos] = useState<Pedido[]>([]);
  const [enCurso, setEnCurso] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [generadorListo, setGeneradorListo] = useState(false);

  const cargarPedidos = useCallback(() => {
    authFetch('/api/config/requests')
      .then((r) => r.json())
      .then((data) => { setPedidos(data.items ?? []); setEnCurso(data.inProgress ?? null); })
      .catch(() => {});
  }, []);

  useEffect(() => {
    authFetch('/api/config/current').then((r) => r.json()).then(setVigente).catch(() => setVigente(null));
    cargarPedidos();
  }, [cargarPedidos]);

  // Mientras hay un envio en curso, el estado se sigue cada segundo.
  useEffect(() => {
    if (!enCurso) return;
    const id = setInterval(cargarPedidos, 1000);
    return () => clearInterval(id);
  }, [enCurso, cargarPedidos]);

  // Le pasa al generador la configuracion vigente apenas los dos estan listos.
  useEffect(() => {
    if (!generadorListo || !vigente?.content) return;
    marco.current?.contentWindow?.postMessage(
      { type: 'biosoft-load', config: vigente.content }, window.location.origin,
    );
  }, [generadorListo, vigente]);

  useEffect(() => {
    function alRecibir(ev: MessageEvent) {
      if (ev.origin !== window.location.origin || ev.source !== marco.current?.contentWindow) return;
      if (ev.data?.type === 'biosoft-ready') setGeneradorListo(true);
      if (ev.data?.type === 'biosoft-send') void enviar(ev.data.config);
    }
    window.addEventListener('message', alRecibir);
    return () => window.removeEventListener('message', alRecibir);
  });

  async function enviar(config: unknown) {
    setError(null);
    const ok = window.confirm(
      'La configuracion se graba en la tarjeta SD de la placa y se aplica en el PROXIMO REINICIO, ' +
      'cualquiera sea su causa. No hay confirmacion en la pantalla de la placa.\n\n¿Enviar?',
    );
    if (!ok) return;
    const respuesta = await authFetch('/api/config/requests', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ config }),
    });
    const data = await respuesta.json().catch(() => ({}));
    if (!respuesta.ok) { setError(data.error ?? `Error ${respuesta.status}`); return; }
    setEnCurso(data.requestId);
    cargarPedidos();
  }

  return (
    <>
      <section className="panel">
        <h3>Configuracion vigente de la placa</h3>
        {!vigente?.configId ? (
          <p className="empty">La placa todavia no informo su configuracion.</p>
        ) : (
          <dl className="datos">
            <div><dt>Configuracion</dt><dd>{vigente.configId === 'default' ? 'Valores de fabrica (sin tarjeta)' : vigente.configId}</dd></div>
            {vigente.reportedAt && (
              <div><dt>Informada</dt><dd>{new Date(vigente.reportedAt).toLocaleString('es-AR')}</dd></div>
            )}
          </dl>
        )}
        <p className="aviso">
          Lo que se envie desde aca se graba en la tarjeta SD y se aplica en el proximo reinicio de la
          placa, cualquiera sea su causa. Se rechaza si hay un experimento en curso. La configuracion
          anterior queda respaldada en la tarjeta como config.prev.json. WiFi y broker no se editan desde
          aca y se conservan.
        </p>
        {error && <p className="login-error">{error}</p>}
      </section>

      {pedidos.length > 0 && (
        <section className="panel">
          <h3>Envios</h3>
          <ul className="lista-pedidos">
            {pedidos.map((p) => (
              <li key={p.requestId} className={`pedido pedido-${p.state}`}>
                <span>{new Date(p.createdAt).toLocaleString('es-AR')}</span>
                <span className="pedido-estado">
                  {NOMBRE_ESTADO[p.state] ?? p.state}
                  {EN_CURSO.has(p.state) && p.chunks ? ` (${p.sentChunks ?? 0}/${p.chunks})` : ''}
                </span>
                {p.newConfigId && <span>nueva: {p.newConfigId}</span>}
                {p.message && <span className="pedido-mensaje">{p.message}</span>}
              </li>
            ))}
          </ul>
        </section>
      )}

      <section className="panel panel-generador">
        <iframe
          ref={marco}
          src="/generador-config.html"
          title="Generador de configuracion"
          className="generador"
        />
      </section>
    </>
  );
}
