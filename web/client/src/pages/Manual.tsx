import { useEffect, useRef, useState } from 'react';
import { Icono } from '../components/Icono.js';
import { authFetch } from '../lib/auth.js';
import { useTema } from '../lib/tema.js';

// Manual de usuario. La fuente unica es docs/manual-de-usuario.html (y el PDF
// que se genera de ella); el servidor tiene una copia en server/manual/ que
// mantiene `npm run sync:generador` y que server/test/manual.test.ts obliga a
// tener al dia.
//
// Es SOLO para quien tiene cuenta: el servidor lo entrega detras del login
// (/api/manual, /api/manual.pdf), nunca como archivo publico. Por eso no hay
// un src ni un href directos -- el navegador no mandaria el token --: el HTML
// se pide con authFetch y va al marco por srcdoc (mismo origen que esta
// pagina), y el PDF y "Abrir aparte" salen de un blob local.
//
// Se muestra en un marco y no reescrito en React: el manual trae su propia
// hoja de estilo (pantalla + impresion), y copiar su marcado aca seria una
// segunda copia a mano del mismo texto -- justo lo que el manual evita al no
// tener version Markdown.
//
// Mismas reglas que el generador embebido en Configuracion:
//   - UNA barra de desplazamiento, la de la pagina: el marco crece al alto del
//     contenido. El marco es del mismo origen, asi que el alto se mide desde
//     aca (ResizeObserver de la ventana del marco) sin tocar el manual.
//   - El tema es el del monitor: data-theme en el <html> del manual, que lo
//     respeta por encima de prefers-color-scheme.
// Como el marco no scrollea, los enlaces internos (#s3) se resuelven
// desplazando la pagina, y el indice lo arma esta pagina leyendo el del
// manual (asi no hay una lista de secciones duplicada que se desincronice).

const HTML = '/api/manual';
const PDF = '/api/manual.pdf';

/** Abre un blob (descarga o pestaña nueva) y libera la URL despues. */
function abrirBlob(blob: Blob, nombreDescarga?: string) {
  const url = URL.createObjectURL(blob);
  if (nombreDescarga) {
    const a = document.createElement('a');
    a.href = url;
    a.download = nombreDescarga;
    a.click();
  } else {
    window.open(url, '_blank', 'noopener');
  }
  // La pestaña nueva tarda en leerla: un minuto sobra.
  setTimeout(() => URL.revokeObjectURL(url), 60_000);
}

interface Seccion { id: string; titulo: string }

