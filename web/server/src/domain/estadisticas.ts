import { Alert, BoardEvent, Run } from '../models/index.js';

// Estadisticas de la pagina Inicio (GET /api/stats).
//
// "Experimentos" son las corridas normal + unknown, igual que la pestaña
// Experimentos del historial: una corrida sin marca puede haber tenido
// animales, asi que no se la descarta. Las pruebas se cuentan aparte y nunca
// entran en horas, desenlaces ni alertas: son datos de banco.
//
// La agregacion vive en funciones puras (resumirCorridas) para poder
// testearla sin Mongo; las consultas son lo minimo para alimentarlas.

export const VENTANA_RECIENTE_DIAS = 30;
export const VENTANA_EQUIPO_DIAS = 7;

/** Los eventos de la bitacora que dicen algo de la salud del equipo. */
export const CODIGOS_EQUIPO = [
  'arranque', 'mega_reinicio', 'enlace_caido', 'broker_caido', 'corrida_perdida',
  'mega_margen_bajo', 'cola_llena', 'config_ignorada',
] as const;

export interface CorridaParaStats {
  _id: unknown;
  startedAt: Date;
  endedAt?: Date | null;
  state: string;
  runType?: string | null;
  targets?: { mode?: string | null } | null;
  result?: { reason?: string | null; elapsedSeconds?: number | null } | null;
}

export interface Desenlaces {
  completed: number;
  stopped: number;
  critical: number;
  lost: number;
  /** Cerradas con un motivo que no es ninguno de los anteriores (unknown...). */
  otro: number;
  /** Huerfanas: nunca llego su result. */
  sinCierre: number;
}

export interface ResumenCorridas {
  experimentos: {
    total: number;
    recientes: number;
    tratado: number;
    control: number;
    /** Sin marca de tipo (firmware viejo): cuentan como experimento. */
    sinMarca: number;
    horasExposicion: number;
    desenlaces: Desenlaces;
    ultimoInicio: string | null;
  };
  pruebas: { total: number; recientes: number };
  /** Ids de los experimentos, para contar sus alertas. */
  idsExperimentos: unknown[];
}

/** Mismo criterio que client/src/lib/format.ts (esCampoNulo). */
export function esModoNulo(mode: string | null | undefined): boolean {
  if (!mode) return false;
  const valor = mode.toLowerCase();
  return valor === 'null' || valor.includes('nulo');
}

/**
 * Segundos de exposicion de una corrida cerrada: lo que informo la placa en
 * el result; sin eso (huerfana), del inicio a su ultimo dato (endedAt). Una
 * corrida en curso no suma: todavia no se sabe cuanto va a durar.
 */
export function segundosDeExposicion(run: CorridaParaStats): number {
  if (run.state === 'running') return 0;
  const informados = run.result?.elapsedSeconds;
  if (typeof informados === 'number' && Number.isFinite(informados) && informados >= 0) return informados;
  if (!run.endedAt) return 0;
  const ms = new Date(run.endedAt).getTime() - new Date(run.startedAt).getTime();
  return ms > 0 ? ms / 1000 : 0;
}

