// Bitacora de la placa (esp32/src/eventlog.hpp, server/src/domain/bitacora.ts).
//
// CODIGOS es el espejo del catalogo cerrado de la placa (EventCodes): mismo
// riesgo de deriva que el orden de bits de RLX. Un codigo que la placa manda
// y que no esta aca se muestra igual, con el codigo crudo como nombre.

export type NivelEvento = 'E' | 'W' | 'I';

export interface EventoPlaca {
  id: string;
  ts: string;
  level: NivelEvento;
  code: string;
  message: string;
  /** Numero de evento desde el arranque de la placa (vuelve a 1 al reiniciar). */
  seq: number | null;
  uptimeSeconds: number | null;
  /** Eventos que la placa piso en su buffer (lleno) justo antes de este. */
  dropped: number;
  /**
   * La corrida en curso a la hora del evento, o null si paso fuera de
   * corridas. La calcula el servidor por fecha (la placa no manda id de
   * corrida), ver server/src/domain/bitacora.ts.
   */
  run: { id: string; number: number | null; startedAt: string; runType: string; state: string } | null;
}

export const CODIGOS: Record<string, string> = {
  arranque: 'Arranque de la placa',
  config_ignorada: 'Configuración de la SD ignorada',
  config_remota: 'Configuración remota',
  enlace_caido: 'Enlace con el Mega caído',
  enlace_ok: 'Enlace con el Mega',
  mega_reinicio: 'El Mega se reinició',
  mega_margen_bajo: 'Memoria del Mega al límite',
  corrida_perdida: 'Corrida perdida',
  start_rechazado: 'Inicio rechazado',
  broker_caido: 'Broker caído',
  broker_ok: 'Broker de vuelta',
  cola_llena: 'Telemetría descartada',
  corrida_inicio: 'Corrida iniciada',
  corrida_fin: 'Corrida terminada',
};

export function nombreCodigo(code: string): string {
  return CODIGOS[code] ?? code;
}

export const NIVELES: Record<NivelEvento, { nombre: string; clase: string }> = {
  E: { nombre: 'Error', clase: 'chip-critical' },
  W: { nombre: 'Aviso', clase: 'chip-warning' },
  I: { nombre: 'Info', clase: '' },
};

/** Filtros de nivel que ofrece la pagina (valor = lo que va en ?level=). */
export const FILTROS_NIVEL: Array<{ valor: string; nombre: string }> = [
  { valor: '', nombre: 'Todos los niveles' },
  { valor: 'E,W', nombre: 'Errores y avisos' },
  { valor: 'E', nombre: 'Solo errores' },
];

/** Filtro por corridas (valor = lo que va en ?scope=). */
export const FILTROS_ALCANCE: Array<{ valor: string; nombre: string }> = [
  { valor: '', nombre: 'Dentro y fuera de corridas' },
  { valor: 'in', nombre: 'Solo durante corridas' },
  { valor: 'out', nombre: 'Solo fuera de corridas' },
];

export interface FiltrosEventos {
  niveles: string;
  codigo: string;
  /** '' | 'in' | 'out' */
  alcance: string;
  /** id de una corrida: solo sus eventos (pisa a `alcance`). */
  corrida: string;
}

/** Si un evento pasa los filtros (para sumar los que llegan en vivo). */
export function pasaFiltro(evento: EventoPlaca, f: FiltrosEventos): boolean {
  if (f.niveles && !f.niveles.split(',').includes(evento.level)) return false;
  if (f.codigo && evento.code !== f.codigo) return false;
  if (f.corrida) return evento.run?.id === f.corrida;
  if (f.alcance === 'in' && !evento.run) return false;
  if (f.alcance === 'out' && evento.run) return false;
  return true;
}

/** "#42 · 22/11 12:30 · prueba" -- como se nombra una corrida en la bitacora. */
export function nombreCorrida(run: { number?: number | null; startedAt: string; runType: string }): string {
  const inicio = new Date(run.startedAt);
  const fecha = inicio.toLocaleDateString('es-AR', { day: '2-digit', month: '2-digit' });
  const hora = inicio.toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit' });
  const tipo = run.runType === 'test' ? 'prueba' : run.runType === 'normal' ? 'experimento' : 'sin marca';
  const numero = typeof run.number === 'number' && run.number > 0 ? `#${run.number} · ` : '';
  return `${numero}${fecha} ${hora} · ${tipo}`;
}
