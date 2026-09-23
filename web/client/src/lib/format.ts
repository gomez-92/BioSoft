// Formato de los valores que se muestran. Dos reglas que valen para todos:
//
//  - Un valor ausente se muestra como "--", nunca como 0: durante un
//    experimento, 0 mT es una medicion posible y real, y confundirla con
//    "todavia no llego nada" es exactamente el error que hay que evitar.
//  - La ANTIGUEDAD del dato se muestra siempre. La placa publica cada 30 s por
//    defecto y con QoS 0, asi que un numero en pantalla puede ser viejo sin
//    que nada mas lo delate.

export function numero(value: number | undefined, decimales = 2): string {
  return value === undefined || value === null ? '--' : value.toFixed(decimales);
}

export function duracion(segundos: number | undefined): string {
  if (segundos === undefined || segundos < 0) return '--';
  const h = Math.floor(segundos / 3600);
  const m = Math.floor((segundos % 3600) / 60);
  const s = Math.floor(segundos % 60);
  if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m`;
  if (m > 0) return `${m}m ${String(s).padStart(2, '0')}s`;
  return `${s}s`;
}

export function antiguedad(iso: string | undefined, ahora: number): string {
  if (!iso) return '';
  const segundos = Math.max(0, Math.round((ahora - new Date(iso).getTime()) / 1000));
  if (segundos < 5) return 'recien';
  return `hace ${duracion(segundos)}`;
}

// Un dato mas viejo que esto se muestra atenuado: sigue siendo el ultimo que
// se conoce, pero ya no describe lo que esta pasando.
export const VIEJO_MS = 90_000;

export function esViejo(iso: string | undefined, ahora: number): boolean {
  return iso !== undefined && ahora - new Date(iso).getTime() > VIEJO_MS;
}

export function horaCorta(iso: string): string {
  return new Date(iso).toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

const NOMBRE_ESTADO: Record<string, string> = {
  normal: 'Normal', warning: 'Advertencia', critical: 'Critico',
};

export function nombreSalud(health: string | undefined): string {
  return health ? NOMBRE_ESTADO[health] ?? health : '--';
}

const NOMBRE_TIPO: Record<string, string> = {
  critical: 'critica', streak: 'racha', frequency: 'frecuencia',
};

export function nombreTipoAlerta(type: string): string {
  return NOMBRE_TIPO[type] ?? type;
}

const NOMBRE_MOTIVO: Record<string, string> = {
  completed: 'Completado', critical: 'Cortado por alerta critica', stopped: 'Detenido',
};

export function nombreMotivo(reason: string): string {
  return NOMBRE_MOTIVO[reason] ?? reason;
}
