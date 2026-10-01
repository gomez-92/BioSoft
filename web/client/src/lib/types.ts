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
}

export interface ResultItem {
  ts: string; runId?: string; reason: string; description?: string;
  progressPercent?: number; elapsedSeconds?: number; meanMagneticField?: number;
  source?: string; type?: string; count?: number; limit?: number; emergency?: boolean;
}

export interface Snapshot {
  deviceId: string;
  run: RunInfo | null;
  measures: Measures | null;
  coils: Coils | null;
  status: Status | null;
  alerts: AlertItem[];
  link: { broker: boolean; lastMessageAt: Record<string, string> };
}
