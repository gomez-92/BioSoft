// Verificacion de tools/generador-config.html.
//
//   node tools/test-generador.js
//
// Extrae el <script> del HTML y lo corre en node con un DOM minimo, para poder
// ejercitar validate()/buildJson() sin browser. Es el equivalente de los
// `pio test -e native` del mega2560: fija en un test lo que se rompe en
// silencio, que aca es cada regla que el firmware aplicaria descartando un
// valor sin decir nada.
//
// No necesita instalar nada: solo node y los modulos que ya trae.
const fs = require("fs");
const vm = require("vm");
const path = require("path");

const REPO = path.join(__dirname, "..");
const html = fs.readFileSync(path.join(REPO, "tools/generador-config.html"), "utf8");
const code = html.match(/<script>([\s\S]*)<\/script>/)[1];

// DOM minimo: solo lo que el script toca.
const nodes = {};
function fakeEl(id){
  return { id, innerHTML:"", textContent:"", disabled:false, dataset:{},
           addEventListener(){}, click(){}, files:[], value:"" };
}
["msgs","app","preview","file","dl","btnReset","btnLoad"].forEach(id => nodes[id] = fakeEl(id));

const document = {
  getElementById: id => nodes[id] || (nodes[id] = fakeEl(id)),
  addEventListener(){},
  createElement: () => fakeEl("tmp")
};

const ctx = { document, console, JSON, Number, Math, RegExp, Object, Array, String,
              Blob: class {}, URL: {createObjectURL:()=> "", revokeObjectURL(){}},
              FileReader: class {}, setTimeout, alert: () => {} };
vm.createContext(ctx);
// let/const del script no se exponen como propiedades del contexto (solo var y
// las declaraciones de funcion), asi que se agrega un puente en el mismo scope
// lexico para poder leerlos desde el harness.
vm.runInContext(code + `
var __t = {
  get errors(){ return errors; }, get warns(){ return warns; },
  get state(){ return state; }, set state(v){ state = v; },
  DEFAULTS: DEFAULTS, validate: validate, buildJson: buildJson
};`, ctx);
const T = ctx.__t;

let fails = 0;
function check(name, cond, extra){
  if (cond) console.log("  OK   " + name);
  else { fails++; console.log("  FAIL " + name + (extra ? "\n       " + extra : "")); }
}

console.log("\n== 1. Defaults ==");
T.validate();
check("los defaults no producen ningun error", T.errors.length === 0, T.errors.join("\n       "));
check("el unico aviso con defaults es CEM1 deshabilitada", T.warns.length === 1,
      "avisos: " + T.warns.length + "\n       " + T.warns.join("\n       "));
T.warns.forEach(w => console.log("       aviso: " + w.slice(0, 90) + "..."));

console.log("\n== 2. El archivo generado coincide con docs/config.example.json ==");
const generated = T.buildJson();
const example = JSON.parse(fs.readFileSync(path.join(REPO, "docs/config.example.json"), "utf8"));
check("mismo contenido que el ejemplo del esquema",
      JSON.stringify(generated) === JSON.stringify(example),
      "generado:\n" + JSON.stringify(generated).slice(0,200) + "\nejemplo:\n" + JSON.stringify(example).slice(0,200));

console.log("\n== 3. Trampas que el generador tiene que atajar ==");
const reset = () => { T.state = JSON.parse(JSON.stringify(T.DEFAULTS)); };
function expectError(name, mutate, needle){
  reset(); mutate(T.state); T.validate();
  const hit = T.errors.some(e => e.includes(needle));
  check(name, hit, hit ? "" : "errores: " + (T.errors.join(" | ") || "(ninguno)"));
}
function expectWarn(name, mutate, needle){
  reset(); mutate(T.state); T.validate();
  const hit = T.warns.some(w => w.includes(needle));
  check(name, hit, hit ? "" : "avisos: " + (T.warns.join(" | ") || "(ninguno)"));
}
// Para lo que NO tiene que bloquear la descarga: una regla de mas es tan
// molesta como una de menos.
function expectOk(name, mutate){
  reset(); mutate(T.state); T.validate();
  check(name, T.errors.length === 0, "errores: " + T.errors.join(" | "));
}

expectError("intervalo con decimales (is<unsigned long>() lo rechazaria)",
  s => s.intervals.mega.ping = 2000.5, "entero en ms");
expectError("duracion de menu con decimales (quedaria en 0)",
  s => s.menus.duration[0].value = 60000.5, "quedaria en 0");
expectError("measureTemperature por debajo del bloqueo del DS18B20",
  s => s.intervals.mega.measureTemperature = 500, "no puede bajar de 1000");
expectError("rango normal sin ningun critico que lo contenga (dejaria el otro combo vacio)",
  s => s.menus.temperatureNormal[0] = {label:"20~60 C", min:20, max:60},
  "ningun rango critico");
