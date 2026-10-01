import {
  Area, CartesianGrid, ComposedChart, Legend, Line, ReferenceArea, ReferenceLine,
  ResponsiveContainer, Tooltip, XAxis, YAxis,
} from 'recharts';
import { esAlertaCritica, numero } from '../lib/format.js';
import { aMilisegundos, bucketEnMs, insertarHuecos } from '../lib/series.js';
import type { Targets } from '../lib/types.js';

// Graficos de la corrida.
//
// Reglas que valen para los cuatro, y que no son de gusto:
//
//  - UN SOLO EJE por grafico. Campo (mT) y temperatura (°C) no comparten
//    grafico aunque compartan el tiempo: dos escalas en un mismo dibujo hacen
//    que el cruce de dos lineas parezca significar algo cuando no significa
//    nada.
//  - Cada punto agregado muestra su banda MIN-MAX ademas del promedio. El
//    promedio solo esconde el pico que disparo la alerta, que es justamente
//    lo que alguien viene a buscar.
//  - Los huecos se dibujan como huecos (ver insertarHuecos).
//  - Los colores de serie salen de una paleta validada para daltonismo sobre
//    esta superficie oscura, en orden fijo por bobina: el color sigue a la
//    bobina, no a su posicion en la lista, asi que ocultar una no repinta las
//    otras.

const SERIE = ['#3987e5', '#d95926', '#199e70', '#c98500'];   // slots 1-4, validados
const CRITICO = '#d03b3b';
const ADVERTENCIA = '#fab219';
const TINTA_TENUE = '#898781';
const GRILLA = '#2c2c2a';
const SUPERFICIE = '#171a21';

const ALTO = 240;

export interface PuntoMedicion {
  ts: string; field?: number; fieldMin?: number; fieldMax?: number;
  temp?: number; tempMin?: number; tempMax?: number; n?: number;
}

export interface AlertaMarca { ts: string; type: string; source: string }

function ejeTiempo(inicio: number, fin: number) {
  return {
    dataKey: 't' as const,
    type: 'number' as const,
    domain: [inicio, fin] as [number, number],
    scale: 'time' as const,
    tickFormatter: (valor: number) =>
      new Date(valor).toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit' }),
    stroke: TINTA_TENUE,
    fontSize: 11,
  };
}

