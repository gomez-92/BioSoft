import { createServer } from 'node:http';
import mongoose from 'mongoose';
import cors from 'cors';
import express from 'express';
import { manejadorDeErrores, requiereBase } from './api/asincrono.js';
import { authRouter } from './api/auth.js';
import { healthRouter } from './api/health.js';
import { liveRouter } from './api/live.js';
import { runsRouter } from './api/runs.js';
import { config } from './config.js';
import { requireAuth } from './auth/middleware.js';
import { seedInitialUser } from './auth/seed.js';
import { connectMongo } from './db/mongo.js';
import { initRunTracker, sweepStaleRuns } from './domain/runtracker.js';
import { registerHandlers } from './mqtt/handlers.js';
import { mqttStatus, startIngestor, stopIngestor } from './mqtt/ingestor.js';
import { cabecerasSeguras, servirCliente, verificarProduccion } from './produccion.js';
import { startRealtime } from './realtime/socket.js';

// Antes que nada: si la configuracion de produccion esta mal, mejor no
// arrancar que arrancar a medias.
verificarProduccion();

const app = express();

// Detras del proxy de la plataforma, req.ip es la IP del proxy salvo que se
// confie en X-Forwarded-For. Importa: el limite de intentos de login se cuenta
// por IP, y con todas las peticiones viniendo de la "misma" IP, un solo
// atacante bloquearia a todos los usuarios.
app.set('trust proxy', 1);

cabecerasSeguras(app);
// Sin origenes configurados no se habilita CORS: en produccion el front sale
// del mismo servidor y no hace falta.
app.use(cors(config.corsOrigins.length > 0 ? { origin: config.corsOrigins } : { origin: false }));
app.use(express.json({ limit: '16kb' }));
// Lo unico publico. Ademas del latido informa si los DOS ENLACES estan
// arriba, con booleanos y nada mas: ni URLs, ni nombres, ni cantidades.
//
// Que diga eso sin pedir token es a proposito. /api/health lo dice con
// detalle, pero esta detras del login -- y cuando la base esta caida NO SE
// PUEDE INICIAR SESION, asi que el unico endpoint capaz de explicar por que
// nada funciona quedaba inaccesible justo cuando hacia falta. Paso en el
// primer despliegue.
app.get('/api/ping', (_req, res) => {
  res.json({
    ok: true,
    mongo: mongoose.connection.readyState === 1,
    mqtt: mqttStatus().connected,
  });
});

app.use('/api', requiereBase, authRouter);

// De aca para abajo todo exige token. /api/health incluido: dice la URL del
// broker, el clientId y cuantos datos hay guardados -- poco para un atacante,
// pero no hay ninguna razon para regalarlo.
app.use('/api', requireAuth, healthRouter);
app.use('/api', requireAuth, requiereBase, liveRouter);
app.use('/api', requireAuth, requiereBase, runsRouter);

// El front va DESPUES de las rutas de la API: su comodin atrapa todo lo que
// no empiece con /api.
servirCliente(app);

app.use(manejadorDeErrores);

const server = createServer(app);

connectMongo();
startRealtime(server);   // antes del ingestor: el handler emite apenas guarda
registerHandlers();

// El tracker se reconstruye ANTES de suscribirse: si el backend se reinicio a
// mitad de un experimento, las muestras que lleguen tienen que caer en la
// corrida que ya estaba abierta y no en una nueva.
mongoose.connection.once('connected', () => {
  seedInitialUser().catch((error) => console.error('[auth] fallo la semilla:', error));
  initRunTracker()
    .then(() => startIngestor())
    .catch((error) => {
      console.error('[app] no se pudo recuperar la corrida abierta:', error);
      startIngestor();
    });
});

// El barrido corre por intervalo, no por evento, porque el caso que atiende es
// justamente que dejaron de llegar eventos.
const barrido = setInterval(() => {
  if (mongoose.connection.readyState !== 1) return;
  sweepStaleRuns().catch((error) => console.error('[corridas] fallo el barrido:', error));
}, config.run.sweepIntervalSeconds * 1000);
barrido.unref();

server.listen(config.port, () => {
  console.log(`[http] escuchando en http://localhost:${config.port}`);
});

async function shutdown(signal: string): Promise<void> {
  console.log(`[app] ${signal} recibido, cerrando`);
  await stopIngestor();
  server.close(() => process.exit(0));
}

// Una promesa rechazada que nadie atrapa TERMINA el proceso en Node por
// defecto. En un servicio que la plataforma reinicia solo, eso se convierte en
// un bucle de reinicios cuya causa no aparece por ningun lado. Se registra y
// se sigue: un pedido fallido es mejor que el servidor entero cayendose.
process.on('unhandledRejection', (razon) => {
  console.error('[app] promesa rechazada sin manejar:', razon);
});

process.on('SIGINT', () => void shutdown('SIGINT'));
process.on('SIGTERM', () => void shutdown('SIGTERM'));