export function resumirCorridas(runs: CorridaParaStats[], ahora: number): ResumenCorridas {
  const desde = ahora - VENTANA_RECIENTE_DIAS * 24 * 3600 * 1000;
  const desenlaces: Desenlaces = { completed: 0, stopped: 0, critical: 0, lost: 0, otro: 0, sinCierre: 0 };
  const resumen: ResumenCorridas = {
    experimentos: {
      total: 0, recientes: 0, tratado: 0, control: 0, sinMarca: 0, horasExposicion: 0,
      desenlaces, ultimoInicio: null,
    },
    pruebas: { total: 0, recientes: 0 },
    idsExperimentos: [],
  };
  let segundos = 0;
  let ultimo = 0;

  for (const run of runs) {
    const inicio = new Date(run.startedAt).getTime();
    const reciente = inicio >= desde;
    if (run.runType === 'test') {
      resumen.pruebas.total += 1;
      if (reciente) resumen.pruebas.recientes += 1;
      continue;
    }
    const e = resumen.experimentos;
    e.total += 1;
    if (reciente) e.recientes += 1;
    if (run.runType !== 'normal') e.sinMarca += 1;
    if (esModoNulo(run.targets?.mode)) e.control += 1; else e.tratado += 1;
    if (inicio > ultimo) ultimo = inicio;
    resumen.idsExperimentos.push(run._id);
    segundos += segundosDeExposicion(run);

    if (run.state === 'running') continue;
    const motivo = run.result?.reason;
    if (!motivo) desenlaces.sinCierre += 1;
    else if (motivo === 'completed' || motivo === 'stopped' || motivo === 'critical' || motivo === 'lost') {
      desenlaces[motivo] += 1;
    } else desenlaces.otro += 1;
  }

  resumen.experimentos.horasExposicion = Math.round((segundos / 3600) * 10) / 10;
  resumen.experimentos.ultimoInicio = ultimo > 0 ? new Date(ultimo).toISOString() : null;
  return resumen;
}

export interface AlertasFrecuentes { source: string; type: string; cantidad: number }

export interface Estadisticas {
  generadoEn: string;
  ventanaDias: number;
  experimentos: ResumenCorridas['experimentos'];
  pruebas: ResumenCorridas['pruebas'];
  alertas: AlertasFrecuentes[];
  /** Solo para administradores (la bitacora es de ellos). */
  equipo?: {
    dias: number;
    eventos: Record<string, number>;
    ultimoArranque: string | null;
  };
}

export async function calcularEstadisticas(incluirEquipo: boolean, ahora = Date.now()): Promise<Estadisticas> {
  const runs = await Run.find({}, {
    startedAt: 1, endedAt: 1, state: 1, runType: 1, 'targets.mode': 1,
    'result.reason': 1, 'result.elapsedSeconds': 1,
  }).lean<CorridaParaStats[]>();
  const resumen = resumirCorridas(runs, ahora);

  const alertas = resumen.idsExperimentos.length === 0 ? [] : await Alert.aggregate<AlertasFrecuentes>([
    { $match: { runId: { $in: resumen.idsExperimentos } } },
    { $group: { _id: { source: '$source', type: '$type' }, cantidad: { $sum: 1 } } },
    { $sort: { cantidad: -1 } },
    { $limit: 6 },
    { $project: { _id: 0, source: '$_id.source', type: '$_id.type', cantidad: 1 } },
  ]);

  const estadisticas: Estadisticas = {
    generadoEn: new Date(ahora).toISOString(),
    ventanaDias: VENTANA_RECIENTE_DIAS,
    experimentos: resumen.experimentos,
    pruebas: resumen.pruebas,
    alertas,
  };

  if (incluirEquipo) {
    const desde = new Date(ahora - VENTANA_EQUIPO_DIAS * 24 * 3600 * 1000);
    const [porCodigo, arranque] = await Promise.all([
      BoardEvent.aggregate<{ _id: string; n: number }>([
        { $match: { ts: { $gte: desde }, code: { $in: [...CODIGOS_EQUIPO] } } },
        { $group: { _id: '$code', n: { $sum: 1 } } },
      ]),
      BoardEvent.findOne({ code: 'arranque' }).sort({ ts: -1 }).select('ts').lean(),
    ]);
    const eventos: Record<string, number> = Object.fromEntries(CODIGOS_EQUIPO.map((c) => [c, 0]));
    for (const fila of porCodigo) eventos[fila._id] = fila.n;
    estadisticas.equipo = {
      dias: VENTANA_EQUIPO_DIAS,
      eventos,
      ultimoArranque: arranque?.ts ? new Date(arranque.ts).toISOString() : null,
    };
  }
  return estadisticas;
}
