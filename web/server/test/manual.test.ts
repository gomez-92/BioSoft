import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { signToken } from '../src/auth/tokens.js';

// El manual es solo para usuarios del monitor. No necesita Mongo: un token
// firmado de un usuario que la tabla de sesiones no conoce pasa (ver
// auth/sesiones.ts), que es justo lo que hace falta aca.

let baseUrl = '';
let server: import('node:http').Server | undefined;

beforeAll(async () => {
  const express = (await import('express')).default;
  const { manualRouter } = await import('../src/api/manual.js');
  const { requireAuth } = await import('../src/auth/middleware.js');
  const app = express();
  app.use('/api', requireAuth, manualRouter);
  server = app.listen(0);
  const address = server.address();
  baseUrl = `http://localhost:${typeof address === 'object' && address ? address.port : 0}`;
});

afterAll(() => { server?.close(); });

const token = () => signToken({ sub: 'abc123', username: 'mario' });

describe('manual de usuario', () => {
  for (const ruta of ['/api/manual', '/api/manual.pdf']) {
    it(`${ruta} sin sesion no se entrega`, async () => {
      expect((await fetch(`${baseUrl}${ruta}`)).status).toBe(401);
    });
  }

  it('con sesion entrega el HTML y el PDF', async () => {
    const html = await fetch(`${baseUrl}/api/manual`, { headers: { authorization: `Bearer ${token()}` } });
    expect(html.status).toBe(200);
    expect(html.headers.get('content-type')).toMatch(/text\/html/);
    expect(await html.text()).toContain('nav class="toc"');

    const pdf = await fetch(`${baseUrl}/api/manual.pdf`, { headers: { authorization: `Bearer ${token()}` } });
    expect(pdf.status).toBe(200);
    expect(pdf.headers.get('content-type')).toBe('application/pdf');
    expect(Buffer.from(await pdf.arrayBuffer()).subarray(0, 4).toString()).toBe('%PDF');
  });
});

// La copia en server/manual/ no puede quedar vieja. Si falla: `npm run
// sync:generador` desde web/. En el contenedor no esta docs/: se saltea.
describe('copia del manual', () => {
  for (const archivo of ['manual-de-usuario.html', 'manual-de-usuario.pdf']) {
    const original = fileURLToPath(new URL(`../../../docs/${archivo}`, import.meta.url));
    const copia = fileURLToPath(new URL(`../manual/${archivo}`, import.meta.url));
    it.skipIf(!existsSync(original))(`${archivo} es identica a docs/`, () => {
      expect(readFileSync(copia).equals(readFileSync(original))).toBe(true);
    });
  }
});
