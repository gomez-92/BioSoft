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