function Tip({ active, payload, label, unidad, decimales }: {
  active?: boolean; payload?: Array<{ name?: string; value?: number; color?: string }>;
  label?: number; unidad: string; decimales: number;
}) {
  if (!active || !payload?.length) return null;
  return (
    <div className="tooltip">
      <p className="tooltip-hora">
        {new Date(label ?? 0).toLocaleTimeString('es-AR', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}
      </p>
      {payload.filter((fila) => fila.value !== undefined && fila.value !== null).map((fila) => (
        <p key={fila.name} className="tooltip-fila">
          <span className="tooltip-marca" style={{ background: fila.color }} />
          {fila.name}: <strong>{numero(fila.value, decimales)} {unidad}</strong>
        </p>
      ))}
    </div>
  );
}

function marcasDeAlerta(alertas: AlertaMarca[]) {
  return alertas.map((alerta, index) => (
    <ReferenceLine
      key={`${alerta.ts}-${index}`}
      x={new Date(alerta.ts).getTime()}
      stroke={esAlertaCritica(alerta.type) ? CRITICO : ADVERTENCIA}
      strokeDasharray="3 3"
      strokeWidth={1}
    />
  ));
}

/** Campo magnetico, con la banda de tolerancia alrededor del objetivo. */
export function GraficoCampo({ puntos, bucket, targets, alertas, inicio, fin }: {
  puntos: PuntoMedicion[]; bucket?: string; targets: Targets | null;
  alertas: AlertaMarca[]; inicio: number; fin: number;
}) {
  const datos = aMilisegundos(insertarHuecos(puntos, bucketEnMs(bucket)) as PuntoMedicion[]);
  const objetivo = targets?.cem;
  const tolerancia = targets?.tol ?? 0;
  const banda = objetivo !== undefined && tolerancia > 0
    ? [objetivo * (1 - tolerancia / 100), objetivo * (1 + tolerancia / 100)] as const
    : null;

  return (
    <figure className="figura">
      <figcaption>
        Campo magnetico <span className="unidad">mT</span>
        {banda && <span className="nota"> · banda: tolerancia ±{numero(tolerancia, 0)}%</span>}
      </figcaption>
      <ResponsiveContainer width="100%" height={ALTO}>
        <ComposedChart data={datos} margin={{ top: 8, right: 12, bottom: 4, left: -8 }}>
          <CartesianGrid stroke={GRILLA} vertical={false} />
          <XAxis {...ejeTiempo(inicio, fin)} />
          <YAxis stroke={TINTA_TENUE} fontSize={11} width={52}
            tickFormatter={(v: number) => numero(v, 2)} />
          {banda && (
            <ReferenceArea y1={banda[0]} y2={banda[1]} fill="#3987e5" fillOpacity={0.08}
              stroke="none" ifOverflow="extendDomain" />
          )}
          {objetivo !== undefined && (
            <ReferenceLine y={objetivo} stroke="#3987e5" strokeDasharray="4 4" strokeWidth={1}
              label={{ value: `objetivo ${numero(objetivo, 2)}`, fill: TINTA_TENUE, fontSize: 10, position: 'insideTopRight' }} />
          )}
          {marcasDeAlerta(alertas)}
          {/* La banda min-max del bucket: sin esto el promedio esconde los picos. */}
          <Area type="monotone" dataKey="fieldMax" stroke="none" fill={SERIE[0]} fillOpacity={0.18}
            connectNulls={false} isAnimationActive={false} name="max" />
          <Area type="monotone" dataKey="fieldMin" stroke="none" fill={SUPERFICIE} fillOpacity={1}
            connectNulls={false} isAnimationActive={false} name="min" />
          <Line type="monotone" dataKey="field" stroke={SERIE[0]} strokeWidth={2} dot={false}
            connectNulls={false} isAnimationActive={false} name="campo" />
          <Tooltip content={<Tip unidad="mT" decimales={3} />} />
        </ComposedChart>
      </ResponsiveContainer>
    </figure>
  );
}

/** Temperatura, con las bandas normal y critica del experimento. */
export function GraficoTemperatura({ puntos, bucket, targets, alertas, inicio, fin }: {
  puntos: PuntoMedicion[]; bucket?: string; targets: Targets | null;
  alertas: AlertaMarca[]; inicio: number; fin: number;
}) {
  const datos = aMilisegundos(insertarHuecos(puntos, bucketEnMs(bucket)) as PuntoMedicion[]);
  const normal = targets?.tnmin !== undefined && targets?.tnmax !== undefined
    ? [targets.tnmin, targets.tnmax] as const : null;

  return (
    <figure className="figura">
      <figcaption>
        Temperatura <span className="unidad">°C</span>
        {normal && <span className="nota"> · banda: rango normal</span>}
      </figcaption>
      <ResponsiveContainer width="100%" height={ALTO}>
        <ComposedChart data={datos} margin={{ top: 8, right: 12, bottom: 4, left: -8 }}>
          <CartesianGrid stroke={GRILLA} vertical={false} />
          <XAxis {...ejeTiempo(inicio, fin)} />
          <YAxis stroke={TINTA_TENUE} fontSize={11} width={52}
            tickFormatter={(v: number) => numero(v, 0)} />
          {normal && (
            <ReferenceArea y1={normal[0]} y2={normal[1]} fill="#199e70" fillOpacity={0.08}
              stroke="none" ifOverflow="extendDomain" />
          )}
          {/* Los limites criticos son los que cortan el experimento: van en
              rojo de estado, no en un color de serie. */}
          {targets?.tcmax !== undefined && (
            <ReferenceLine y={targets.tcmax} stroke={CRITICO} strokeDasharray="4 4" strokeWidth={1}
              label={{ value: 'critico', fill: CRITICO, fontSize: 10, position: 'insideTopRight' }} />
          )}
          {targets?.tcmin !== undefined && (
            <ReferenceLine y={targets.tcmin} stroke={CRITICO} strokeDasharray="4 4" strokeWidth={1} />
          )}
          {marcasDeAlerta(alertas)}
          <Area type="monotone" dataKey="tempMax" stroke="none" fill={SERIE[1]} fillOpacity={0.18}
            connectNulls={false} isAnimationActive={false} name="max" />
          <Area type="monotone" dataKey="tempMin" stroke="none" fill={SUPERFICIE} fillOpacity={1}
            connectNulls={false} isAnimationActive={false} name="min" />
          <Line type="monotone" dataKey="temp" stroke={SERIE[1]} strokeWidth={2} dot={false}
            connectNulls={false} isAnimationActive={false} name="temperatura" />
          <Tooltip content={<Tip unidad="°C" decimales={1} />} />
        </ComposedChart>
      </ResponsiveContainer>
    </figure>
  );
}

/**
 * Corriente y duty por bobina. Son dos graficos y no uno con dos ejes: amperes
 * y porcentaje no comparten escala, y superponerlos haria que el cruce de dos
 * lineas pareciera un evento.
 */
export function GraficoBobinas({ puntos, bucket, bobinas, magnitud, inicio, fin }: {
  puntos: Array<Record<string, unknown> & { ts: string }>; bucket?: string;
  bobinas: number[]; magnitud: 'corriente' | 'duty'; inicio: number; fin: number;
}) {
  if (bobinas.length === 0) return null;
  const datos = aMilisegundos(insertarHuecos(puntos, bucketEnMs(bucket)));
  const prefijo = magnitud === 'corriente' ? 'c' : 'd';
  const unidad = magnitud === 'corriente' ? 'A' : '%';

  return (
    <figure className="figura">
      <figcaption>
        {magnitud === 'corriente' ? 'Corriente por bobina' : 'Duty por bobina'}{' '}
        <span className="unidad">{unidad}</span>
      </figcaption>
      <ResponsiveContainer width="100%" height={ALTO}>
        <ComposedChart data={datos} margin={{ top: 8, right: 12, bottom: 4, left: -8 }}>
          <CartesianGrid stroke={GRILLA} vertical={false} />
          <XAxis {...ejeTiempo(inicio, fin)} />
          <YAxis stroke={TINTA_TENUE} fontSize={11} width={52}
            tickFormatter={(v: number) => numero(v, magnitud === 'corriente' ? 2 : 0)} />
          {/* El color sale del NUMERO de bobina, no del orden en la lista: si
              una bobina no reporto, las demas conservan su color. */}
          {bobinas.map((n) => (
            <Line key={n} type="monotone" dataKey={`${prefijo}${n}`} name={`B${n}`}
              stroke={SERIE[(n - 1) % SERIE.length]} strokeWidth={2} dot={false}
              connectNulls={false} isAnimationActive={false} />
          ))}
          <Tooltip content={<Tip unidad={unidad} decimales={magnitud === 'corriente' ? 2 : 1} />} />
          {/* Con dos o mas series la leyenda va siempre: la identidad no puede
              depender solo del color. */}
          {bobinas.length >= 2 && (
            <Legend wrapperStyle={{ fontSize: 12, color: TINTA_TENUE }} iconType="plainline" />
          )}
        </ComposedChart>
      </ResponsiveContainer>
    </figure>
  );
}
