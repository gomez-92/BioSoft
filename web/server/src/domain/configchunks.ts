// Transporte de la configuracion remota (tarjeta 24), del lado del monitor.
// Espeja esp32/src/remoteconfig.hpp: si algo de aca cambia, cambia alla.
//
// La configuracion (~7 KB) viaja en BLOQUES porque la placa tiene un solo
// buffer MQTT de 512 bytes. El tope del bloque se mide en caracteres YA
// ESCAPADOS dentro de un string JSON, que es lo que de verdad ocupa el
// mensaje: una comilla ocupa dos.

export const MAX_CHUNK_ESCAPED = 340;

/** Lo que ocupa un caracter dentro de un string JSON (igual que la placa). */
function escapedLength(char: string): number {
  if (char === '"' || char === '\\') return 2;
  if (char.charCodeAt(0) < 0x20) return 6;
  return 1;
}

/**
 * Parte el texto en bloques cuyo largo escapado no supera el tope. Mismo
 * algoritmo que RemoteConfig::chunkLength() en la placa. El texto es ASCII
 * (el generador no produce otra cosa en las claves; las etiquetas con tilde
 * se escapan al serializar), asi que un caracter es un byte.
 */
export function chunkText(text: string, max = MAX_CHUNK_ESCAPED): string[] {
  const chunks: string[] = [];
  let start = 0;
  while (start < text.length) {
    let escaped = 0;
    let end = start;
    while (end < text.length) {
      const next = escapedLength(text[end]);
      if (escaped + next > max) break;
      escaped += next;
      end++;
    }
    chunks.push(text.slice(start, end));
    start = end;
  }
  return chunks;
}

// CRC32 IEEE (el de zip/Ethernet), el mismo que calcula la placa para el
// configId. Se implementa aca y no con zlib.crc32 porque esa funcion recien
// existe desde Node 22.2, y el monitor no deberia depender de la version
// exacta de la imagen.
const TABLA = (() => {
  const tabla = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    tabla[n] = c >>> 0;
  }
  return tabla;
})();

export function crc32(text: string): string {
  let crc = 0xFFFFFFFF;
  for (const byte of Buffer.from(text, 'utf8')) {
    crc = TABLA[(crc ^ byte) & 0xFF] ^ (crc >>> 8);
  }
  return ((crc ^ 0xFFFFFFFF) >>> 0).toString(16).padStart(8, '0');
}

/**
 * Lo que se le manda a la placa: la configuracion SIN wifi ni broker. Las
 * credenciales no viajan nunca por MQTT; la placa conserva las del archivo
 * vigente al grabar.
 */
export function sinCredenciales(config: Record<string, unknown>): Record<string, unknown> {
  const { wifi: _wifi, broker: _broker, ...resto } = config;
  return resto;
}
