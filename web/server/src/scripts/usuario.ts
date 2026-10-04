import mongoose from 'mongoose';
import { hashPassword } from '../auth/password.js';
import { config } from '../config.js';
import { User } from '../models/user.js';

// Alta y cambio de contraseña de usuarios, desde la consola:
//
//   npm run usuario --workspace server -- mario una-clave-larga
//
// Una cuenta NUEVA creada por aca es administradora: la consola del servidor
// es el camino de ultimo recurso (por ejemplo, si nadie recuerda la clave de
// ningun administrador), y una cuenta sin rol no serviria para eso. Para el
// resto de los usuarios esta la pagina Usuarios del monitor.
//
// Cambiar la contraseña desde aca invalida las sesiones abiertas de esa
// cuenta recien cuando el servidor se reinicia (la tabla de revocacion vive
// en su memoria, ver auth/sesiones.ts); desde la web es inmediato.
//
// La contraseña viaja como argumento, asi que queda en el historial de la
// terminal. Para una cuenta de produccion conviene cambiarla despues desde un
// entorno que no se registre, o crearla con la semilla por variables de
// entorno (ver auth/seed.ts).
const [username, password] = process.argv.slice(2);

if (!username || !password) {
  console.error('uso: npm run usuario --workspace server -- <usuario> <contraseña>');
  process.exit(1);
}
if (password.length < 8) {
  console.error('la contraseña tiene que tener al menos 8 caracteres');
  process.exit(1);
}

await mongoose.connect(config.mongoUri);

const passwordHash = await hashPassword(password);
const existente = await User.findOne({ username: username.toLowerCase() });

if (existente) {
  existente.passwordHash = passwordHash;
  existente.tokensValidAfter = new Date(Math.floor(Date.now() / 1000) * 1000);
  await existente.save();
  console.log(`contraseña actualizada para "${username}"`);
} else {
  await User.create({ username: username.toLowerCase(), passwordHash, role: 'admin' });
  console.log(`usuario "${username}" creado (administrador)`);
}

await mongoose.disconnect();
