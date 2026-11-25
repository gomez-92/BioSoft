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

// Topes de la placa (ConfigLoader: MaxSsidLength, MaxBroker*Length...), sin
// el terminador.
export const MAX_REDES = 5;
const TOPE = { ssid: 32, clave: 63, host: 63, usuario: 31, claveBroker: 63, clientId: 31 };

/**
 * WiFi y broker viajan a la placa desde 2026-11-19 (antes no viajaban nunca).
 * Las contrasenas van SOLO de ida: una red o un broker sin `password`
 * conserva la que ya tiene la placa. Devuelve el motivo si algo no entra en
 * la placa, o null. La placa vuelve a validar (y no deja quitar la red en
 * uso).
 */
export function problemaDeCredenciales(config: Record<string, unknown>): string | null {
  const texto = (v: unknown) => typeof v === 'string';
  if (config.wifi !== undefined) {
    if (!Array.isArray(config.wifi)) return 'wifi tiene que ser una lista de redes';
    if (config.wifi.length > MAX_REDES) return `no entran más de ${MAX_REDES} redes WiFi`;
    for (const red of config.wifi as Array<Record<string, unknown>>) {
      if (!red || typeof red !== 'object' || !texto(red.ssid) || !(red.ssid as string).trim()) {
        return 'hay una red WiFi sin nombre (ssid)';
      }
      if ((red.ssid as string).length > TOPE.ssid) return `el nombre de red "${red.ssid}" pasa de ${TOPE.ssid} caracteres`;
      if (red.password !== undefined && (!texto(red.password) || (red.password as string).length > TOPE.clave)) {
        return `la contraseña de "${red.ssid}" tiene que ser texto de hasta ${TOPE.clave} caracteres`;
      }
    }
  }
  if (config.broker !== undefined) {
    const b = config.broker as Record<string, unknown>;
    if (!b || typeof b !== 'object' || Array.isArray(b)) return 'broker tiene que ser un objeto';
    const largo = (k: string, max: number) => b[k] !== undefined && (!texto(b[k]) || (b[k] as string).length > max);
    if (largo('server', TOPE.host)) return `broker.server tiene que ser texto de hasta ${TOPE.host} caracteres`;
    if (largo('user', TOPE.usuario)) return `broker.user tiene que ser texto de hasta ${TOPE.usuario} caracteres`;
    if (largo('password', TOPE.claveBroker)) return `broker.password tiene que ser texto de hasta ${TOPE.claveBroker} caracteres`;
    if (largo('clientId', TOPE.clientId)) return `broker.clientId tiene que ser texto de hasta ${TOPE.clientId} caracteres`;
    if (b.port !== undefined && !(Number.isInteger(b.port) && (b.port as number) >= 1 && (b.port as number) <= 65535)) {
      return 'broker.port tiene que ser un entero de 1 a 65535';
    }
  }
  return null;
}

/**
 * La misma configuracion SIN contrasenas: es lo que se guarda en la base
 * (el pedido) y lo unico que vuelve de la placa. Las contrasenas solo pasan
 * por el monitor camino a la placa.
 */
export function sinContrasenas(config: Record<string, unknown>): Record<string, unknown> {
  const copia: Record<string, unknown> = { ...config };
  if (Array.isArray(config.wifi)) {
    copia.wifi = (config.wifi as Array<Record<string, unknown>>).map(({ password: _p, ...red }) => red);
  }
  if (config.broker && typeof config.broker === 'object') {
    const { password: _p, ...broker } = config.broker as Record<string, unknown>;
    copia.broker = broker;
  }
  return copia;
}
