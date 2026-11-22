import { Router } from 'express';
import { asincrono } from './asincrono.js';
import { requireAdmin } from '../auth/middleware.js';
import { buscarEventos, CorridaInexistente, type AlcanceEventos, type NivelEvento } from '../domain/bitacora.js';

// Bitacora de la placa. Solo administradores (decision del 2026-11-22: es
// diagnostico del sistema, no del experimento). El
// texto igual no lleva SSID, host del broker ni nada de la configuracion
// (esp32/src/eventlog.hpp): la placa no sabe quien lo va a leer.
export const bitacoraRouter = Router();

const MAX_PAGINA = 200;

function fecha(valor: unknown): Date | undefined {
  if (typeof valor !== 'string' || valor === '') return undefined;
  const date = new Date(valor);
  return Number.isNaN(date.getTime()) ? undefined : date;
}

/**
 *   GET /api/events?level=E,W&code=broker_caido&scope=in|out&run=<id>&number=42&from=&to=&before=&limit=50
 * `before` pagina hacia atras (la fecha del ultimo evento de la pagina anterior).
 * `scope` = durante alguna corrida / fuera de corridas; `run` = solo esa
 * corrida (404 si no existe); `number` = la corrida con ese numero ("42" o
 * "#42"; 404 si no hay), y la respuesta trae `run` con cual fue. Cada evento
 * trae `run`: la corrida en curso a su hora, o null.
 */
bitacoraRouter.get('/events', requireAdmin, asincrono(async (req, res) => {
  const limit = Math.min(MAX_PAGINA, Math.max(1, Number(req.query.limit ?? 50) || 50));
  const levels = typeof req.query.level === 'string'
    ? req.query.level.split(',').filter((l): l is NivelEvento => l === 'E' || l === 'W' || l === 'I')
    : undefined;
  const code = typeof req.query.code === 'string' && /^[a-z0-9_]{1,32}$/.test(req.query.code)
    ? req.query.code
    : undefined;
  const scope: AlcanceEventos = req.query.scope === 'in' || req.query.scope === 'out' ? req.query.scope : 'all';
  const runId = typeof req.query.run === 'string' && req.query.run !== '' ? req.query.run : undefined;
  const numero = typeof req.query.number === 'string' ? Number(req.query.number.replace(/^#/, '')) : NaN;
  const runNumber = Number.isInteger(numero) && numero > 0 ? numero : undefined;
  try {
    res.json(await buscarEventos({
      levels, code, limit, scope, runId, runNumber,
      from: fecha(req.query.from), to: fecha(req.query.to), before: fecha(req.query.before),
    }));
  } catch (error) {
    if (error instanceof CorridaInexistente) {
      res.status(404).json({ error: runNumber ? `no hay ninguna corrida #${runNumber}` : 'no existe esa corrida' });
      return;
    }
    throw error;
  }
}));
