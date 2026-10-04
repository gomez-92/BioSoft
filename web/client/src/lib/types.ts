// Formas que manda el backend. No son las de la placa: el backend ya tradujo
// las claves cortas del contrato MQTT (CEM1, c1..d4, REASON...) a nombres
// legibles y convirtio los tiempos "hh:mm:ss" a segundos.

export interface Targets {
  mode?: string; cem?: number; freq?: number; dur?: number; tol?: number;
  tnmin?: number; tnmax?: number; tcmin?: number; tcmax?: number;
  test?: boolean;
  configId?: string; relaxations?: number;
}

/** "normal" | "test" | "unknown" (sin marca TEST: firmware viejo o simulador viejo). */
export type RunType = 'normal' | 'test' | 'unknown';

export interface RunInfo {
  id: string;
  startedAt: string;
  state: string;
  runType: RunType;
  targets: Targets | null;
}

export interface Measures { ts: string; magneticField?: number; temperature?: number }

export interface Coil { n: number; current?: number; duty?: number }
export interface Coils { ts: string; coils: Coil[] }

export interface Status {
  ts: string; health?: string; progress?: number; elapsedSeconds?: number;
  elapsedText?: string; remainingSeconds?: number; state?: string; megaOk?: boolean;
}

export interface AlertItem {
  ts: string; source: string; type: string; count: number; limit: number;
  /** Ultima medicion de esa fuente antes de la alerta (server/src/domain/alertas.ts). */
  value?: number; valueAt?: string;
}

export interface ResultItem {
  ts: string; runId?: string; reason: string; description?: string;
  progressPercent?: number; elapsedSeconds?: number; meanMagneticField?: number;
  source?: string; type?: string; count?: number; limit?: number; emergency?: boolean;
  /** Tipo de la corrida que cerro (lo completa el cliente; la placa no lo manda en result). */
  runType?: RunType;
}

export interface Snapshot {
  deviceId: string;
  run: RunInfo | null;
  measures: Measures | null;
  coils: Coils | null;
  status: Status | null;
  alerts: AlertItem[];
  /** La ultima corrida terminada (solo cuando no hay una en curso). */
  lastRun: {
    id: string; startedAt: string; endedAt: string | null; state: string; runType: RunType;
    result: Omit<ResultItem, 'ts'> | null;
  } | null;
  link: { broker: boolean; lastMessageAt: Record<string, string> };
}

export type Rol = 'admin' | 'viewer';
export interface Perfil { username: string; role: Rol; email: string | null }

/** Ping a la placa (server/src/domain/boardping.ts). */
export interface PlacaStatus {
  estado: 'desconocido' | 'conectada' | 'sin_respuesta';
  ultimoPing: string | null;
  ultimaRespuesta: string | null;
  latenciaMs: number | null;
  appState: string | null;
  configId: string | null;
  configStatus: string | null;
  mega: boolean | null;
  uptimeSeconds: number | null;
}
