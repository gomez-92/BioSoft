import { useRef, useState } from 'react';
import {
  Area, CartesianGrid, ComposedChart, Legend, Line, ReferenceArea, ReferenceDot, ReferenceLine,
  ResponsiveContainer, Tooltip, XAxis, YAxis,
} from 'recharts';
import { detalleAlerta, graficoDeFuente } from '../lib/alertas.js';
import { esAlertaCritica, numero } from '../lib/format.js';
import { multiplicadorDe, rangosCampo, rangosTemperatura, type Rangos } from '../lib/rangos.js';
import { aMilisegundos, bucketEnMs, insertarHuecos } from '../lib/series.js';
import type { AlertItem, Targets } from '../lib/types.js';

// Graficos de la corrida, en el detalle y en vivo.
//
// Reglas que valen para todos, y que no son de gusto:
//
//  - UN SOLO EJE por grafico. Campo (mT) y temperatura (°C) no comparten
//    grafico aunque compartan el tiempo: dos escalas en un mismo dibujo hacen
//    que el cruce de dos lineas parezca significar algo cuando no significa
//    nada.
//  - Cada punto agregado muestra su banda MIN-MAX ademas del promedio. El
//    promedio solo esconde el pico que disparo la alerta, que es justamente
//    lo que alguien viene a buscar.
//  - Los huecos se dibujan como huecos (ver insertarHuecos).
//  - Cada alerta va en el grafico de SU magnitud (CEM1 en el campo, TEMP1 en
//    la temperatura), no en todos: una racha de temperatura marcada sobre el
//    campo sugiere un problema de campo que no existe. Se dibuja como un
//    punto a la altura de la ultima medicion de esa fuente, con una linea
//    vertical tenue para ubicarla en el tiempo, y al pasar por encima (o
//    tocarla, en el celular) muestra su detalle.
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

/** Lo que el detalle de una alerta necesita saber de la corrida. */
export interface ContextoCorrida {
  targets: Targets | null;
  config?: unknown;
  inicio: number;
}

