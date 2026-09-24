import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  parseAlert, parseCoils, parseElapsed, parseMeasures, parseResult, parseStatus, parseTargets,
} from '../src/domain/telemetry.js';

// Mismo criterio que las suites nativas del Mega: fijar lo que se rompe EN
// SILENCIO. Nada de esto falla con un error visible si se rompe -- se traduce
// en una base con datos mal tipados, o en un valor que deja de llegar.

// Los avisos de clave desconocida son parte del comportamiento esperado en
// varios casos; silenciarlos evita ensuciar la salida de los tests.
beforeEach(() => { vi.spyOn(console, 'warn').mockImplementation(() => {}); });

describe('parseElapsed', () => {
  it('convierte "hh:mm:ss" a segundos', () => {
    expect(parseElapsed('00:00:00')).toBe(0);
    expect(parseElapsed('00:33:00')).toBe(1980);
    expect(parseElapsed('01:00:00')).toBe(3600);
  });

  // La placa no acota las horas a 24: SystemData::elapsedTime() sigue contando.
  it('acepta mas de 24 horas', () => {
    expect(parseElapsed('36:10:05')).toBe(130205);
  });

  it('rechaza lo que no tiene esa forma en vez de inventar un numero', () => {
    for (const value of ['1:2:3', '00:60:00', '00:00:99', 'ayer', '', 3600, null, undefined]) {
      expect(parseElapsed(value)).toBeUndefined();
    }
  });
});

describe('parseMeasures', () => {
  it('parsea los dos campos', () => {
    expect(parseMeasures({ CEM1: 1.5249, TEMP1: 25.26 }))
      .toEqual({ magneticField: 1.5249, temperature: 25.26 });
  });

  // La tarjeta SD puede deshabilitar cada campo por separado, asi que un
  // mensaje con uno solo es legitimo y no se puede descartar.
  it('acepta un mensaje con un solo campo', () => {
    expect(parseMeasures({ TEMP1: 24 })).toEqual({ magneticField: undefined, temperature: 24 });
  });

  it('descarta el mensaje que no trae ningun campo util', () => {
    expect(parseMeasures({})).toBeNull();
    expect(parseMeasures({ OTRA: 1 })).toBeNull();
  });

  // Un numero como texto es un cambio de contrato, no un valor: convertirlo
  // por las nuestras esconderia el problema dentro de datos que parecen sanos.
  it('ignora un numero que viene como texto', () => {
    expect(parseMeasures({ CEM1: '1.5', TEMP1: 24 })?.magneticField).toBeUndefined();
  });

  it('no deja pasar NaN ni Infinity a la base', () => {
    expect(parseMeasures({ CEM1: NaN, TEMP1: Infinity })).toBeNull();
  });

  it('rechaza lo que no es un objeto', () => {
    for (const value of [null, 42, 'hola', [1, 2]]) expect(parseMeasures(value)).toBeNull();
  });
});

describe('parseCoils', () => {
  // El conjunto de claves presentes ES informacion: dice cuantas bobinas hay
  // montadas. Dos bobinas tienen que dar dos entradas, no cuatro con ceros.
  it('devuelve solo las bobinas que reportaron', () => {
    expect(parseCoils({ c1: 0.85, d1: 43.6, c2: 0.8, d2: 44.7 })).toEqual([
      { n: 1, current: 0.85, duty: 43.6 },
      { n: 2, current: 0.8, duty: 44.7 },
    ]);
  });

  it('acepta una bobina que reporta duty sin corriente', () => {
    expect(parseCoils({ d3: 40 })).toEqual([{ n: 3, current: undefined, duty: 40 }]);
  });

  it('conserva el numero real de bobina, no la posicion', () => {
    expect(parseCoils({ c4: 0.7, d4: 38 })).toEqual([{ n: 4, current: 0.7, duty: 38 }]);
  });

  it('descarta el mensaje sin ninguna bobina', () => {
    expect(parseCoils({})).toBeNull();
  });
});

describe('parseStatus', () => {
  it('parsea el grupo completo y convierte el tiempo', () => {
    expect(parseStatus({
      ESTADO: 'critical', PROGRESS: 67, ELAPSED_TIME: '00:40:00',
      REMAINING: 1200, STATE: 'running', MEGA: true,
    })).toEqual({
      health: 'critical', progress: 67, elapsedSeconds: 2400, elapsedText: '00:40:00',
      remainingSeconds: 1200, state: 'running', megaOk: true,
    });
  });

  // MEGA:false es justamente el caso que importa desde afuera -- la diferencia
  // entre "el experimento va bien" y "hace rato que no sabemos nada de la
  // placa que lo controla". No se puede perder por tratarlo como ausente.
  it('conserva MEGA en false', () => {
    expect(parseStatus({ MEGA: false })?.megaOk).toBe(false);
  });

  it('conserva PROGRESS en 0', () => {
    expect(parseStatus({ PROGRESS: 0 })?.progress).toBe(0);
  });
});

