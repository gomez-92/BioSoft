import { Router } from 'express';
import { fileURLToPath } from 'node:url';

// Manual de usuario: SOLO para quien tiene cuenta en el monitor (decision del
// autor, 2026-11-25). Por eso no esta en client/public/ -- todo lo de ahi lo
// sirve express.static a cualquiera que tenga la direccion -- sino aca, detras
// de requireAuth. El front lo pide con el token (authFetch) y lo muestra en un
// marco con srcdoc; el PDF lo baja como blob.
//
// La carpeta server/manual/ es una COPIA de docs/ (el contenedor se construye
// solo con web/): la mantiene `npm run sync:generador` y test/manual.test.ts
// falla si difiere. Se resuelve desde este modulo y no desde el cwd: vale
// igual para src/api (tsx) y dist/api (node), los dos dos niveles adentro.
export const CARPETA_MANUAL = fileURLToPath(new URL('../../manual/', import.meta.url));

export const manualRouter = Router();

function enviar(archivo: string, tipo: string) {
  return (_req: unknown, res: import('express').Response) => {
    // Privado y sin cache: cambia con cada version y no debe quedar en un
    // proxy intermedio.
    res.setHeader('Cache-Control', 'private, no-cache');
    res.type(tipo);
    res.sendFile(archivo, { root: CARPETA_MANUAL }, (error) => {
      if (error && !res.headersSent) res.status(404).json({ error: 'manual no disponible' });
    });
  };
}

manualRouter.get('/manual', enviar('manual-de-usuario.html', 'html'));
manualRouter.get('/manual.pdf', enviar('manual-de-usuario.pdf', 'pdf'));
