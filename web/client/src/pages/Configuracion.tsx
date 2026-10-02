import { useCallback, useEffect, useRef, useState } from 'react';
import { authFetch } from '../lib/auth.js';
import { motivoConfig } from '../lib/format.js';

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

interface Vigente {
  configId: string | null; reportedAt?: string | null; content?: unknown; loadStatus?: string | null;
}
interface Pedido {
  requestId: string; user?: string; state: string; message?: string | null;
  chunks?: number; sentChunks?: number; newConfigId?: string | null; createdAt: string;
  /** Solo en el ultimo envio: la configuracion que se mando. */
  content?: unknown;
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

// Que decir del ultimo envio, en el banner de la pagina y en la cabecera del
// generador (donde esta el boton Enviar, que es donde se esta mirando).
// "aceptada" es la confirmacion de la placa: el archivo quedo grabado en la
// SD. Que este VIGENTE se sabe recien cuando, tras reiniciar, la placa
// informa ese mismo id como su configuracion.
interface EstadoEnvio { kind: 'ok' | 'error' | 'progress'; text: string; detail?: string }

export function estadoEnvio(pedido: Pedido | undefined, vigenteId: string | null | undefined): EstadoEnvio | null {
  if (!pedido) return null;
  const hora = new Date(pedido.createdAt).toLocaleString('es-AR');
  if (EN_CURSO.has(pedido.state)) {
    return {
      kind: 'progress',
      text: `Enviando a la placa... bloque ${pedido.sentChunks ?? 0} de ${pedido.chunks ?? '?'}`,
      detail: 'Cada bloque espera la confirmacion de la placa antes del siguiente.',
    };
  }
  if (pedido.state === 'aceptada') {
    if (pedido.newConfigId && pedido.newConfigId === vigenteId) {
      return {
        kind: 'ok',
        text: `Vigente en la placa (${pedido.newConfigId})`,
        detail: `Enviada ${hora}. La placa reinicio y ya corre con esta configuracion.`,
      };
    }
    return {
      kind: 'ok',
      text: `La placa confirmo la recepcion: configuracion grabada en la SD${pedido.newConfigId ? ` (${pedido.newConfigId})` : ''}`,
      detail: `Enviada ${hora}. Se aplica en el proximo reinicio de la placa; hasta entonces sigue con la anterior.`,
    };
  }
  return {
    kind: 'error',
    text: NOMBRE_ESTADO[pedido.state] ?? pedido.state,
    detail: [pedido.message, `Enviada ${hora}. La placa sigue con su configuracion anterior.`].filter(Boolean).join('. '),
  };
}

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

  const cargarVigente = useCallback(() => {
    authFetch('/api/config/current').then((r) => r.json()).then(setVigente).catch(() => setVigente(null));
  }, []);

  useEffect(() => {
    cargarVigente();
    cargarPedidos();
  }, [cargarVigente, cargarPedidos]);

  const ultimo = pedidos[0];
  const estado = estadoEnvio(ultimo, vigente?.configId);

  // Grabada pero todavia no vigente: se vuelve a mirar la vigente cada 10 s,
  // para que el aviso pase solo a "vigente" cuando la placa reinicie.
  const esperandoReinicio = ultimo?.state === 'aceptada' && !!ultimo.newConfigId
    && ultimo.newConfigId !== vigente?.configId;
  useEffect(() => {
    if (!esperandoReinicio) return;
    const id = setInterval(cargarVigente, 10000);
    return () => clearInterval(id);
  }, [esperandoReinicio, cargarVigente]);

  // Mientras hay un envio en curso, el estado se sigue cada segundo.
  useEffect(() => {
    if (!enCurso) return;
    const id = setInterval(cargarPedidos, 1000);
    return () => clearInterval(id);
  }, [enCurso, cargarPedidos]);

  // Le pasa al generador la configuracion vigente para cargarla en el
  // formulario, una sola vez: repetirlo pisaria lo que se esta editando.
  const cargadaEnFormulario = useRef(false);
  useEffect(() => {
    if (!generadorListo || !vigente?.content || cargadaEnFormulario.current) return;
    cargadaEnFormulario.current = true;
    marco.current?.contentWindow?.postMessage(
      { type: 'biosoft-load', config: vigente.content }, window.location.origin,
    );
  }, [generadorListo, vigente]);

  // Las tres etapas, separadas: lo que corre en la placa, lo enviado que
  // todavia no corre (enviandose, o grabado esperando el reinicio) y -- del
  // lado del generador -- lo que se esta editando.
  const enviado = ultimo && (EN_CURSO.has(ultimo.state) || esperandoReinicio)
    ? {
      config: ultimo.content ?? null,
      configId: ultimo.newConfigId ?? null,
      fase: EN_CURSO.has(ultimo.state) ? 'enviando' : 'grabado',
    }
    : null;
  const etapas = JSON.stringify({
    placa: vigente?.configId
      ? { config: vigente.content ?? null, configId: vigente.configId, ...motivoConfig(vigente.configId, vigente.loadStatus) }
      : null,
    enviado,
  });
  useEffect(() => {
    if (!generadorListo) return;
    marco.current?.contentWindow?.postMessage(
      { type: 'biosoft-board', ...JSON.parse(etapas) }, window.location.origin,
    );
  }, [generadorListo, etapas]);

  useEffect(() => {
    if (!generadorListo) return;
    marco.current?.contentWindow?.postMessage({ type: 'biosoft-status', status: estado }, window.location.origin);
  }, [generadorListo, estado?.kind, estado?.text, estado?.detail]);

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
            <div><dt>Configuracion</dt><dd>{vigente.configId === 'default' ? 'Valores de fabrica' : vigente.configId}</dd></div>
            <div><dt>Origen</dt><dd>{motivoConfig(vigente.configId, vigente.loadStatus).texto}</dd></div>
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
        {estado && (
          <div className={`envio-estado envio-${estado.kind}`} role="status">
            <strong>{estado.text}</strong>
            {estado.detail && <span>{estado.detail}</span>}
          </div>
        )}
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
