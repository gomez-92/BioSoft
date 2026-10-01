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
expectError("contrasena de wifi sin SSID (renglon a medio cargar)",
  s => { s.wifi[2].ssid = ""; s.wifi[2].password = "algo"; }, "no SSID");
expectError("puerto de broker fuera de rango",
  s => s.broker.port = 70000, "entre 1 y 65535");
expectWarn("puerto 1883: MQTT sin cifrar hacia un broker remoto",
  s => s.broker.port = 1883, "texto plano");
expectWarn("red wifi sin contrasena",
  s => s.wifi[0].password = "", "red abierta");
expectWarn("red wifi repetida",
  s => s.wifi[1].ssid = s.wifi[0].ssid, "repetida");
expectWarn("sin ninguna red wifi cargada",
  s => s.wifi.forEach(n => { n.ssid = ""; n.password = ""; }), "quedan las compiladas");
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
// que el firmware trunca toda clave a 15. Con ese tope, y con la telemetria
// repartida en grupos, ninguno se acerca al buffer. Se verifica el tope, no
// un error que no puede existir.
(function(){
  reset();
  ["magneticField","temperature"].forEach(k =>
    T.state.telemetry.fields[k].name = "NOMBRE_LARGUISIMO_QUE_NO_ENTRA");
  T.validate();
  check("claves larguisimas no generan error de payload (el firmware trunca a 15)",
        !T.errors.some(e => e.includes("payload")), T.errors.join(" | "));
  check("claves larguisimas si avisan que se truncan",
        T.warns.some(w => w.includes("se truncara")), T.warns.join(" | "));
})();
expectError("dos grupos de telemetria en el mismo topic",
  s => s.telemetry.groups.coils.topic = s.telemetry.groups.measures.topic,
  "no puede distinguir un mensaje del otro");
expectError("cadencia de un grupo periodico en 0",
  s => s.telemetry.groups.coils.interval = 0, "entero positivo en ms");
expectWarn("alertas con retencion (son eventos, no estado)",
  s => s.telemetry.groups.alerts.retain = true, "como si acabara de ocurrir");
expectWarn("grupo de resultado deshabilitado",
  s => s.telemetry.groups.result.enabled = false, "no se entera de por que termino");
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
expectWarn("campo de telemetria renombrado (rompe el dashboard remoto)",
  s => s.telemetry.fields.health.name = "SALUD", "el dashboard espera");
expectWarn("control.enabled en false no es una corrida real",
  s => s.control.enabled = false, "no una corrida real");
expectWarn("settlingTime por debajo del default",
  s => s.intervals.mega.settlingTime = 2000, "acerca el primer flag posible");

console.log("\n== 4. Salida serializada ==");
reset(); T.validate();
const out = JSON.stringify(T.buildJson(), null, 2);
check("ningun entero sale con punto decimal", !/: *\d+\.0(?=[,\n])/.test(out));
check("schemaVersion siempre 1", T.buildJson().schemaVersion === 1);

console.log("\n== runType ==");
expectError("un runType que no es normal ni test bloquea la descarga",
  st => { st.runType = "prueba"; }, "Tipo de corrida");
expectWarn("runType test avisa que las corridas salen marcadas como prueba",
  st => { st.runType = "test"; }, "PRUEBA");
reset(); T.validate();
check("runType normal no agrega avisos", !T.warns.some(w => w.includes("Tipo de corrida")));
reset(); T.state.runType = "test";
check("el archivo generado lleva el runType elegido", T.buildJson().runType === "test");
check("runType va justo despues de schemaVersion (archivo diffeable contra el ejemplo)",
      Object.keys(T.buildJson())[1] === "runType");

console.log("\n== Interruptores de banco (requireMega, detector.enabled, sensor de TEMP1) ==");
reset(); T.validate();
check("los defaults exigen el Mega y dejan el detector encendido",
      T.state.requireMega === true && T.state.detector.enabled === true);
expectWarn("requireMega false avisa que es solo para banco",
  st => { st.requireMega = false; }, "placa de control");
expectError("requireMega no booleano bloquea la descarga",
  st => { st.requireMega = "false"; }, "requireMega");
