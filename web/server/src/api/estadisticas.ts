import { Router } from 'express';
import mongoose from 'mongoose';
import { asincrono } from './asincrono.js';
import { calcularEstadisticas } from '../domain/estadisticas.js';
import { User } from '../models/user.js';

// Estadisticas de la pagina Inicio. Cualquier sesion las lee; la parte del
// equipo (sale de la bitacora) solo la reciben los administradores, con el
// rol leido de la base como en requireAdmin.
export const estadisticasRouter = Router();

estadisticasRouter.get('/stats', asincrono(async (req, res) => {
  let esAdmin = false;
  if (mongoose.isValidObjectId(req.user?.sub)) {
    const usuario = await User.findById(req.user?.sub).select('role').lean();
    esAdmin = usuario?.role === 'admin';
  }
  res.json(await calcularEstadisticas(esAdmin));
}));
