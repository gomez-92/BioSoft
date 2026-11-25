import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { CODIGOS, nombreCodigo, pasaFiltro, type EventoPlaca } from './bitacora.js';

// CODIGOS es una copia a mano del catalogo de la placa (EventCodes en
// esp32/src/eventlog.hpp). Si la placa suma un codigo y aca no, el evento se
// ve igual pero con el codigo crudo y sin opcion en el filtro: este test lo
// avisa antes. Dentro del contenedor no esta esp32/: se saltea.
const firmware = fileURLToPath(new URL('../../../../esp32/src/eventlog.hpp', import.meta.url));

describe('catalogo de la bitacora', () => {
  it.skipIf(!existsSync(firmware))('tiene los mismos codigos que eventlog.hpp', () => {
    const texto = readFileSync(firmware, 'utf8');
    const deLaPlaca = [...texto.matchAll(/constexpr const char\* (\w+)\s*= "(\w+)";/g)]
      .filter(([, nombre]) => nombre !== 'Topic')
      .map(([, nombre, valor]) => {
        expect(valor).toBe(nombre);   // la macro EVENTO usa el identificador como codigo
        return valor;
      });
    expect(Object.keys(CODIGOS).sort()).toEqual(deLaPlaca.sort());
  });

  it('un codigo desconocido se muestra crudo', () => {
    expect(nombreCodigo('mega_reinicio')).toBe('El Mega se reinició');
    expect(nombreCodigo('codigo_nuevo')).toBe('codigo_nuevo');
  });

  it('filtra los eventos en vivo como la API', () => {
    const sin = { niveles: '', codigo: '', alcance: '', corrida: '' };
    const evento = { level: 'W', code: 'broker_caido', run: null } as EventoPlaca;
    const enCorrida = { ...evento, run: { id: 'r1', number: 3, startedAt: '', runType: 'test', state: 'running' } };
    expect(pasaFiltro(evento, sin)).toBe(true);
    expect(pasaFiltro(evento, { ...sin, niveles: 'E,W' })).toBe(true);
    expect(pasaFiltro(evento, { ...sin, niveles: 'E' })).toBe(false);
    expect(pasaFiltro(evento, { ...sin, codigo: 'broker_ok' })).toBe(false);
    expect(pasaFiltro(evento, { ...sin, alcance: 'in' })).toBe(false);
    expect(pasaFiltro(enCorrida, { ...sin, alcance: 'in' })).toBe(true);
    expect(pasaFiltro(enCorrida, { ...sin, alcance: 'out' })).toBe(false);
    expect(pasaFiltro(enCorrida, { ...sin, corrida: 'r1' })).toBe(true);
    expect(pasaFiltro(enCorrida, { ...sin, corrida: 'r2' })).toBe(false);
  });
});