expectWarn("detector apagado avisa que nada puede cortar",
  st => { st.detector.enabled = false; }, "Detector apagado");
expectError("detector.enabled no booleano bloquea la descarga",
  st => { st.detector.enabled = "no"; }, "detector.enabled");
expectWarn("TEMP1 en sim avisa que no mide el gabinete",
  st => { st.detector.sources[1].sensor = "sim"; }, "TEMP1 con sensor sim");
expectWarn("TEMP1 none vigilada avisa que nunca corta por temperatura",
  st => { st.detector.sources[1].sensor = "none"; }, "sin termometro");
expectError("CEM1 no acepta none (es la realimentacion del lazo)",
  st => { st.detector.sources[0].sensor = "none"; }, "CEM1: sensor");
expectError("TEMP1 no acepta un driver de CEM1",
  st => { st.detector.sources[1].sensor = "mlx90393"; }, "TEMP1: sensor");
reset();
const out2 = T.buildJson();
check("requireMega va despues de runType", Object.keys(out2)[2] === "requireMega");
check("detector.enabled va antes de sources", Object.keys(out2.detector)[0] === "enabled");

console.log("\n== Escenarios sinteticos (tarjeta 22) ==");
reset(); T.validate();
check("de fabrica CEM1 es el sensor real (mlx90393)",
      T.state.detector.sources[0].sensor === "mlx90393");
check("de fabrica ninguna fuente usa escenario",
      T.state.detector.sources.every(s => s.sensor !== "scenario"));
expectWarn("CEM1 en sim avisa que las bobinas quedan bloqueadas",
  st => { st.detector.sources[0].sensor = "sim"; }, "BLOQUEADAS");
expectError("noise negativo en un escenario bloquea la descarga",
  st => { st.detector.sources[1].sensor = "scenario"; st.detector.sources[1].scenario.noise = -1; }, "noise");
expectError("setpointDuty fuera de 0..1 bloquea la descarga",
  st => { st.detector.sources[0].sensor = "scenario"; st.detector.sources[0].scenario.setpointDuty = 1.5; }, "duty que da el objetivo");
expectWarn("oscilacion sin periodo avisa que no oscila",
  st => { st.detector.sources[0].sensor = "scenario"; st.detector.sources[0].scenario.oscAmp = 1; }, "no oscila");

// Los perfiles: tienen que dejar las dos fuentes en escenario, con la senal
// que dicen, y sin errores -- un perfil que no se puede descargar no sirve.
const profileOf = key => {
  reset();
  ctx.applyProfile(key);
  T.validate();
  return T.state.detector.sources;
};
let src = profileOf("tempAlta");
check("perfil temperatura alta: las dos fuentes en escenario",
      src.every(s => s.sensor === "scenario"));
check("perfil temperatura alta: TEMP1 salta a +2,5 a los 60 s",
      src[1].scenario.stepAt === 60 && src[1].scenario.step === 2.5);
check("perfil temperatura alta: sin errores", T.errors.length === 0, T.errors.join(" | "));
src = profileOf("campoInestable");
check("perfil campo inestable: prende CEM1 en el Detector (viene apagada)", src[0].enabled === true);
check("perfil campo inestable: CEM1 conserva la planta", src[0].scenario.setpointDuty === 0.5);
src = profileOf("campoFuera");
check("perfil campo fuera de rango: CEM1 sin planta", src[0].scenario.setpointDuty === 0);
src = profileOf("normal");
check("perfil todo normal: sin errores", T.errors.length === 0, T.errors.join(" | "));
check("perfil todo normal: CEM1 sigue apagada en el Detector", src[0].enabled === false);
reset();
ctx.applyProfile("tempAlta");
ctx.realSensors();
check("volver a sensores reales restaura mlx90393 / ds18b20",
      T.state.detector.sources[0].sensor === "mlx90393" && T.state.detector.sources[1].sensor === "ds18b20");

console.log(fails === 0 ? "\nTODO OK\n" : "\n" + fails + " FALLA(S)\n");
process.exit(fails ? 1 : 0);
