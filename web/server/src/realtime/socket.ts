import type { Server as HttpServer } from 'node:http';
import { Server } from 'socket.io';

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

  io.on('connection', (socket) => {
    console.log(`[socket] cliente conectado (${socket.id})`);
    socket.on('disconnect', () => console.log(`[socket] cliente desconectado (${socket.id})`));
  });
}

export type RealtimeEvent = 'measures' | 'coils' | 'status' | 'targets' | 'alert' | 'result';

export function emitTelemetry(event: RealtimeEvent, data: unknown): void {
  io?.emit(`telemetry:${event}`, data);
}

export function realtimeStatus() {
  return { clients: io?.engine.clientsCount ?? 0 };
}