function ejeTiempo(inicio: number, fin: number) {
  const corto = fin - inicio < 15 * 60_000;
  return {
    dataKey: 't' as const,
    type: 'number' as const,
    domain: [inicio, fin] as [number, number],
    // El dominio manda aunque haya puntos afuera: con una ventana deslizante
    // se pasa el ultimo punto anterior a la ventana para que la linea entre
    // desde el borde, y recharts lo recorta en vez de agrandar el eje.
    allowDataOverflow: true,
    scale: 'time' as const,
    // En una corrida corta (las pruebas de banco suelen durar minutos) las
    // marcas en hh:mm se repetian; con segundos se distinguen.
    tickFormatter: (valor: number) =>
      new Date(valor).toLocaleTimeString('es-AR', corto
        ? { hour: '2-digit', minute: '2-digit', second: '2-digit' }
        : { hour: '2-digit', minute: '2-digit' }),
    stroke: TINTA_TENUE,
    fontSize: 11,
    minTickGap: 24,
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

/**
 * Altura a la que se dibuja una alerta: la ultima medicion de su fuente que
 * trae la propia alerta; si no vino, el punto de la serie mas cercano antes
 * de ella.
 */
function alturaDeAlerta(alerta: AlertItem, datos: ReadonlyArray<{ t: number }>, clave: string): number | undefined {
  if (alerta.value !== undefined) return alerta.value;
  const t = new Date(alerta.ts).getTime();
  let mejor: number | undefined;
  for (const punto of datos) {
    if (punto.t > t) break;
    const valor = (punto as Record<string, unknown>)[clave];
    if (typeof valor === 'number') mejor = valor;
  }
  return mejor;
}

interface AlertaActiva { alerta: AlertItem; x: number; y: number }

/** El globo con el detalle de una alerta, sobre el grafico. */
function GloboAlerta({ activa, ancho, contexto, onCerrar }: {
  activa: AlertaActiva; ancho: number; contexto: ContextoCorrida; onCerrar: () => void;
}) {
  const detalle = detalleAlerta(activa.alerta, contexto);
  // Anclado al lado que tenga lugar: cerca del borde derecho se abre hacia la
  // izquierda, si no se saldria del grafico.
  const haciaIzquierda = activa.x > ancho * 0.55;
  return (
    <div
      className={`globo-alerta ${esAlertaCritica(activa.alerta.type) ? 'globo-critica' : ''}`}
      style={{
        left: haciaIzquierda ? undefined : activa.x + 12,
        right: haciaIzquierda ? ancho - activa.x + 12 : undefined,
        top: Math.max(0, activa.y - 20),
      }}
      onClick={onCerrar}
      role="tooltip"
    >
      <p className="globo-titulo">{detalle.titulo}</p>
      {detalle.lineas.map((linea) => <p key={linea} className="globo-linea">{linea}</p>)}
    </div>
  );
}

function marcasDeAlerta(
  alertas: AlertItem[], datos: ReadonlyArray<{ t: number }>, clave: string,
  dominioY: number | undefined, onActivar: (activa: AlertaActiva | null) => void,
) {
  return alertas.flatMap((alerta, index) => {
    const x = new Date(alerta.ts).getTime();
    const color = esAlertaCritica(alerta.type) ? CRITICO : ADVERTENCIA;
    const y = alturaDeAlerta(alerta, datos, clave) ?? dominioY;
    const marcas = [
      <ReferenceLine key={`l-${alerta.ts}-${index}`} x={x} stroke={color}
        strokeDasharray="3 3" strokeWidth={1} strokeOpacity={0.55} />,
    ];
    if (y !== undefined) {
      marcas.push(
        <ReferenceDot key={`p-${alerta.ts}-${index}`} x={x} y={y} r={6} ifOverflow="extendDomain"
          shape={(props: { cx?: number; cy?: number }) => {
            const cx = props.cx ?? 0;
            const cy = props.cy ?? 0;
            return (
              <g className="marca-alerta"
                onMouseEnter={() => onActivar({ alerta, x: cx, y: cy })}
                onMouseLeave={() => onActivar(null)}
                onClick={(e) => { e.stopPropagation(); onActivar({ alerta, x: cx, y: cy }); }}>
                {/* Area de toque mas grande que el punto visible: en el
                    celular un circulo de 6 px no se puede tocar. */}
                <circle cx={cx} cy={cy} r={14} fill="transparent" />
                <circle cx={cx} cy={cy} r={6} fill={color} stroke={SUPERFICIE} strokeWidth={2} />
                {alerta.count >= alerta.limit && (
                  <circle cx={cx} cy={cy} r={10} fill="none" stroke={color} strokeWidth={1.5} />
                )}
              </g>
            );
          }} />,
      );
    }
    return marcas;
  });
}

/** Contenedor comun: guarda el ancho (para el globo) y la alerta activa. */
function useGlobo() {
  const area = useRef<HTMLDivElement>(null);
  const [activa, setActiva] = useState<AlertaActiva | null>(null);
  return { area, activa, setActiva, ancho: area.current?.clientWidth ?? 0 };
}

function bandas(rangos: Rangos, color: string, decimales: number) {
  return (
    <>
      {rangos.normal && (
        <ReferenceArea y1={rangos.normal[0]} y2={rangos.normal[1]} fill={color} fillOpacity={0.08}
          stroke="none" ifOverflow="extendDomain" />
      )}
      {/* Los limites criticos son los que cortan el experimento: van en rojo
          de estado, no en un color de serie. */}
      {rangos.critico && (
        <ReferenceLine y={rangos.critico[1]} stroke={CRITICO} strokeDasharray="4 4" strokeWidth={1}
          ifOverflow="extendDomain"
          label={{ value: `critico ${numero(rangos.critico[1], decimales)}`, fill: CRITICO, fontSize: 10, position: 'insideTopRight' }} />
      )}
      {rangos.critico && (
        <ReferenceLine y={rangos.critico[0]} stroke={CRITICO} strokeDasharray="4 4" strokeWidth={1}
          ifOverflow="extendDomain" />
      )}
    </>
  );
}

function filtrarPorGrafico(alertas: AlertItem[], grafico: 'campo' | 'temperatura'): AlertItem[] {
  return alertas.filter((alerta) => graficoDeFuente(alerta.source) === grafico);
}

/** Campo magnetico, con la banda normal y los limites criticos que vigila el Mega. */
export function GraficoCampo({ puntos, bucket, contexto, alertas, desde, fin, alto = ALTO }: {
  puntos: PuntoMedicion[]; bucket?: string; contexto: ContextoCorrida;
  alertas: AlertItem[]; fin: number; alto?: number;
  /** Borde izquierdo del eje (ventana deslizante); sin el, el inicio de la corrida. */
  desde?: number;
}) {
  const globo = useGlobo();
  const datos = aMilisegundos(insertarHuecos(puntos, bucketEnMs(bucket)) as PuntoMedicion[]);
  const rangos = rangosCampo(contexto.targets, multiplicadorDe(contexto.config));
  const propias = filtrarPorGrafico(alertas, 'campo');

  return (
    <figure className="figura">
      <figcaption>
        Campo magnetico <span className="unidad">mT</span>
        {rangos.normal && <span className="nota"> · banda: rango normal · punteado rojo: critico</span>}
      </figcaption>
      <div className="grafico-area" ref={globo.area} onMouseLeave={() => globo.setActiva(null)}>
        <ResponsiveContainer width="100%" height={alto}>
          <ComposedChart data={datos} margin={{ top: 8, right: 12, bottom: 4, left: -8 }}>
            <CartesianGrid stroke={GRILLA} vertical={false} />
            <XAxis {...ejeTiempo(desde ?? contexto.inicio, fin)} />
            <YAxis stroke={TINTA_TENUE} fontSize={11} width={56}
              tickFormatter={(v: number) => numero(v, 2)} />
            {bandas(rangos, SERIE[0], 3)}
            {rangos.objetivo !== undefined && (
              <ReferenceLine y={rangos.objetivo} stroke="#3987e5" strokeDasharray="4 4" strokeWidth={1}
                label={{ value: `objetivo ${numero(rangos.objetivo, 2)}`, fill: TINTA_TENUE, fontSize: 10, position: 'insideBottomLeft' }} />
            )}
            {/* La banda min-max del bucket: sin esto el promedio esconde los picos. */}
            <Area type="monotone" dataKey="fieldMax" stroke="none" fill={SERIE[0]} fillOpacity={0.18}
              connectNulls={false} isAnimationActive={false} name="max" />
            <Area type="monotone" dataKey="fieldMin" stroke="none" fill={SUPERFICIE} fillOpacity={1}
              connectNulls={false} isAnimationActive={false} name="min" />
            <Line type="monotone" dataKey="field" stroke={SERIE[0]} strokeWidth={2}
              dot={bucket === 'raw' && datos.length < 120 ? { r: 2 } : false}
              connectNulls={false} isAnimationActive={false} name="campo" />
            {!globo.activa && <Tooltip content={<Tip unidad="mT" decimales={3} />} />}
            {marcasDeAlerta(propias, datos, 'field', rangos.objetivo, globo.setActiva)}
          </ComposedChart>
        </ResponsiveContainer>
        {globo.activa && (
          <GloboAlerta activa={globo.activa} ancho={globo.ancho} contexto={contexto}
            onCerrar={() => globo.setActiva(null)} />
        )}
      </div>
    </figure>
  );
}

/** Temperatura, con las bandas normal y critica del experimento. */
export function GraficoTemperatura({ puntos, bucket, contexto, alertas, desde, fin, alto = ALTO }: {
  puntos: PuntoMedicion[]; bucket?: string; contexto: ContextoCorrida;
  alertas: AlertItem[]; fin: number; alto?: number;
  /** Borde izquierdo del eje (ventana deslizante); sin el, el inicio de la corrida. */
  desde?: number;
}) {
  const globo = useGlobo();
  const datos = aMilisegundos(insertarHuecos(puntos, bucketEnMs(bucket)) as PuntoMedicion[]);
  const rangos = rangosTemperatura(contexto.targets);
  const propias = filtrarPorGrafico(alertas, 'temperatura');
  const medio = rangos.normal ? (rangos.normal[0] + rangos.normal[1]) / 2 : undefined;

  return (
    <figure className="figura">
      <figcaption>
        Temperatura <span className="unidad">°C</span>
        {rangos.normal && <span className="nota"> · banda: rango normal · punteado rojo: critico</span>}
      </figcaption>
      <div className="grafico-area" ref={globo.area} onMouseLeave={() => globo.setActiva(null)}>
        <ResponsiveContainer width="100%" height={alto}>
          <ComposedChart data={datos} margin={{ top: 8, right: 12, bottom: 4, left: -8 }}>
            <CartesianGrid stroke={GRILLA} vertical={false} />
            <XAxis {...ejeTiempo(desde ?? contexto.inicio, fin)} />
            <YAxis stroke={TINTA_TENUE} fontSize={11} width={56}
              tickFormatter={(v: number) => numero(v, 0)} />
            {bandas(rangos, '#199e70', 0)}
            <Area type="monotone" dataKey="tempMax" stroke="none" fill={SERIE[1]} fillOpacity={0.18}
              connectNulls={false} isAnimationActive={false} name="max" />
            <Area type="monotone" dataKey="tempMin" stroke="none" fill={SUPERFICIE} fillOpacity={1}
              connectNulls={false} isAnimationActive={false} name="min" />
            <Line type="monotone" dataKey="temp" stroke={SERIE[1]} strokeWidth={2}
              dot={bucket === 'raw' && datos.length < 120 ? { r: 2 } : false}
              connectNulls={false} isAnimationActive={false} name="temperatura" />
            {!globo.activa && <Tooltip content={<Tip unidad="°C" decimales={1} />} />}
            {marcasDeAlerta(propias, datos, 'temp', medio, globo.setActiva)}
          </ComposedChart>
        </ResponsiveContainer>
        {globo.activa && (
          <GloboAlerta activa={globo.activa} ancho={globo.ancho} contexto={contexto}
            onCerrar={() => globo.setActiva(null)} />
        )}
      </div>
    </figure>
  );
}

/**
 * Corriente y duty por bobina. Son dos graficos y no uno con dos ejes: amperes
 * y porcentaje no comparten escala, y superponerlos haria que el cruce de dos
 * lineas pareciera un evento.
 */
export function GraficoBobinas({ puntos, bucket, bobinas, magnitud, inicio, fin, alto = ALTO }: {
  puntos: Array<Record<string, unknown> & { ts: string }>; bucket?: string;
  bobinas: number[]; magnitud: 'corriente' | 'duty'; inicio: number; fin: number; alto?: number;
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
      <ResponsiveContainer width="100%" height={alto}>
        <ComposedChart data={datos} margin={{ top: 8, right: 12, bottom: 4, left: -8 }}>
          <CartesianGrid stroke={GRILLA} vertical={false} />
          <XAxis {...ejeTiempo(inicio, fin)} />
          <YAxis stroke={TINTA_TENUE} fontSize={11} width={56}
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
