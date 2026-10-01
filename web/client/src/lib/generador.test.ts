import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

// La seccion Configuracion embebe una COPIA del generador (tarjeta 24): el
// contenedor se construye solo con web/ y tools/ no llega adentro. Una copia
// a mano se desincroniza en silencio -- el mismo problema de los dos
// seriallink.hpp --, y entonces el monitor validaria con reglas viejas.
// Este test es el que lo impide: si falla, correr `npm run sync:generador`.
const original = fileURLToPath(new URL('../../../../tools/generador-config.html', import.meta.url));
const copia = fileURLToPath(new URL('../../public/generador-config.html', import.meta.url));

describe('generador embebido', () => {
  // Dentro del contenedor no esta tools/: ahi no hay contra que comparar.
  it.skipIf(!existsSync(original))('la copia es identica a tools/generador-config.html', () => {
    expect(readFileSync(copia, 'utf8')).toBe(readFileSync(original, 'utf8'));
  });
});
