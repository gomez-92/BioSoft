import type { Server as HttpServer } from 'node:http';
import mongoose from 'mongoose';
import { Server } from 'socket.io';
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
  io = new Server(server, { cors: { origin: true } });

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
export function emitLinkStatus(broker: boolean): void {
  io?.emit('link:status', { broker });
}

export function realtimeStatus() {
  return { clients: io?.engine.clientsCount ?? 0 };
}
