import { config } from '../config.js';

// Traduccion de los payloads de la placa a documentos. Todo lo que este
// archivo decide viene del contrato real (esp32/src/topics.hpp y
// MySystem::_publish*()), no de lo que seria comodo recibir:
//
//  - Ningun payload trae timestamp. Lo pone el ingestor al recibir.
//  - Un grupo puede llegar INCOMPLETO de forma legitima: la tarjeta SD puede
//    deshabilitar campos uno por uno (`if (Topics::MagneticField.enabled)`) y
//    `coils` solo trae las bobinas que existen. Asi que se acepta lo parcial y
//    solo se descarta lo que no tiene NADA aprovechable.
//  - Los tiempos vienen como texto "hh:mm:ss", no como numero.

export interface ParsedMeasures { magneticField?: number; temperature?: number }
export interface ParsedCoil { n: number; current?: number; duty?: number }
export interface ParsedStatus {
  health?: string; progress?: number; elapsedSeconds?: number; elapsedText?: string;
  remainingSeconds?: number; state?: string; megaOk?: boolean;
}
export interface ParsedTargets {
  mode?: string; cem?: number; freq?: number; dur?: number; tol?: number;
  tnmin?: number; tnmax?: number; tcmin?: number; tcmax?: number;
}
export interface ParsedAlert { source: string; type: string; count: number; limit: number }
export interface ParsedResult {
  reason: string; description?: string; progressPercent?: number;
  elapsedSeconds?: number; elapsedText?: string; meanMagneticField?: number;
  source?: string; type?: string; count?: number; limit?: number; emergency?: boolean;
}

function asObject(payload: unknown): Record<string, unknown> | null {
  return typeof payload === 'object' && payload !== null && !Array.isArray(payload)
    ? (payload as Record<string, unknown>)
    : null;
}

// Solo numeros de verdad. Un "1.5" como texto seria un cambio de contrato, no
// un valor: mejor que falte y quede en el log a que entre a la base convertido
// por las nuestras.
function num(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}

function str(value: unknown): string | undefined {
  return typeof value === 'string' && value !== '' ? value : undefined;
}

/**
 * "hh:mm:ss" -> segundos. La placa manda el tiempo transcurrido como texto
 * (SystemData::elapsedTime()), asi que para graficarlo o compararlo hay que
 * convertirlo. Las horas NO estan acotadas a 24: un experimento largo manda
 * "36:10:00" y eso es valido.
 */
export function parseElapsed(value: unknown): number | undefined {
  const text = str(value);
  if (!text) return undefined;
  const match = /^(\d+):([0-5]\d):([0-5]\d)$/.exec(text);
  if (!match) return undefined;
  return Number(match[1]) * 3600 + Number(match[2]) * 60 + Number(match[3]);
}

// Un payload con claves que no esperamos casi siempre significa que alguien
// renombro un campo desde la tarjeta SD. No hay error visible en ningun lado,
// asi que el payload entero en el log es la unica pista que queda.
function warnUnknownKeys(group: string, payload: Record<string, unknown>, known: string[]): void {
  const unknown = Object.keys(payload).filter((key) => !known.includes(key));
  if (unknown.length === 0) return;
  console.warn(
    `[telemetria] ${group}: claves desconocidas [${unknown.join(', ')}] -- ` +
    `revisar si se renombraron desde la SD. Payload: ${JSON.stringify(payload)}`,
  );
}

export function parseMeasures(payload: unknown): ParsedMeasures | null {
  const data = asObject(payload);
  if (!data) return null;
  warnUnknownKeys('measures', data, [config.fields.magneticField, config.fields.temperature]);

  const parsed: ParsedMeasures = {
    magneticField: num(data[config.fields.magneticField]),
    temperature: num(data[config.fields.temperature]),
  };
  // Los dos campos son deshabilitables por separado desde la SD, pero un
  // mensaje sin ninguno de los dos no tiene nada que guardar.
  if (parsed.magneticField === undefined && parsed.temperature === undefined) return null;
  return parsed;
}

/**
 * `coils` trae solo las bobinas que EXISTEN del otro lado ("c1".."d4"), y el
 * conjunto de claves presentes es en si mismo informacion: dice cuantas
 * bobinas hay montadas. Por eso se guarda un arreglo de las que reportaron y
 * no cuatro posiciones fijas, que serian indistinguibles de cuatro bobinas
 * reales midiendo cero.
 */
