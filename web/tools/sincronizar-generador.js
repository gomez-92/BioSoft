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
