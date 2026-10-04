import nodemailer, { type Transporter } from 'nodemailer';
import { config } from '../config.js';

// Envio del enlace de recuperacion por correo. Es OPCIONAL: sin SMTP_URL y
// PUBLIC_URL el monitor anda igual, y el enlace lo genera un administrador
// desde la pagina Usuarios.
//
// PUBLIC_URL es obligatoria para mandar correos, y no se deduce del pedido:
// el encabezado Host lo elige quien hace el pedido, y armar el enlace con el
// le permitiria a cualquiera pedir la recuperacion de otra cuenta con su
// propio dominio en el Host -- el correo legitimo llevaria a la victima a un
// sitio ajeno con el token en la URL ("password reset poisoning").

export function correoDisponible(): boolean {
  return Boolean(config.mail.smtpUrl && config.mail.publicUrl);
}

let transporte: Transporter | null = null;

export function enlaceDeClave(token: string): string {
  return `${config.mail.publicUrl.replace(/\/+$/, '')}/restablecer?token=${encodeURIComponent(token)}`;
}

export async function enviarCorreoDeClave(destino: string, usuario: string, token: string, horas: number): Promise<void> {
  if (!correoDisponible()) return;
  transporte ??= nodemailer.createTransport(config.mail.smtpUrl);
  const enlace = enlaceDeClave(token);
  await transporte.sendMail({
    from: config.mail.from,
    to: destino,
    subject: 'BioSoft: restablecer contraseña',
    text:
      `Hola ${usuario}:\n\n` +
      `Para fijar una contraseña nueva en el monitor de BioSoft, abrí este enlace:\n\n${enlace}\n\n` +
      `Vale por ${horas} horas y una sola vez. Si no lo pediste, ignorá este correo: ` +
      'tu contraseña actual sigue funcionando.\n',
  });
}
