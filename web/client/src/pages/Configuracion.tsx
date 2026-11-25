import { useCallback, useEffect, useRef, useState } from 'react';
import { authFetch } from '../lib/auth.js';
import { motivoConfig } from '../lib/format.js';
import { useEsAdmin } from '../lib/sesion.js';
import { temaEfectivo, useTema } from '../lib/tema.js';
import type { PlacaStatus } from '../lib/types.js';

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
  /** Redes y broker que la placa usa de verdad, sin contrasenas. */
  effective?: unknown;
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
  invalida: 'Rechazada: configuración inválida',
  ocupada: 'Rechazada: hay un experimento en curso',
  incompleta: 'Interrumpida: faltaron bloques',
  error: 'Error en la placa',
  no_entregada: 'La placa no respondió',
  revertida: 'Revertida: la placa no llegó al broker y volvió a la anterior',
  red_conservada: 'Aplicada, pero la placa conservó la red en uso',
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
      detail: 'Cada bloque espera la confirmación de la placa antes del siguiente.',
    };
  }
  // Se aplico; solo la red en uso que se quito sigue, porque las otras no
  // conectaron.
  if (pedido.state === 'red_conservada') {
    return {
      kind: 'ok',
      text: NOMBRE_ESTADO.red_conservada,
      detail: [pedido.message, `Enviada ${hora}. El resto de la configuración ya corre en la placa.`].filter(Boolean).join('. '),
    };
  }
  if (pedido.state === 'aceptada') {
    if (pedido.newConfigId && pedido.newConfigId === vigenteId) {
      return {
        kind: 'ok',
        text: `Vigente en la placa (${pedido.newConfigId})`,
        detail: `Enviada ${hora}. La placa reinició y ya corre con esta configuración.`,
      };
    }
    return {
      kind: 'ok',
      text: `La placa confirmó la recepción: configuración grabada en la SD${pedido.newConfigId ? ` (${pedido.newConfigId})` : ''}`,
      detail: `Enviada ${hora}. La placa se reinicia sola para aplicarla en cuanto su pantalla esté en Principal sin experimento (si está en Resultado o Configuración, al volver a Principal); hasta entonces sigue con la anterior.`,
    };
  }
  return {
    kind: 'error',
    text: NOMBRE_ESTADO[pedido.state] ?? pedido.state,
    detail: [pedido.message, `Enviada ${hora}. La placa sigue con su configuración anterior.`].filter(Boolean).join('. '),
  };
}