export function parseCoils(payload: unknown): ParsedCoil[] | null {
  const data = asObject(payload);
  if (!data) return null;
  const known: string[] = [];
  for (let n = 1; n <= 4; n++) known.push(`c${n}`, `d${n}`);
  warnUnknownKeys('coils', data, known);

  const coils: ParsedCoil[] = [];
  for (let n = 1; n <= 4; n++) {
    const current = num(data[`c${n}`]);
    const duty = num(data[`d${n}`]);
    if (current === undefined && duty === undefined) continue;
    coils.push({ n, current, duty });
  }
  return coils.length > 0 ? coils : null;
}

export function parseStatus(payload: unknown): ParsedStatus | null {
  const data = asObject(payload);
  if (!data) return null;
  const { health, progress, elapsed } = config.fields;
  warnUnknownKeys('status', data, [health, progress, elapsed, 'REMAINING', 'STATE', 'MEGA']);

  const parsed: ParsedStatus = {
    health: str(data[health]),
    progress: num(data[progress]),
    elapsedSeconds: parseElapsed(data[elapsed]),
    elapsedText: str(data[elapsed]),
    remainingSeconds: num(data.REMAINING),
    state: str(data.STATE),
    megaOk: typeof data.MEGA === 'boolean' ? data.MEGA : undefined,
  };
  return Object.values(parsed).some((value) => value !== undefined) ? parsed : null;
}

export function parseTargets(payload: unknown): ParsedTargets | null {
  const data = asObject(payload);
  if (!data) return null;
  warnUnknownKeys('targets', data,
    ['MODE', 'CEM', 'FREQ', 'DUR', 'TOL', 'TNMIN', 'TNMAX', 'TCMIN', 'TCMAX']);

  const parsed: ParsedTargets = {
    mode: str(data.MODE), cem: num(data.CEM), freq: num(data.FREQ),
    dur: num(data.DUR), tol: num(data.TOL),
    tnmin: num(data.TNMIN), tnmax: num(data.TNMAX),
    tcmin: num(data.TCMIN), tcmax: num(data.TCMAX),
  };
  return Object.values(parsed).some((value) => value !== undefined) ? parsed : null;
}

/**
 * A diferencia del resto, una alerta se descarta ENTERA si le falta algo: un
 * "TEMP1 critical" sin count ni limit no dice si el experimento esta por
 * cortarse o acaba de empezar a quejarse, y media alerta en la base es peor
 * que ninguna. Mismo criterio que el Detector del Mega con sus reglas.
 */
export function parseAlert(payload: unknown): ParsedAlert | null {
  const data = asObject(payload);
  if (!data) return null;
  warnUnknownKeys('alerts', data, ['SRC', 'TYPE', 'COUNT', 'LIMIT']);

  const source = str(data.SRC);
  const type = str(data.TYPE);
  const count = num(data.COUNT);
  const limit = num(data.LIMIT);
  if (!source || !type || count === undefined || limit === undefined) {
    console.warn(`[telemetria] alerta incompleta, descartada: ${JSON.stringify(data)}`);
    return null;
  }
  return { source, type, count, limit };
}

/**
 * El resultado es el unico mensaje que el monitor no puede deducir de ningun
 * otro: sin el, una corrida cortada por temperatura critica se ve desde afuera
 * igual que una que termino bien. `REASON` es lo unico imprescindible; los
 * campos crudos del corte (SRC/TYPE/COUNT/LIMIT) solo vienen cuando aplican.
 */
export function parseResult(payload: unknown): ParsedResult | null {
  const data = asObject(payload);
  if (!data) return null;
  warnUnknownKeys('result', data,
    ['REASON', 'DESC', 'PROGRESS', 'ELAPSED', 'MEAN', 'SRC', 'TYPE', 'COUNT', 'LIMIT', 'emerg']);

  const reason = str(data.REASON);
  if (!reason) {
    console.warn(`[telemetria] result sin REASON, descartado: ${JSON.stringify(data)}`);
    return null;
  }
  return {
    reason,
    description: str(data.DESC),
    progressPercent: num(data.PROGRESS),
    elapsedSeconds: parseElapsed(data.ELAPSED),
    elapsedText: str(data.ELAPSED),
    meanMagneticField: num(data.MEAN),
    source: str(data.SRC),
    type: str(data.TYPE),
    count: num(data.COUNT),
    limit: num(data.LIMIT),
    emergency: typeof data.emerg === 'boolean' ? data.emerg : undefined,
  };
}
