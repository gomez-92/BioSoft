import { Router } from 'express';
import { asincrono } from './asincrono.js';
import { requireAdmin } from '../auth/middleware.js';
import { ConfigRequest, ConfigSnapshot } from '../models/index.js';
import { PedidoInvalido, enviarConfiguracion, pedidoEnCurso } from '../domain/configsync.js';

// Configuracion remota (tarjeta 24). Leer la vigente y mandar una nueva. Todo
// detras del login (lo monta index.ts con requireAuth).
export const configRouter = Router();

/** La ultima configuracion que la placa informo como vigente. */
configRouter.get('/config/current', asincrono(async (_req, res) => {
  const snapshot = await ConfigSnapshot.findOne().sort({ lastReportedAt: -1 }).lean();
  if (!snapshot) { res.json({ configId: null }); return; }
  res.json({
    configId: snapshot.configId,
    reportedAt: snapshot.lastReportedAt ?? null,
    content: snapshot.content ?? null,
    loadStatus: snapshot.loadStatus ?? null,
  });
}));

/** Una configuracion por id: la del detalle de una corrida (tarjeta 23). */
configRouter.get('/config/snapshots/:id', asincrono(async (req, res) => {
  const snapshot = await ConfigSnapshot.findOne({ configId: req.params.id }).lean();
  if (!snapshot) { res.status(404).json({ error: 'configuracion no encontrada' }); return; }
  res.json({
    configId: snapshot.configId,
    reportedAt: snapshot.lastReportedAt ?? null,
    content: snapshot.content ?? null,
  });
}));

configRouter.get('/config/requests', asincrono(async (_req, res) => {
  const items: Array<Record<string, unknown>> = await ConfigRequest.find().sort({ createdAt: -1 })
    .limit(20).select('-text').lean();
  // El ULTIMO envio lleva su contenido: el generador lo muestra como
  // "Enviado" (grabado en la SD, todavia no vigente) aparte de lo que corre
  // en la placa y de lo que se esta editando. Los demas no lo necesitan, y
  // son ~7 KB cada uno.
  if (items.length > 0) {
    const ultimo = await ConfigRequest.findOne({ requestId: items[0].requestId }).select('text').lean();
    try {
      items[0] = { ...items[0], content: ultimo?.text ? JSON.parse(ultimo.text) : null };
    } catch {
      items[0] = { ...items[0], content: null };
    }
  }
  res.json({ inProgress: pedidoEnCurso(), items });
}));

// Mandar configuracion cambia lo que la placa hace en el proximo reinicio:
// solo administradores.
configRouter.post('/config/requests', requireAdmin, asincrono(async (req, res) => {
  try {
    const resultado = await enviarConfiguracion(req.body?.config, req.user?.username ?? 'desconocido');
    res.status(202).json(resultado);
  } catch (error) {
    if (error instanceof PedidoInvalido) {
      res.status(pedidoEnCurso() ? 409 : 400).json({ error: error.message });
      return;
    }
    throw error;
  }
}));
