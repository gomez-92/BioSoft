import mongoose from 'mongoose';
import { config } from '../config.js';

// La conexion NO bloquea el arranque del servidor. Si Mongo no esta, el
// backend igual levanta, sigue recibiendo MQTT y lo dice en /api/health: para
// diagnosticar sirve mucho mas una API que responde "mongo: desconectado" que
// un proceso que murio al arrancar y no contesta nada.
export function connectMongo(): void {
  mongoose.connection.on('connected', () => console.log('[mongo] conectado'));
  mongoose.connection.on('disconnected', () => console.warn('[mongo] desconectado'));
  mongoose.connection.on('error', (error) => console.error('[mongo] error:', error.message));

  mongoose.connect(config.mongoUri).catch((error) => {
    console.error('[mongo] no se pudo conectar:', error.message);
  });
}

export function mongoStatus(): { connected: boolean; state: string } {
  const states = ['desconectado', 'conectado', 'conectando', 'desconectando'];
  return {
    connected: mongoose.connection.readyState === 1,
    state: states[mongoose.connection.readyState] ?? 'desconocido',
  };
}
