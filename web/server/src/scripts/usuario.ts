import mongoose from 'mongoose';
import { hashPassword } from '../auth/password.js';
import { config } from '../config.js';
import { User } from '../models/user.js';

// Alta y cambio de contraseña de usuarios, desde la consola:
//
//   npm run usuario --workspace server -- mario una-clave-larga
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
  await existente.save();
  console.log(`contraseña actualizada para "${username}"`);
} else {
  await User.create({ username: username.toLowerCase(), passwordHash });
  console.log(`usuario "${username}" creado`);
}

await mongoose.disconnect();
