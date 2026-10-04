import { Schema, model } from 'mongoose';

// Usuarios del monitor. Son pocos y no hay registro publico: los crea un
// administrador desde la pagina Usuarios, el script `npm run usuario` o la
// semilla inicial desde variables de entorno.
//
// Dos roles, y distinguen algo real:
//   - "viewer": ve todo (en vivo, historial, configuracion vigente). Es lo que
//     el monitor era entero antes de tener caminos de escritura.
//   - "admin": ademas manda configuracion a la placa, borra corridas y
//     gestiona usuarios. Esos tres caminos son los unicos que cambian algo,
//     y por eso son los unicos que el rol protege.
export const ROLES = ['admin', 'viewer'] as const;
export type Rol = typeof ROLES[number];

const userSchema = new Schema({
  username: { type: String, required: true, unique: true, lowercase: true, trim: true },
  // Opcional: solo sirve para mandar el enlace de recuperacion. Sin correo
  // (o sin SMTP configurado), el enlace lo genera un administrador.
  email: { type: String, lowercase: true, trim: true, default: null },
  // Las cuentas anteriores a los roles no tienen este campo: la migracion de
  // arranque (auth/sesiones.ts) las pasa a "admin", porque antes de los roles
  // todas podian hacer todo.
  role: { type: String, enum: ROLES, default: 'viewer' },
  // Nunca la contraseña: solo su hash con scrypt, con el salt y los parametros
  // adentro (ver auth/password.ts).
  passwordHash: { type: String, required: true },
  // Los tokens emitidos ANTES de esta fecha dejan de valer. Se mueve al
  // cambiar o restablecer la contraseña: una sesion abierta con la clave
  // vieja (la que se queria revocar) no puede seguir entrando 7 dias mas.
  tokensValidAfter: { type: Date, default: null },
  // Enlace de recuperacion vigente. Solo el HASH del token: quien lea la base
  // no puede usarlo para entrar.
  resetTokenHash: { type: String, default: null },
  resetTokenExpiresAt: { type: Date, default: null },
  createdAt: { type: Date, default: Date.now },
  lastLoginAt: Date,
}, { versionKey: false });

export const User = model('User', userSchema);
