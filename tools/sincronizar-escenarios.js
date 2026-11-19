// Copia tools/escenarios.json (los escenarios de fabrica) dentro del
// generador, en `const SCENARIO_FILE = ...;`.
//
//   node tools/sincronizar-escenarios.js
//
// El generador abre con doble clic, sin servidor, asi que no puede leer el
// archivo: lleva una copia. tools/test-generador.js falla si difieren; esto
// es lo que hay que correr entonces. Despues, `npm run sync:generador` (desde
// web/) lleva el generador y los escenarios al monitor.
const fs = require("fs");
const path = require("path");

const html = path.join(__dirname, "generador-config.html");
const perfiles = fs.readFileSync(path.join(__dirname, "escenarios.json"), "utf8").trim();
JSON.parse(perfiles);   // que sea JSON valido antes de tocar nada

const texto = fs.readFileSync(html, "utf8");
const inicio = texto.indexOf("const SCENARIO_FILE = ");
const fin = texto.indexOf("\nconst SCENARIO_PROFILES = ", inicio);
if (inicio < 0 || fin < 0) throw new Error("no encuentro SCENARIO_FILE en el generador");
// Respeta los finales de linea del archivo.
const nl = texto.includes("\r\n") ? "\r\n" : "\n";
const nuevo = texto.slice(0, inicio) + "const SCENARIO_FILE = " + perfiles.replace(/\r?\n/g, nl) + ";" + nl +
  texto.slice(fin + 1);
fs.writeFileSync(html, nuevo);
console.log("Escenarios de fabrica copiados en tools/generador-config.html");
