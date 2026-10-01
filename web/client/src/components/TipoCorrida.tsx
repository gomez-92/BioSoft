import { etiquetaTipoCorrida, relajacionesConAviso } from '../lib/format.js';

// Chip de tipo de corrida. Es lo que separa datos de banco de un experimento
// con animales, y desde afuera no hay otra forma de saberlo: una corrida de
// prueba se ve, en todo lo demas, igual que una real. Un experimento normal
// no lleva chip (ver etiquetaTipoCorrida).
export function TipoCorrida({ runType }: { runType?: string | null }) {
  const etiqueta = etiquetaTipoCorrida(runType);
  if (!etiqueta) return null;
  const clase = runType === 'test' ? 'chip-prueba' : 'chip-sin-marca';
  return <span className={`chip ${clase}`}>{etiqueta}</span>;
}

// Un experimento (runType normal) que corrio con protecciones apagadas o con
// datos simulados: los numeros pueden ser reales, pero no son los de un
// experimento en condiciones. En una corrida de prueba no se marca: ahi las
// relajaciones son lo esperado.
export function ConRelajaciones({ runType, relaxations }: {
  runType?: string | null; relaxations?: number | null;
}) {
  if (runType !== 'normal' || relajacionesConAviso(relaxations).length === 0) return null;
  return <span className="chip chip-relajada">CON RELAJACIONES</span>;
}