export function Manual() {
  const marco = useRef<HTMLIFrameElement>(null);
  const { efectivo } = useTema();
  const [listo, setListo] = useState(false);
  const [alto, setAlto] = useState<number | null>(null);
  const [secciones, setSecciones] = useState<Seccion[]>([]);
  const [contenido, setContenido] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [bajando, setBajando] = useState(false);

  useEffect(() => {
    let vigente = true;
    authFetch(HTML)
      .then(async (r) => {
        if (!r.ok) throw new Error(`error ${r.status}`);
        const texto = await r.text();
        if (vigente) setContenido(texto);
      })
      .catch((e: Error) => { if (vigente) setError(`No se pudo cargar el manual (${e.message}).`); });
    return () => { vigente = false; };
  }, []);

  async function descargarPdf() {
    setBajando(true);
    try {
      const r = await authFetch(PDF);
      if (!r.ok) throw new Error(`error ${r.status}`);
      abrirBlob(await r.blob(), 'SEMBAF - Manual de usuario.pdf');
    } catch (e) {
      setError(`No se pudo descargar el PDF (${(e as Error).message}).`);
    } finally {
      setBajando(false);
    }
  }

  // Tema: al cargar y cada vez que cambia en el monitor.
  useEffect(() => {
    const doc = marco.current?.contentDocument;
    if (!listo || !doc) return;
    doc.documentElement.dataset.theme = efectivo === 'oscuro' ? 'dark' : 'light';
  }, [listo, efectivo]);

  useEffect(() => {
    if (!listo) return;
    const ventana = marco.current?.contentWindow as (Window & typeof globalThis) | null | undefined;
    const doc = marco.current?.contentDocument;
    if (!ventana || !doc) return;

    // Alto del contenido: el del body, no el del documento (que nunca baja
    // del alto del marco y lo haria crecer sin fin).
    const medir = () => setAlto(Math.ceil(doc.body.getBoundingClientRect().height));
    const observador = new ventana.ResizeObserver(medir);
    observador.observe(doc.body);
    medir();

    const alClic = (ev: MouseEvent) => {
      const enlace = (ev.target as Element | null)?.closest?.('a');
      const href = enlace?.getAttribute('href');
      if (!enlace || !href) return;
      if (href.startsWith('#')) {
        ev.preventDefault();
        irA(href.slice(1));
      } else {
        // Un enlace externo no reemplaza el manual dentro del marco.
        enlace.setAttribute('target', '_blank');
        enlace.setAttribute('rel', 'noopener');
      }
    };
    doc.addEventListener('click', alClic);
    return () => {
      observador.disconnect();
      doc.removeEventListener('click', alClic);
    };
  }, [listo]);

  function alCargar() {
    const doc = marco.current?.contentDocument;
    if (!doc) return;
    doc.documentElement.classList.add('embebido');
    setSecciones(Array.from(doc.querySelectorAll<HTMLAnchorElement>('nav.toc a[href^="#"]')).map((a) => ({
      id: a.getAttribute('href')!.slice(1),
      titulo: (a.textContent ?? '').replace(/\s+/g, ' ').trim().replace(/^(\d+) /, '$1. '),
    })));
    setListo(true);
    // Abierto con /manual#s7: ir directo a esa seccion.
    if (window.location.hash.length > 1) irA(window.location.hash.slice(1));
  }

  function irA(id: string) {
    const destino = marco.current?.contentDocument?.getElementById(id);
    if (!destino || !marco.current) return;
    // En el celular la barra superior queda fija arriba y taparia el titulo.
    const barra = document.querySelector<HTMLElement>('.barra-superior');
    const tapa = barra && getComputedStyle(barra).position === 'sticky' ? barra.offsetHeight : 0;
    const y = marco.current.getBoundingClientRect().top + window.scrollY
      + destino.getBoundingClientRect().top - tapa - 12;
    window.scrollTo({ top: Math.max(0, y) });
    window.history.replaceState(null, '', `#${id}`);
  }

  return (
    <div className="manual-pagina">
      <section className="panel manual-barra">
        <div>
          <h1 className="manual-titulo">Manual de usuario</h1>
          <p className="subtitle">Operación del equipo, cableado, monitor web y diagnóstico de fallas.</p>
        </div>
        <div className="manual-acciones">
          {secciones.length > 0 && (
            <select className="manual-secciones" aria-label="Ir a una sección" value=""
              onChange={(e) => { if (e.target.value) irA(e.target.value); }}>
              <option value="">Ir a una sección…</option>
              {secciones.map((s) => <option key={s.id} value={s.id}>{s.titulo}</option>)}
            </select>
          )}
          <button type="button" className="boton boton-primario" onClick={descargarPdf} disabled={bajando}>
            <Icono nombre="descargar" tamaño={14} />{bajando ? 'Descargando…' : 'Descargar PDF'}
          </button>
          <button type="button" className="boton" disabled={!contenido}
            onClick={() => contenido && abrirBlob(new Blob([contenido], { type: 'text/html' }))}>
            <Icono nombre="externo" tamaño={14} />Abrir aparte
          </button>
        </div>
      </section>

      {error && <p className="panel empty">{error}</p>}

      <section className="panel panel-manual">
        {!contenido && !error && <p className="empty manual-espera">Cargando el manual…</p>}
        {contenido && <iframe
          ref={marco}
          srcDoc={contenido}
          title="Manual de usuario"
          className={`manual${listo ? '' : ' manual-cargando'}`}
          scrolling="no"
          onLoad={alCargar}
          style={alto ? { height: alto } : undefined}
        />}
      </section>
    </div>
  );
}
