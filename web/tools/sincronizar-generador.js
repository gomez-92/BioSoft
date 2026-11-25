// Copia el generador de configuracion (tools/generador-config.html, en la raiz
// del repo) al front del monitor, que lo embebe en la seccion Configuracion
// (tarjeta 24).
//
//   npm run sync:generador      (desde web/)
//
// Por que una COPIA y no una referencia: el contenedor del monitor se
// construye solo con web/ (Dockerfile, contexto de fly/render), y tools/ no
// llega adentro. Una copia a mano se desincroniza -- el mismo problema de los
// dos seriallink.hpp --, asi que client/src/lib/generador.test.ts falla si la
// copia y el original difieren. Ese test es el que obliga a correr esto.
const fs = require('fs');
const path = require('path');

const origen = path.join(__dirname, '..', '..', 'tools', 'generador-config.html');
const destino = path.join(__dirname, '..', 'client', 'public', 'generador-config.html');

fs.mkdirSync(path.dirname(destino), { recursive: true });
fs.copyFileSync(origen, destino);
console.log(`Generador copiado a ${path.relative(process.cwd(), destino)}`);

// El manual de usuario (docs/manual-de-usuario.html y su PDF) se lee desde la
// pagina Manual del monitor: misma razon de la copia y mismo control
// (server/test/manual.test.ts). Va al SERVIDOR y no a client/public/, porque
// es solo para quien tiene cuenta: lo de public/ se sirve sin sesion, y
// server/src/api/manual.ts lo entrega detras de requireAuth.
const docs = path.join(__dirname, '..', '..', 'docs');
const destinoDocs = path.join(__dirname, '..', 'server', 'manual');
fs.mkdirSync(destinoDocs, { recursive: true });
for (const archivo of ['manual-de-usuario.html', 'manual-de-usuario.pdf']) {
  fs.copyFileSync(path.join(docs, archivo), path.join(destinoDocs, archivo));
  console.log(`Manual copiado a ${path.relative(process.cwd(), path.join(destinoDocs, archivo))}`);
}

// Los escenarios de fabrica (tools/escenarios.json) van al servidor, que los
// siembra en la biblioteca la primera vez. Como modulo .ts y no como .json:
// asi entra al build de tsc sin import attributes. Mismo control que el
// generador: server/test/escenarios.test.ts falla si difieren.
const perfiles = JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'tools', 'escenarios.json'), 'utf8'));
const destinoPerfiles = path.join(__dirname, '..', 'server', 'src', 'domain', 'escenariosfabrica.ts');
fs.writeFileSync(destinoPerfiles,
  '// GENERADO por `npm run sync:generador` desde tools/escenarios.json. No editar a mano.\n' +
  'export const ESCENARIOS_FABRICA = ' + JSON.stringify(perfiles, null, 2) + ' as const;\n');
console.log(`Escenarios de fabrica copiados a ${path.relative(process.cwd(), destinoPerfiles)}`);