expectError("rango critico que no contiene a ningun normal",
  s => s.menus.temperatureCritical[0] = {label:"0~5 C", min:0, max:5},
  "no contiene a ningun");
expectOk("par valido aunque no todos los pares lo sean (la pantalla filtra)",
  s => { s.menus.temperatureNormal.push({label:"10~70 C", min:10, max:70});
         s.menus.temperatureCritical.push({label:"5~75 C", min:5, max:75}); });
expectError("splashTimeout por debajo del minimo de pantalla",
  s => s.intervals.esp32.splashTimeout = 200, "no puede bajar de 1000");
expectError("busyTimeout por debajo del minimo de pantalla",
  s => s.intervals.esp32.busyTimeout = 999, "no puede bajar de 1000");
expectError("calibrationFactor fuera de 0,1-5,0",
  s => s.coils[0].calibrationFactor = 7, "0,1 a 5,0");
expectError("nombre de bobina repetido (config_coil resuelve por nombre)",
  s => s.coils[1].name = "BOB1", "esta repetido");
expectError("bufferSize fuera de 8-32",
  s => s.detector.sources[0].bufferSize = 64, "entre 8 y 32");
expectError("threshold mayor que bufferSize",
  s => s.detector.sources[1].rules.frequency.threshold = 40, "entre 1 y bufferSize");
expectError("maxEvents fuera de 1-10",
  s => s.detector.sources[1].rules.critical.maxEvents = 99, "entre 1 y 10");
expectError("criticalMultiplier fuera de 1,0-3,0",
  s => s.detector.sources[0].range.criticalMultiplier = 5, "1,0 a 3,0");
expectError("par (address, channel) duplicado",
  s => { s.currentSensors[1].address = 72; s.currentSensors[1].channel = 0; }, "esta repetido");
expectError("channel 2, que no existe en un ADS1115",
  s => s.currentSensors[0].channel = 2, "no tiene un par diferencial 2 ni 3");
// El payload NO puede desbordar: Topics::TelemetryField::name es char[16], asi
// que el firmware trunca toda clave a 15. Con ese tope la tanda mas pesada da
// 94 de 128 bytes. Se verifica el tope, no un error que no puede existir.
(function(){
  reset();
  ["magneticField","temperature","current"].forEach(k =>
    T.state.telemetry.fields[k].name = "NOMBRE_LARGUISIMO_QUE_NO_ENTRA");
  T.validate();
  check("claves larguisimas no generan error de payload (el firmware trunca a 15)",
        !T.errors.some(e => e.includes("payload")), T.errors.join(" | "));
  check("claves larguisimas si avisan que se truncan",
        T.warns.some(w => w.includes("se truncara")), T.warns.join(" | "));
})();
expectWarn("una tanda sin ningun campo habilitado no se publica",
  s => { s.telemetry.fields.health.enabled = false;
         s.telemetry.fields.progress.enabled = false;
         s.telemetry.fields.elapsedTime.enabled = false; }, "esa publicacion no se manda");
expectError("etiqueta de menu vacia",
  s => s.menus.frequency[0].label = "", "falta la etiqueta");
expectError("rango de temperatura invertido",
  s => { s.menus.temperatureNormal[0].min = 50; }, "tiene que ser menor que");

expectWarn("busyTimeout que no supera a mega.sendState",
  s => { s.intervals.esp32.busyTimeout = 5000; s.intervals.mega.sendState = 5000; },
  "antes de recibir la confirmacion del Mega");
expectWarn("bobinas habilitadas todas en el mismo grupo de fase",
  s => { s.coils[1].enabled = false; s.coils[2].enabled = true; }, "mismo grupo de fase");
expectWarn("sampleRate distinto entre los dos canales de un mismo ADS1115",
  s => { s.currentSensors[1].enabled = true; s.currentSensors[1].sampleRate = 128; },
  "gana el ultimo que se configura");
expectWarn("campo de telemetria renombrado (rompe el Decoder de Datacake)",
  s => s.telemetry.fields.health.name = "SALUD", "Decoder de Datacake espera");
expectWarn("control.enabled en false no es una corrida real",
  s => s.control.enabled = false, "no una corrida real");
expectWarn("settlingTime por debajo del default",
  s => s.intervals.mega.settlingTime = 2000, "acerca el primer flag posible");

console.log("\n== 4. Salida serializada ==");
reset(); T.validate();
const out = JSON.stringify(T.buildJson(), null, 2);
check("ningun entero sale con punto decimal", !/: *\d+\.0(?=[,\n])/.test(out));
check("schemaVersion siempre 1", T.buildJson().schemaVersion === 1);

console.log(fails === 0 ? "\nTODO OK\n" : "\n" + fails + " FALLA(S)\n");
process.exit(fails ? 1 : 0);
