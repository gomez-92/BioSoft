import mongoose from 'mongoose';

// Conexion a Mongo para los tests que escriben de verdad. Apunta al Mongo del
// docker-compose y NUNCA a la base de la aplicacion.
//
// Cada archivo de test pide SU PROPIA base. Vitest corre los archivos en
// paralelo, en procesos distintos: compartiendo una sola base, el `deleteMany`
// de un archivo borra los datos que el otro acaba de escribir y las fallas
// aparecen y desaparecen segun el orden en que arranquen los workers. Ya paso
// una vez, y desde afuera parece un bug de la correlacion de corridas.
//
// Se descarto mongodb-memory-server: descarga un mongod de 780 MB para
// levantar lo que el compose ya tiene andando.
export async function connectTestDb(nombre: string): Promise<boolean> {
  const base = process.env.MONGO_TEST_URI ?? 'mongodb://localhost:27017';
  const uri = `${base.replace(/\/+$/, '')}/biosoft_test_${nombre}`;
  try {
    await mongoose.connect(uri, { serverSelectionTimeoutMS: 3000 });
    return true;
  } catch {
    // Si Mongo no esta, la suite se SALTEA con un mensaje que dice que hacer,
    // en vez de fallar como si el codigo estuviera roto.
    console.warn(
      `[tests] Mongo no responde en ${uri}: se saltea esta suite. ` +
      'Levantalo con "npm run infra:up" desde web/ y volve a correrla.',
    );
    return false;
  }
}

export async function disconnectTestDb(conectado: boolean): Promise<void> {
  if (conectado) await mongoose.disconnect();
}
