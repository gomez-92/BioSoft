import type { Server as HttpServer } from 'node:http';
import mongoose from 'mongoose';
import { Server } from 'socket.io';
import { sesionVigente } from '../auth/sesiones.js';
import { verifyToken } from '../auth/tokens.js';
import { config } from '../config.js';
import { buildSnapshot } from '../domain/snapshot.js';

// El navegador NUNCA habla con el broker: este es el unico camino por el que
// le llega un dato en vivo. Asi las credenciales del broker no salen del
// servidor y lo que se ve en vivo es lo mismo que se persistio.
//
// Este modulo no escucha MQTT: emite lo que le pasa el handler de ingesta,
// DESPUES de guardar (ver mqtt/handlers.ts). Que el orden sea ese es lo que
// hace imposible que el tablero muestre una medicion que no quedo en la base.
let io: Server | undefined;

export function startRealtime(server: HttpServer): void {
  io = new Server(server, {
    // Mismo criterio que el REST: sin origenes configurados, solo el propio.
    cors: config.corsOrigins.length > 0 ? { origin: config.corsOrigins } : { origin: false },
  });

  // El WebSocket exige el mismo token que el REST. Cerrar solo las rutas HTTP
  // dejaria toda la telemetria en vivo accesible a cualquiera que abriera un
  // socket: la puerta de atras del mismo dato.
  io.use((socket, next) => {
    const token = socket.handshake.auth?.token;
    const payload = typeof token === 'string' ? verifyToken(token) : null;
    if (payload && sesionVigente(payload)) return next();
    next(new Error('no autorizado'));
  });

  io.on('connection', async (socket) => {
    console.log(`[socket] cliente conectado (${socket.id})`);
    socket.on('disconnect', () => console.log(`[socket] cliente desconectado (${socket.id})`));

    // Lo primero que recibe un cliente es el estado completo. Sin esto, una
    // pantalla recien abierta se queda vacia hasta la proxima tanda -- con el
    // intervalo por defecto, hasta 30 segundos mirando guiones durante un
    // experimento que esta corriendo perfectamente.
    if (mongoose.connection.readyState !== 1) return;
    try {
      socket.emit('snapshot', await buildSnapshot());
    } catch (error) {
      console.error('[socket] no se pudo armar el snapshot:', error);
    }
  });
}

export type RealtimeEvent =
  | 'measures' | 'coils' | 'status' | 'targets' | 'alert' | 'result';

export function emitTelemetry(event: RealtimeEvent, data: unknown): void {
  io?.emit(`telemetry:${event}`, data);
}

// Estado del enlace con el broker. Es informacion propia del backend, no
// telemetria: desde el navegador es la diferencia entre "el experimento no
// esta publicando" y "nosotros no estamos escuchando".
/** Configuracion remota (tarjeta 24): vigente y estado de los envios. */
export function emitConfig(evento: string, data: unknown): void {
  io?.emit(evento, data);
}

export function emitLinkStatus(broker: boolean): void {
  io?.emit('link:status', { broker });
}

export function realtimeStatus() {
  return { clients: io?.engine.clientsCount ?? 0 };
}