export function Configuracion({ placa }: { placa?: PlacaStatus | null } = {}) {
  const esAdmin = useEsAdmin();
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
  // Va aunque la placa corra con los valores de fabrica (content null): la
  // vista Conexion necesita igual las redes y el broker EFECTIVOS.
  const cargadaEnFormulario = useRef(false);
  useEffect(() => {
    if (!generadorListo || !vigente?.configId || cargadaEnFormulario.current) return;
    cargadaEnFormulario.current = true;
    marco.current?.contentWindow?.postMessage(
      { type: 'biosoft-load', config: vigente.content ?? null, efectivo: vigente.effective ?? null },
      window.location.origin,
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
    // La red por la que esta conectada la placa: el generador no deja quitarla.
    redEnUso: placa?.network ?? null,
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

  // Solo lectura: el generador oculta Enviar (y ofrece Descargar). El
  // servidor rechaza el envio de todas formas (requireAdmin).
  useEffect(() => {
    if (!generadorListo || esAdmin) return;
    marco.current?.contentWindow?.postMessage({ type: 'biosoft-readonly' }, window.location.origin);
  }, [generadorListo, esAdmin]);

  // Biblioteca de escenarios de prueba: la lista vive en la base del monitor
  // (los de fabrica sembrados + los propios) y el generador la muestra como
  // "pruebas". Leer es para todos; crear, editar, borrar y restaurar, solo
  // admin (el servidor lo exige igual).
  const [escenarios, setEscenarios] = useState<unknown[] | null>(null);
  useEffect(() => {
    authFetch('/api/scenarios').then((r) => (r.ok ? r.json() : null))
      .then((data) => setEscenarios(data?.items ?? null)).catch(() => setEscenarios(null));
  }, []);
  useEffect(() => {
    if (!generadorListo || !escenarios) return;
    marco.current?.contentWindow?.postMessage(
      { type: 'biosoft-scenarios', items: escenarios, admin: esAdmin }, window.location.origin,
    );
  }, [generadorListo, escenarios, esAdmin]);

  function avisarAlGenerador(ok: boolean, text: string, selectId?: string | null) {
    marco.current?.contentWindow?.postMessage(
      { type: 'biosoft-scenario-result', ok, text, ...(selectId !== undefined ? { selectId } : {}) },
      window.location.origin,
    );
  }

  async function operarEscenario(metodo: string, ruta: string, cuerpo?: unknown) {
    const respuesta = await authFetch(ruta, {
      method: metodo,
      headers: { 'Content-Type': 'application/json' },
      body: cuerpo === undefined ? undefined : JSON.stringify(cuerpo),
    });
    const data = await respuesta.json().catch(() => ({}));
    if (respuesta.ok && Array.isArray(data.items)) setEscenarios(data.items);
    return { ok: respuesta.ok, data, status: respuesta.status };
  }

  async function guardarEscenario(id: string | null, escenario: unknown) {
    const r = id
      ? await operarEscenario('PUT', `/api/scenarios/${encodeURIComponent(id)}`, escenario)
      : await operarEscenario('POST', '/api/scenarios', escenario);
    if (r.ok) avisarAlGenerador(true, id ? 'Cambios guardados.' : 'Escenario creado.', r.data.item?.id ?? null);
    else avisarAlGenerador(false, r.data.error ?? `No se pudo guardar (error ${r.status}).`);
  }

  async function borrarEscenario(id: string, nombre: string) {
    if (!window.confirm(`¿Borrar el escenario "${nombre}"? Si es de fábrica, se puede recuperar con ` +
      '"Restaurar los de fábrica".')) return;
    const r = await operarEscenario('DELETE', `/api/scenarios/${encodeURIComponent(id)}`);
    if (r.ok) avisarAlGenerador(true, `"${nombre}" borrado.`, null);
    else avisarAlGenerador(false, r.data.error ?? `No se pudo borrar (error ${r.status}).`);
  }

  async function restaurarEscenarios() {
    if (!window.confirm('Los escenarios de fábrica vuelven a sus valores originales y se recrean los que ' +
      'se hayan borrado. Los escenarios propios no se tocan.\n\n¿Restaurar?')) return;
    const r = await operarEscenario('POST', '/api/scenarios/restore');
    if (r.ok) avisarAlGenerador(true, 'Escenarios de fábrica restaurados.');
    else avisarAlGenerador(false, r.data.error ?? `No se pudo restaurar (error ${r.status}).`);
  }

  useEffect(() => {
    function alRecibir(ev: MessageEvent) {
      if (ev.origin !== window.location.origin || ev.source !== marco.current?.contentWindow) return;
      if (ev.data?.type === 'biosoft-ready') setGeneradorListo(true);
      if (ev.data?.type === 'biosoft-altura' && typeof ev.data.h === 'number') {
        setAlturaGenerador(Math.ceil(ev.data.h));
      }
      // Cambio de paso (o de vista) en el generador: se vuelve al comienzo del
      // marco, que es donde empieza lo que se esta mirando.
      if (ev.data?.type === 'biosoft-arriba' && marco.current) {
        const tope = marco.current.getBoundingClientRect().top + window.scrollY - 12;
        if (window.scrollY > tope) window.scrollTo({ top: Math.max(0, tope) });
      }
      if (ev.data?.type === 'biosoft-send') void enviar(ev.data.config);
      if (ev.data?.type === 'biosoft-scenario-save') void guardarEscenario(ev.data.id ?? null, ev.data.scenario);
      if (ev.data?.type === 'biosoft-scenario-delete') void borrarEscenario(String(ev.data.id), String(ev.data.name ?? ''));
      if (ev.data?.type === 'biosoft-scenario-restore') void restaurarEscenarios();
    }
    window.addEventListener('message', alRecibir);
    return () => window.removeEventListener('message', alRecibir);
  });

  async function enviar(config: unknown) {
    setError(null);
    const ok = window.confirm(
      'La configuración se graba en la tarjeta SD de la placa y la placa SE REINICIA SOLA para ' +
      'aplicarla en cuanto su pantalla esté en Principal y sin experimento (si alguien está en ' +
      'Resultado o Configuración, al volver a Principal). No hay confirmación en la pantalla de la ' +
      'placa.\n\n¿Enviar?',
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

  // El generador embebido sigue el tema del monitor. La URL lleva el tema
  // inicial (fija, para no recargar el marco y perder lo que se edita); los
  // cambios posteriores van por mensaje.
  const { efectivo } = useTema();
  const srcGenerador = useRef(`/generador-config.html?tema=${temaEfectivo()}`);
  useEffect(() => {
    if (!generadorListo) return;
    marco.current?.contentWindow?.postMessage({ type: 'biosoft-tema', tema: efectivo }, window.location.origin);
  }, [generadorListo, efectivo]);

  // UNA sola barra de desplazamiento, la de la pagina: el marco del
  // generador toma el alto de su contenido (lo informa con "biosoft-altura")
  // y nunca scrollea por su cuenta. Antes la pagina quedaba fija y el marco
  // tenia su propio scroll: eran dos, y no se sabia cual movia que.
  const [alturaGenerador, setAlturaGenerador] = useState<number | null>(null);

  return (
    <div className="config-pagina">
      <section className="panel config-franja">
        <div className="franja-dato">
          <span className="franja-rotulo">En la placa</span>
          {!vigente?.configId ? (
            <span className="tenue-celda">todavía no informó su configuración</span>
          ) : (
            <span title={vigente.reportedAt ? `Informada ${new Date(vigente.reportedAt).toLocaleString('es-AR')}` : undefined}>
              <strong>{vigente.configId === 'default' ? 'Valores de fábrica' : vigente.configId}</strong>
              <span className="tenue-celda"> · {motivoConfig(vigente.configId, vigente.loadStatus).texto}</span>
            </span>
          )}
        </div>

        {estado && (
          <div className={`franja-envio envio-${estado.kind}`} role="status" title={estado.detail}>
            <strong>{estado.text}</strong>
          </div>
        )}
        {error && <span className="login-error franja-error">{error}</span>}
        {!esAdmin && <span className="tenue-celda">Solo lectura: enviar es para administradores.</span>}

        <div className="franja-acciones">
          {pedidos.length > 0 && (
            <details className="desplegable">
              <summary>Envíos ({pedidos.length})</summary>
              <div className="desplegable-panel">
                <ul className="lista-pedidos">
                  {pedidos.map((p) => (
                    <li key={p.requestId} className={`pedido pedido-${p.state}`}>
                      <span>{new Date(p.createdAt).toLocaleString('es-AR')}</span>
                      <span className="pedido-estado">
                        {NOMBRE_ESTADO[p.state] ?? p.state}
                        {EN_CURSO.has(p.state) && p.chunks ? ` (${p.sentChunks ?? 0}/${p.chunks})` : ''}
                      </span>
                      {p.user && <span className="tenue-celda">{p.user}</span>}
                      {p.newConfigId && <span>nueva: {p.newConfigId}</span>}
                      {p.message && <span className="pedido-mensaje">{p.message}</span>}
                    </li>
                  ))}
                </ul>
              </div>
            </details>
          )}
          <details className="desplegable">
            <summary>Cómo se aplica</summary>
            <div className="desplegable-panel">
              {estado?.detail && <p><strong>Último envío:</strong> {estado.detail}</p>}
              <p>
                Lo que se envíe desde acá se graba en la tarjeta SD y la placa se reinicia sola para
                aplicarlo en cuanto su pantalla esté en Principal y sin experimento; si está en Resultado o
                Configuración, espera a que vuelvan a Principal (y mientras tanto oculta REPETIR, que correria
                con la configuración anterior). Se rechaza si hay un experimento en curso. La configuración
                anterior queda respaldada en la tarjeta como config.prev.json. WiFi y broker no se editan desde
                acá y se conservan.
              </p>
            </div>
          </details>
        </div>
      </section>

      <section className="panel panel-generador">
        {/* El generador se pinta con los mismos tokens que el monitor, en su tema. */}
        <iframe
          ref={marco}
          src={srcGenerador.current}
          title="Generador de configuración"
          className="generador"
          scrolling="no"
          style={alturaGenerador ? { height: alturaGenerador } : undefined}
        />
      </section>
    </div>
  );
}
