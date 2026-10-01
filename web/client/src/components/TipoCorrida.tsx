import { etiquetaTipoCorrida } from '../lib/format.js';

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