describe('parseTargets', () => {
  // MODE es lo unico que distingue el grupo tratado del grupo control.
  it('parsea el modo y las consignas', () => {
    const parsed = parseTargets({
      MODE: 'campo nulo', CEM: 1.5, FREQ: 50, DUR: 60, TOL: 10,
      TNMIN: 20, TNMAX: 30, TCMIN: 15, TCMAX: 35,
    });
    expect(parsed?.mode).toBe('campo nulo');
    expect(parsed?.cem).toBe(1.5);
    expect(parsed?.tcmax).toBe(35);
  });

  it('descarta un objeto vacio', () => {
    expect(parseTargets({})).toBeNull();
  });
});

describe('parseAlert', () => {
  it('parsea una alerta completa', () => {
    expect(parseAlert({ SRC: 'TEMP1', TYPE: 'critical', COUNT: 4, LIMIT: 4 }))
      .toEqual({ source: 'TEMP1', type: 'critical', count: 4, limit: 4 });
  });

  // A diferencia del resto, una alerta se descarta ENTERA: sin count ni limit
  // no dice si el experimento esta por cortarse o recien empieza a quejarse.
  it('descarta la alerta incompleta en vez de guardar la mitad', () => {
    expect(parseAlert({ SRC: 'TEMP1', TYPE: 'critical', COUNT: 4 })).toBeNull();
    expect(parseAlert({ SRC: 'TEMP1', COUNT: 4, LIMIT: 4 })).toBeNull();
    expect(parseAlert({ TYPE: 'critical', COUNT: 4, LIMIT: 4 })).toBeNull();
  });

  it('acepta COUNT en 0', () => {
    expect(parseAlert({ SRC: 'CEM1', TYPE: 'streak', COUNT: 0, LIMIT: 3 })?.count).toBe(0);
  });
});

describe('parseResult', () => {
  // El corte critico es el que trae los campos crudos, y es el mensaje mas
  // ancho: sin SRC/TYPE/COUNT/LIMIT la pantalla tendria que adivinar el
  // detalle a partir de la prosa del Mega.
  it('parsea un corte critico con sus campos crudos', () => {
    expect(parseResult({
      REASON: 'critical', DESC: 'TEMP1: limite de alertas critical alcanzado (4/4)',
      SRC: 'TEMP1', TYPE: 'critical', COUNT: 4, LIMIT: 4,
      PROGRESS: 55, ELAPSED: '00:33:00', MEAN: 1.49,
    })).toEqual({
      reason: 'critical', description: 'TEMP1: limite de alertas critical alcanzado (4/4)',
      progressPercent: 55, elapsedSeconds: 1980, elapsedText: '00:33:00',
      meanMagneticField: 1.49, source: 'TEMP1', type: 'critical', count: 4, limit: 4,
      emergency: undefined,
    });
  });

  // El paro fisico y el Detener en pantalla llegan los dos como "stopped": lo
  // unico que los distingue es `emerg`, y en false tiene tanto significado
  // como en true.
  it('conserva emerg en false', () => {
    expect(parseResult({ REASON: 'stopped', emerg: false })?.emergency).toBe(false);
  });

  it('acepta un corte completado sin campos opcionales', () => {
    const parsed = parseResult({ REASON: 'completed', DESC: 'Duracion alcanzada', PROGRESS: 100 });
    expect(parsed?.reason).toBe('completed');
    expect(parsed?.source).toBeUndefined();
  });

  // Sin REASON no se puede decir por que termino el experimento, que es lo
  // unico que este mensaje aporta y nadie mas sabe.
  it('descarta un result sin REASON', () => {
    expect(parseResult({ DESC: 'algo paso' })).toBeNull();
  });
});

describe('certificado del broker desde el entorno', () => {
  // Un .env no admite valores multilinea, asi que el PEM viaja con "\n"
  // literales. Si no se devuelven a saltos reales, TLS rechaza el certificado
  // con el mismo error que si no estuviera cargado -- y entonces parece que el
  // secreto no llego, cuando en realidad llego mal. Costo un despliegue.
  it('convierte los escapes literales en saltos de linea reales', async () => {
    const { pemDesdeEntorno } = await import('../src/mqtt/ingestor.js');
    // Tal cual queda en un archivo .env: la barra y la ene como dos caracteres.
    const plano = '-----BEGIN CERTIFICATE-----\\nMIIB\\nabc\\n-----END CERTIFICATE-----';

    const pem = pemDesdeEntorno(plano);

    expect(pem.split('\n')).toHaveLength(4);
    expect(pem.startsWith('-----BEGIN CERTIFICATE-----\n')).toBe(true);
    expect(pem).not.toContain('\\n');
  });

  it('deja intacto un PEM que ya viene con saltos reales', async () => {
    const { pemDesdeEntorno } = await import('../src/mqtt/ingestor.js');
    const pem = '-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----';
    expect(pemDesdeEntorno(pem)).toBe(pem);
  });
});
