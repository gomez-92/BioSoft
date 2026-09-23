import { Schema, model } from 'mongoose';

// Usuarios del monitor. Son pocos y no hay registro publico: los crea el
// script `npm run usuario` o la semilla inicial desde variables de entorno.
//
// El monitor es de SOLO LECTURA (la placa publica y nadie le manda comandos),
// asi que no hay roles ni permisos: quien entra, ve. Un rol que no distingue
// nada seria decoracion.
const userSchema = new Schema({
  username: { type: String, required: true, unique: true, lowercase: true, trim: true },
  // Nunca la contraseña: solo su hash con scrypt, con el salt y los parametros
  // adentro (ver auth/password.ts).
  passwordHash: { type: String, required: true },
  createdAt: { type: Date, default: Date.now },
  lastLoginAt: Date,
}, { versionKey: false });

export const User = model('User', userSchema);
