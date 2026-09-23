import type { Server as HttpServer } from 'node:http';
import { Server } from 'socket.io';
import { onTelemetry } from '../mqtt/ingestor.js';

// El navegador NUNCA habla con el broker: este es el unico camino por el que
// le llega un dato en vivo. Asi las credenciales del broker no salen del
// servidor y lo que se ve en vivo es lo mismo que se persistio.
//
// FASE 0: reemite lo que llega sin persistir. A partir de la fase 1 el que
// emite es el handler, DESPUES de guardar, para que vivo e historico no
// puedan diferir.
let io: Server | undefined;

export function startRealtime(server: HttpServer): void {
  io = new Server(server, { cors: { origin: true } });

  io.on('connection', (socket) => {
    console.log(`[socket] cliente conectado (${socket.id})`);
    socket.on('disconnect', () => console.log(`[socket] cliente desconectado (${socket.id})`));
  });

  onTelemetry((message) => {
    // Un retenido no es un evento nuevo: es el ultimo estado conocido que el
    // broker reentrega al suscribirse. Va a alimentar el `snapshot` inicial
    // (fase 3), no el flujo en vivo.
    if (message.retained) return;
    io?.emit(`telemetry:${message.group}`, {
      receivedAt: message.receivedAt.toISOString(),
      payload: message.payload,
    });
  });
}

export function realtimeStatus() {
  return { clients: io?.engine.clientsCount ?? 0 };
}
