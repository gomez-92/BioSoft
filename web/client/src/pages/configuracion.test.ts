import { describe, expect, it } from 'vitest';
import { estadoEnvio } from './Configuracion.js';

// El aviso del ultimo envio: "grabada" (la placa confirmo) y "vigente" (la
// placa reinicio y la informa) son cosas distintas, y confundirlas haria
// creer que un escenario ya esta corriendo cuando la placa sigue con el
// anterior.
const base = { requestId: 'x', state: 'aceptada', createdAt: '2026-10-02T12:00:00Z', newConfigId: 'cbf43926' };

describe('estadoEnvio', () => {
  it('sin envios no dice nada', () => {
    expect(estadoEnvio(undefined, 'abc')).toBeNull();
  });

  it('aceptada y todavia no vigente: confirmada, pendiente de reinicio', () => {
    const e = estadoEnvio(base, '00000000')!;
    expect(e.kind).toBe('ok');
    expect(e.text).toContain('confirmo');
    expect(e.detail).toContain('proximo reinicio');
  });

  it('aceptada y ya informada como vigente: vigente', () => {
    expect(estadoEnvio(base, 'cbf43926')!.text).toContain('Vigente');
  });

  it('en curso muestra el avance por bloques', () => {
    const e = estadoEnvio({ ...base, state: 'parcial', chunks: 5, sentChunks: 2 }, null)!;
    expect(e.kind).toBe('progress');
    expect(e.text).toContain('2 de 5');
  });

  it('un rechazo es error y aclara que la placa sigue con la anterior', () => {
    const e = estadoEnvio({ ...base, state: 'no_entregada', message: 'la placa no confirmo el bloque 1 de 5' }, null)!;
    expect(e.kind).toBe('error');
    expect(e.text).toBe('La placa no respondio');
    expect(e.detail).toContain('anterior');
  });
});
