import { config } from '../config.js';
import { User } from '../models/user.js';
import { hashPassword } from './password.js';

/**
 * Crea el primer usuario a partir de variables de entorno, solo si no hay
 * ninguno. Existe para el despliegue: un servidor recien levantado en la nube
 * tiene que poder recibir su primera cuenta sin que nadie entre por consola.
 *
 * NO pisa una cuenta existente ni cambia contraseñas: si ya hay usuarios, no
 * hace nada. Asi, dejar la variable puesta en el entorno no revierte un cambio
 * de contraseña hecho despues.
 */
export async function seedInitialUser(): Promise<void> {
  const { seedUser, seedPassword } = config.auth;
  if (!seedUser || !seedPassword) return;

  if (await User.countDocuments() > 0) return;

  await User.create({
    username: seedUser.toLowerCase(),
    passwordHash: await hashPassword(seedPassword),
    // La primera cuenta es la que crea a las demas: sin el rol, el monitor
    // arrancaria sin nadie capaz de gestionar usuarios.
    role: 'admin',
  });
  console.log(`[auth] usuario inicial "${seedUser}" creado desde el entorno`);
}
