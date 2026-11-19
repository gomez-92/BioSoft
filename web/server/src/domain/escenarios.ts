import { Scenario, Setting } from '../models/index.js';
import { ESCENARIOS_FABRICA } from './escenariosfabrica.js';

// Biblioteca de escenarios de prueba (sensores simulados). Un escenario es
// la senal de cada fuente simulada (scenario.hpp en el Mega) mas la corrida de
// referencia contra la que se diseno; el generador embebido los ofrece como
// "pruebas" y, elegido uno, lo vuelca al formulario de configuracion.
//
// Los de FABRICA son los de tools/escenarios.json (copiado en
// escenariosfabrica.ts por `npm run sync:generador`). Ahi estan comprobados:
// el Mega (test_scenario_runs) y el generador (test-generador.js) corren cada
// uno y exigen el final que promete. Aca son solo el punto de partida: se
// siembran UNA vez, y despues se editan o borran como cualquier otro.
// "Restaurar los de fabrica" los vuelve a esos valores (y recrea los
// borrados) sin tocar los escenarios propios.
//
// Los limites son los del Mega (Scenario::applyPoints, handler de
// config_scenario): un escenario que el Mega rechazaria no se guarda, porque
// despues se elegiria, se mandaria y la placa correria con otra cosa.

export const FUENTES = ['CEM1', 'TEMP1'] as const;
export type Fuente = typeof FUENTES[number];
export const MAX_PUNTOS = 8;
export const MAX_NIVEL = 10;
export const MAX_SEGUNDO = 65535;

export class EscenarioInvalido extends Error {}

export interface Senal {
  points: Array<[number, number]>;
  noise: number; oscAmp: number; oscPeriod: number; dropAt: number; setpointDuty?: number;
}
export interface FuenteEscenario { enabled: boolean; scenario: Senal }
export interface Referencia {
  intensity: number; frequency: number; tolerance: number;
  tempNormal: { min: number; max: number }; tempCritical: { min: number; max: number };
  durationS: number; mode: 'x' | 'null';
}
export interface EscenarioEntrada {
  name: string; description: string;
  sources: Partial<Record<Fuente, FuenteEscenario>>;
  reference: Referencia | null;
}

const esNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);

function leerSenal(fuente: Fuente, valor: unknown): Senal {
  const s = valor as Record<string, unknown> | null;
  if (!s || typeof s !== 'object') throw new EscenarioInvalido(`${fuente}: falta la senal`);
  const pts = s.points;
  if (!Array.isArray(pts) || pts.length > MAX_PUNTOS) {
    throw new EscenarioInvalido(`${fuente}: los puntos tienen que ser una lista de hasta ${MAX_PUNTOS}`);
  }
  const points: Array<[number, number]> = [];
  pts.forEach((p, i) => {
    if (!Array.isArray(p) || p.length !== 2 || !esNum(p[0]) || !esNum(p[1])) {
      throw new EscenarioInvalido(`${fuente}: el punto ${i + 1} no es [segundo, nivel]`);
    }
    const [t, l] = p as [number, number];
    if (!Number.isInteger(t) || t < 0 || t > MAX_SEGUNDO) {
      throw new EscenarioInvalido(`${fuente}: el segundo ${t} tiene que ser entero, de 0 a ${MAX_SEGUNDO}`);
    }
    if (i > 0 && t < points[i - 1][0]) throw new EscenarioInvalido(`${fuente}: los segundos tienen que ir en orden`);
    if (Math.abs(l) > MAX_NIVEL) throw new EscenarioInvalido(`${fuente}: el nivel ${l} esta fuera de -10..10`);
    points.push([t, Math.round(l * 100) / 100]);
  });
  const num = (k: string, min?: number, max?: number): number => {
    const v = s[k] ?? 0;
    if (!esNum(v) || (min !== undefined && v < min) || (max !== undefined && v > max)) {
      throw new EscenarioInvalido(`${fuente}: ${k} fuera de rango`);
    }
    return v;
  };
  const senal: Senal = {
    points, noise: num('noise', 0), oscAmp: num('oscAmp'), oscPeriod: num('oscPeriod', 0), dropAt: num('dropAt', 0),
  };
  if (fuente === 'CEM1') senal.setpointDuty = num('setpointDuty', 0, 1);
  return senal;
}

function leerReferencia(valor: unknown): Referencia | null {
  if (valor === null || valor === undefined) return null;
  const r = valor as Record<string, unknown>;
  const rango = (v: unknown) => {
    const o = v as Record<string, unknown> | null;
    return o && esNum(o.min) && esNum(o.max) ? { min: o.min, max: o.max } : null;
  };
  const tn = rango(r.tempNormal), tc = rango(r.tempCritical);
  if (!esNum(r.intensity) || !esNum(r.frequency) || !esNum(r.tolerance) || !esNum(r.durationS) || !tn || !tc
      || (r.mode !== 'x' && r.mode !== 'null')) {
    throw new EscenarioInvalido('la corrida de referencia esta incompleta');
  }
  return { intensity: r.intensity, frequency: r.frequency, tolerance: r.tolerance, tempNormal: tn,
           tempCritical: tc, durationS: r.durationS, mode: r.mode };
}

