import { useEffect, useState } from 'react';
import { authFetch } from './auth.js';

// La configuracion completa con la que corrio una corrida, por su configId
// (tarjeta 23). Se usa para lo que la telemetria no dice: que bobinas estan
// habilitadas, los umbrales de cada regla del Detector, el multiplicador del
// rango critico de CEM1.
//
// Se cachea por id: una configuracion con un id dado no cambia nunca (el id
// ES su CRC32), asi que pedirla dos veces no tiene sentido.

const cache = new Map<string, unknown>();

export function useConfigDeCorrida(configId: string | null | undefined): unknown {
  const [contenido, setContenido] = useState<unknown>(() => (configId ? cache.get(configId) ?? null : null));

  useEffect(() => {
    if (!configId || configId === 'default') { setContenido(null); return; }
    if (cache.has(configId)) { setContenido(cache.get(configId)); return; }
    let vigente = true;
    authFetch(`/api/config/snapshots/${encodeURIComponent(configId)}`)
      .then((r) => (r.ok ? r.json() : null))
      .then((data) => {
        const valor = data?.content ?? null;
        if (valor) cache.set(configId, valor);
        if (vigente) setContenido(valor);
      })
      .catch(() => { if (vigente) setContenido(null); });
    return () => { vigente = false; };
  }, [configId]);

  return contenido;
}
