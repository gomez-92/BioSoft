import { Router } from 'express';
import mongoose from 'mongoose';
import { asincrono } from './asincrono.js';
import { requireAdmin } from '../auth/middleware.js';
import { Scenario } from '../models/index.js';
import { EscenarioInvalido, fila, leerEscenario, restaurarFabrica } from '../domain/escenarios.js';
import { ESCENARIOS_FABRICA } from '../domain/escenariosfabrica.js';

// Biblioteca de escenarios de prueba (ver domain/escenarios.ts). Leer es para
// cualquier usuario con sesion (el generador los muestra tambien en solo
// lectura); crear, editar, borrar y restaurar los de fabrica, solo admin: es
// lo que despues se manda a la placa.
export const escenariosRouter = Router();

async function listar() {
  const items = await Scenario.find().lean();
  // Primero los de fabrica en el orden de tools/escenarios.json, despues los
  // propios por nombre.
  const orden = (k: string | null | undefined) => {
    const i = ESCENARIOS_FABRICA.profiles.findIndex((p) => p.key === k);
    return i < 0 ? Number.MAX_SAFE_INTEGER : i;
  };
  items.sort((a, b) => orden(a.factoryKey) - orden(b.factoryKey) || a.name.localeCompare(b.name, 'es'));
  return items.map(fila);
}

function errorDe(error: unknown, res: import('express').Response): boolean {
  if (error instanceof EscenarioInvalido) { res.status(400).json({ error: error.message }); return true; }
  if ((error as { code?: number })?.code === 11000) {
    res.status(409).json({ error: 'ya existe un escenario con ese nombre' });
    return true;
  }
  return false;
}

escenariosRouter.get('/scenarios', asincrono(async (_req, res) => {
  res.json({ items: await listar() });
}));

escenariosRouter.post('/scenarios', requireAdmin, asincrono(async (req, res) => {
  try {
    const entrada = leerEscenario(req.body);
    const creado = await Scenario.create({ ...entrada, updatedBy: req.user?.username ?? 'desconocido' });
    console.log(`[escenarios] ${req.user?.username} creo "${entrada.name}"`);
    res.status(201).json({ item: fila(creado.toObject()), items: await listar() });
  } catch (error) {
    if (!errorDe(error, res)) throw error;
  }
}));

escenariosRouter.put('/scenarios/:id', requireAdmin, asincrono(async (req, res) => {
  if (!mongoose.isValidObjectId(req.params.id)) { res.status(404).json({ error: 'escenario no encontrado' }); return; }
  try {
    const entrada = leerEscenario(req.body);
    // Un escenario de fabrica editado conserva su factoryKey (para poder
    // restaurarlo), pero su promesa deja de mostrarse (fila()).
    const actualizado = await Scenario.findByIdAndUpdate(
      req.params.id, { $set: { ...entrada, updatedBy: req.user?.username ?? 'desconocido' } }, { new: true },
    ).lean();
    if (!actualizado) { res.status(404).json({ error: 'escenario no encontrado' }); return; }
    console.log(`[escenarios] ${req.user?.username} edito "${entrada.name}"`);
    res.json({ item: fila(actualizado), items: await listar() });
  } catch (error) {
    if (!errorDe(error, res)) throw error;
  }
}));

escenariosRouter.delete('/scenarios/:id', requireAdmin, asincrono(async (req, res) => {
  if (!mongoose.isValidObjectId(req.params.id)) { res.status(404).json({ error: 'escenario no encontrado' }); return; }
  const borrado = await Scenario.findByIdAndDelete(req.params.id).lean();
  if (!borrado) { res.status(404).json({ error: 'escenario no encontrado' }); return; }
  console.log(`[escenarios] ${req.user?.username} borro "${borrado.name}"`);
  res.json({ items: await listar() });
}));

escenariosRouter.post('/scenarios/restore', requireAdmin, asincrono(async (req, res) => {
  await restaurarFabrica(req.user?.username ?? 'desconocido');
  console.log(`[escenarios] ${req.user?.username} restauro los de fabrica`);
  res.json({ items: await listar() });
}));