/** Valida lo que manda el cliente; tira EscenarioInvalido con el motivo. */
export function leerEscenario(cuerpo: unknown): EscenarioEntrada {
  const b = (cuerpo ?? {}) as Record<string, unknown>;
  const name = typeof b.name === 'string' ? b.name.trim() : '';
  if (name.length < 1 || name.length > 60) throw new EscenarioInvalido('el nombre tiene que tener de 1 a 60 caracteres');
  const description = typeof b.description === 'string' ? b.description.trim() : '';
  if (description.length > 400) throw new EscenarioInvalido('la descripcion no puede pasar de 400 caracteres');
  const fuentes = (b.sources ?? {}) as Record<string, unknown>;
  const sources: Partial<Record<Fuente, FuenteEscenario>> = {};
  for (const f of FUENTES) {
    const v = fuentes[f] as Record<string, unknown> | undefined;
    if (v === undefined) continue;
    if (typeof v?.enabled !== 'boolean') throw new EscenarioInvalido(`${f}: falta si esta vigilada (enabled)`);
    sources[f] = { enabled: v.enabled, scenario: leerSenal(f, v.scenario) };
  }
  if (Object.keys(fuentes).some((k) => !FUENTES.includes(k as Fuente))) {
    throw new EscenarioInvalido('las unicas fuentes son CEM1 y TEMP1');
  }
  if (Object.keys(sources).length === 0) throw new EscenarioInvalido('el escenario no simula ninguna fuente');
  return { name, description, sources, reference: leerReferencia(b.reference) };
}

/** Lo que se le manda al cliente. */
export function fila(e: {
  _id: unknown; name: string; description?: string | null; sources: unknown; reference?: unknown;
  factoryKey?: string | null; expect?: unknown; minDuration?: number | null;
  updatedAt?: Date | null; updatedBy?: string | null;
}) {
  const fabrica = e.factoryKey ? ESCENARIOS_FABRICA.profiles.find((p) => p.key === e.factoryKey) : undefined;
  return {
    id: String(e._id),
    name: e.name,
    description: e.description ?? '',
    sources: e.sources,
    reference: e.reference ?? null,
    factoryKey: e.factoryKey ?? null,
    // La promesa ("corta por critico a los ~75 s") solo vale para un
    // escenario de fabrica SIN tocar: editado, ya no es el comprobado.
    expect: fabrica && igualAFabrica(e, fabrica) ? e.expect ?? null : null,
    minDuration: fabrica && igualAFabrica(e, fabrica) ? e.minDuration ?? null : null,
    modificado: fabrica ? !igualAFabrica(e, fabrica) : false,
    updatedAt: e.updatedAt ?? null,
    updatedBy: e.updatedBy ?? null,
  };
}

// Solo la senal: el nombre no cambia lo que hace el Mega (y al restaurar
// puede volver con "(fabrica)").
function igualAFabrica(e: { sources: unknown }, f: { sources: unknown }): boolean {
  return JSON.stringify(e.sources) === JSON.stringify(f.sources);
}

function docDeFabrica(p: typeof ESCENARIOS_FABRICA.profiles[number]) {
  return {
    name: p.name, description: p.description, sources: p.sources, reference: null,
    factoryKey: p.key, expect: p.expect, minDuration: p.minDuration, updatedBy: 'fabrica',
  };
}

const SEMBRADO = 'escenariosSembrados';

/**
 * Siembra cada escenario de fabrica UNA sola vez, en el arranque. Se anota
 * que claves ya se sembraron (Setting `escenariosSembrados` = {keys}), asi:
 *  - uno borrado por el admin no vuelve solo (borrarlo es una decision, no un
 *    estado a corregir; para eso esta "Restaurar");
 *  - uno de fabrica NUEVO (agregado a tools/escenarios.json en una
 *    actualizacion) aparece en el proximo arranque sin tocar nada mas.
 * Si un escenario propio ya usa el nombre, el de fabrica entra con
 * "(fabrica)" al final, igual que al restaurar.
 */
export async function sembrarEscenarios(): Promise<void> {
  // Los indices (nombre unico) antes de escribir: Mongoose los arma en
  // segundo plano, y un alta que gana esa carrera duplica un nombre.
  await Promise.all([Scenario.init(), Setting.init()]);
  const marca = await Setting.findOne({ key: SEMBRADO }).lean();
  const sembrados = new Set<string>(Array.isArray(marca?.value?.keys) ? marca.value.keys : []);
  const nuevos = ESCENARIOS_FABRICA.profiles.filter((p) => !sembrados.has(p.key));
  if (nuevos.length === 0) return;
  for (const p of nuevos) {
    if (await Scenario.exists({ factoryKey: p.key })) continue;
    const ocupado = await Scenario.exists({ name: p.name });
    await Scenario.create({ ...docDeFabrica(p), ...(ocupado ? { name: `${p.name} (fabrica)` } : {}) });
  }
  nuevos.forEach((p) => sembrados.add(p.key));
  await Setting.updateOne({ key: SEMBRADO }, { $set: { value: { keys: [...sembrados] } } }, { upsert: true });
  console.log(`[escenarios] sembrados de fabrica: ${nuevos.map((p) => p.key).join(', ')}`);
}

/**
 * Vuelve los de fabrica a sus valores y recrea los borrados. Los propios no
 * se tocan; si uno propio ocupa el nombre de uno de fabrica, el de fabrica
 * vuelve con "(fabrica)" al final (los nombres son unicos).
 */
export async function restaurarFabrica(usuario: string): Promise<void> {
  for (const p of ESCENARIOS_FABRICA.profiles) {
    const actual = await Scenario.findOne({ factoryKey: p.key }).select('_id').lean();
    const ocupado = await Scenario.findOne({ name: p.name, factoryKey: { $ne: p.key } }).select('_id').lean();
    const doc = { ...docDeFabrica(p), updatedBy: usuario, ...(ocupado ? { name: `${p.name} (fabrica)` } : {}) };
    if (actual) await Scenario.updateOne({ _id: actual._id }, { $set: doc });
    else await Scenario.create(doc);
  }
}
