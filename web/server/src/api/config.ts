import { Router } from 'express';
import { asincrono } from './asincrono.js';
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
  const items = await ConfigRequest.find().sort({ createdAt: -1 }).limit(20)
    .select('-text').lean();
  res.json({ inProgress: pedidoEnCurso(), items });
}));

configRouter.post('/config/requests', asincrono(async (req, res) => {
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
