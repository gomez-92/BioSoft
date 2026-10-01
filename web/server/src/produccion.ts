import { existsSync } from 'node:fs';
import path from 'node:path';
import express, { type Express } from 'express';
import { config } from './config.js';

// Todo lo que cambia entre correr esto en la notebook y correrlo en la nube.

/**
 * En produccion el backend sirve tambien el front ya compilado, desde el mismo
 * origen. No es solo comodidad: con un unico origen no hay CORS que configurar,
 * el token viaja sin preflight, y el WebSocket se conecta al mismo host sin
 * ninguna URL absoluta compilada adentro del bundle -- que seria justo lo que
 * habria que recompilar para cambiar de dominio.
 */
export function servirCliente(app: Express): void {
  const dist = path.resolve(process.cwd(), '../client/dist');
  if (!existsSync(dist)) {
    console.warn(`[http] no hay front compilado en ${dist}: se sirve solo la API`);
    return;
  }

  app.use(express.static(dist, {
    // El HTML no se cachea: es lo que apunta a los assets con hash, asi que
    // un index.html viejo en el cache del navegador serviria la version
    // anterior de la app entera aunque el despliegue haya salido bien.
    setHeaders: (res, ruta) => {
      if (ruta.endsWith('.html')) res.setHeader('Cache-Control', 'no-cache');
      else res.setHeader('Cache-Control', 'public, max-age=31536000, immutable');
    },
  }));

  // Cualquier ruta que no sea de la API devuelve el index: el front usa rutas
  // reales (/historial, /corrida/:id), y sin esto recargar la pagina o
  // compartir un link daria 404 -- el servidor buscaria un archivo que no
  // existe porque esas rutas solo existen dentro del navegador.
  app.get(/^(?!\/api\/).*/, (_req, res) => {
    res.sendFile(path.join(dist, 'index.html'));
  });
}

/**
 * Cabeceras de seguridad. Son cuatro lineas en vez de una dependencia entera:
 * lo que hace falta aca es poco y explicito.
 */
export function cabecerasSeguras(app: Express): void {
  app.use((_req, res, next) => {
    // Impide que el navegador adivine el tipo de un archivo servido.
    res.setHeader('X-Content-Type-Options', 'nosniff');
    // Nadie embebe este monitor en un iframe ajeno. SAMEORIGIN y no DENY:
    // la seccion Configuracion embebe el generador (/generador-config.html,
    // tarjeta 24) desde el mismo origen, y DENY bloqueaba tambien ese.
    res.setHeader('X-Frame-Options', 'SAMEORIGIN');
    // No filtrar la URL completa (que lleva el id de corrida) a terceros.
    res.setHeader('Referrer-Policy', 'same-origin');
    next();
  });
}

/**
 * Comprobaciones que tienen que hacer fallar el arranque en produccion, no
 * quedar en una linea de log que nadie lee.
 */
export function verificarProduccion(): void {
  if (process.env.NODE_ENV !== 'production') return;

  const problemas: string[] = [];

  // Sin JWT_SECRET la clave se genera al azar en cada arranque: cada
  // despliegue cerraria todas las sesiones abiertas, y con varias instancias
  // el token emitido por una no valdria en la otra.
  if (!process.env.JWT_SECRET) {
    problemas.push('falta JWT_SECRET (generar con: openssl rand -hex 48)');
  }
  if (process.env.JWT_SECRET && process.env.JWT_SECRET.length < 32) {
    problemas.push('JWT_SECRET es demasiado corto (minimo 32 caracteres)');
  }
  // Un broker sin TLS en produccion manda las credenciales del broker y la
  // telemetria en texto plano por internet.
  if (config.mqtt.url.startsWith('mqtt://') && !config.mqtt.url.includes('localhost')) {
    problemas.push(`el broker no usa TLS (${config.mqtt.url}): usar mqtts://`);
  }

  if (problemas.length > 0) {
    console.error('[arranque] configuracion invalida para produccion:');
    for (const problema of problemas) console.error(`  - ${problema}`);
    process.exit(1);
  }
}
