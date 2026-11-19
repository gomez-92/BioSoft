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
["msgs","app","preview","file","dl","send","btnReset","btnLoad"].forEach(id => nodes[id] = fakeEl(id));

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
  DEFAULTS: DEFAULTS, validate: validate, buildJson: buildJson, loadParsed: loadParsed,
  SCENARIO_PROFILES: SCENARIO_PROFILES, SCENARIO_FILE: SCENARIO_FILE, SIM: SIM
};
var secCountFor = k => secCount[k];`, ctx);
const T = ctx.__t;

let fails = 0;
function check(name, cond, extra){
  if (cond) console.log("  OK   " + name);
  else { fails++; console.log("  FAIL " + name + (extra ? "\n       " + extra : "")); }
}

console.log("\n== 1. Defaults ==");
T.validate();
check("los defaults no producen ningun error", T.errors.length === 0, T.errors.join("\n       "));
check("los unicos avisos con defaults son CEM1 deshabilitada y campo nulo sin mapa", T.warns.length === 2,
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
// Desde la tarjeta 25 el silencio corta: vigilada y sin sensor, cortaria
// en cada corrida.
expectError("TEMP1 none vigilada bloquea la descarga (cortaria por silencio)",
  st => { st.detector.sources[1].sensor = "none"; }, "sin termometro");
reset(); T.state.runType = "test";
T.state.detector.sources[1].sensor = "none"; T.state.detector.sources[1].enabled = false; T.validate();
check("TEMP1 none sin vigilar es valido (en una prueba)", T.errors.length === 0, T.errors.join(" | "));
expectError("maxMissedSamples fuera de 1..20 bloquea la descarga",
  st => { st.detector.sources[1].maxMissedSamples = 0; }, "lecturas perdidas");
expectError("maxMissedSamples con decimales bloquea la descarga",
  st => { st.detector.sources[0].maxMissedSamples = 2.5; }, "lecturas perdidas");
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

// Los perfiles son una COPIA de tools/escenarios.json (el generador abre sin
// servidor y no puede leerlo). Si difieren, el Mega comprueba unos perfiles y
// el generador ofrece otros.
const archivoPerfiles = JSON.parse(fs.readFileSync(path.join(REPO, "tools/escenarios.json"), "utf8"));
check("los perfiles del generador son identicos a tools/escenarios.json",
      JSON.stringify(ctx.__t.SCENARIO_FILE) === JSON.stringify(archivoPerfiles),
      "correr node tools/sincronizar-escenarios.js");

// Los escenarios son POR FUENTE: cada uno deja SU fuente en escenario, con
// la senal y la vigilancia que dice el archivo, no toca la otra, y no deja
// errores -- un escenario que no se puede descargar no sirve.
const srcOf = name => T.state.detector.sources.find(s => s.name === name);
const otherOf = name => name === "CEM1" ? "TEMP1" : "CEM1";
let perfilesSinError = true;
T.SCENARIO_PROFILES.forEach(prof => {
  reset();
  const antes = JSON.stringify(srcOf(otherOf(prof.source)));
  ctx.applyProfile(prof.key);
  T.validate();
  if (T.errors.length) { perfilesSinError = false; console.log("       " + prof.key + ": " + T.errors.join(" | ")); }
  const s = srcOf(prof.source);
  if (s.sensor !== "scenario" || s.enabled !== prof.enabled ||
      JSON.stringify(s.scenario) !== JSON.stringify(prof.scenario)) {
    perfilesSinError = false; console.log("       " + prof.key + " no quedo como el archivo");
  }
  if (JSON.stringify(srcOf(otherOf(prof.source))) !== antes) {
    perfilesSinError = false; console.log("       " + prof.key + " toco la otra fuente");
  }
});
check("cada escenario deja SU fuente como dice el archivo y no toca la otra, sin errores", perfilesSinError);
check("cada escenario es de una sola fuente (TEMP1 o CEM1)",
      T.SCENARIO_PROFILES.every(p => p.source === "TEMP1" || p.source === "CEM1"));
check("la pareja de cada fuente existe y es de esa fuente",
      ["TEMP1", "CEM1"].every(n => (T.SCENARIO_PROFILES.find(p => p.key === T.SCENARIO_FILE.pairWith[n]) || {}).source === n));
reset(); ctx.applyProfile("tempAlta");
check("temperatura alta: TEMP1 salta a +2,5 a los 60 s",
      JSON.stringify(srcOf("TEMP1").scenario.points) === "[[0,0],[60,0],[60,2.5]]");
reset(); ctx.applyProfile("campoFuera");
check("campo fuera de rango: CEM1 sin planta y vigilada",
      srcOf("CEM1").scenario.setpointDuty === 0 && srcOf("CEM1").enabled === true);
// La combinacion que pidio el usuario: cada fuente por su lado.
reset(); ctx.applyProfile("tempNormal"); ctx.applyProfile("campoInestable");
check("se combinan: temperatura normal + campo inestable",
      ctx.profileOfSource(T.state, "TEMP1") === "tempNormal" && ctx.profileOfSource(T.state, "CEM1") === "campoInestable");
ctx.realSensorFor("CEM1");
check("volver una fuente a su sensor real no toca la otra",
      srcOf("CEM1").sensor === "mlx90393" && ctx.profileOfSource(T.state, "TEMP1") === "tempNormal");
reset(); ctx.blankScenarioFor("TEMP1");
check("escenario nuevo: TEMP1 en escenario con la senal plana, como prueba",
      srcOf("TEMP1").sensor === "scenario" && JSON.stringify(srcOf("TEMP1").scenario.points) === "[]" &&
      T.state.runType === "test");

// El escenario de cada fuente se deduce de los valores: tiene que reconocer
// cada uno recien aplicado, dejar de reconocerlo apenas se edita un numero, y
// reconocer los sensores reales (incluida una config sin seccion detector,
// que es como corre una placa sin tarjeta).
let perfilesOk = true;
T.SCENARIO_PROFILES.forEach(prof => {
  reset(); ctx.applyProfile(prof.key);
  if (ctx.profileOfSource(T.state, prof.source) !== prof.key) { perfilesOk = false; console.log("       no reconoce " + prof.key); }
});
check("cada escenario aplicado se reconoce como tal", perfilesOk);
reset(); ctx.applyProfile("tempAlta"); srcOf("TEMP1").scenario.points[2][1] = 3;
check("un escenario editado a mano pasa a personalizado", ctx.profileOfSource(T.state, "TEMP1") === "custom");
reset();
check("los defaults son sensores reales",
      ctx.profileOfSource(T.state, "TEMP1") === "real" && ctx.profileOfSource(T.state, "CEM1") === "real" &&
      ctx.profileSummary(T.state) === "sensores reales");
check("una config sin detector (valores de fabrica) son sensores reales", ctx.profileSummary({}) === "sensores reales");
reset(); srcOf("TEMP1").sensor = "none";
check("TEMP1 en none se informa como sin sensor", ctx.profileOfSource(T.state, "TEMP1") === "none");
reset(); ctx.applyProfile("campoInestable"); srcOf("CEM1").enabled = false;
check("campo inestable con CEM1 sin vigilar no se marca como ese escenario",
      ctx.profileOfSource(T.state, "CEM1") !== "campoInestable");
reset(); ctx.applyProfile("tempAlta"); ctx.applyProfile("campoInestable");
check("el resumen nombra el escenario de cada fuente",
      ctx.profileSummary(T.state) === "Temperatura: Temperatura alta · Campo: Campo inestable");

// El simulador del editor (SIM) es una copia en JS de lo que hace el Mega.
// Se corre con EXACTAMENTE las combinaciones de mega2560/test/test_scenario_runs
// (rangos de los menus, fases de muestreo, paso de 50 ms, duracion minima +
// 2 min) y se le exige el mismo final que alli: si el Mega y el editor dejan
// de coincidir, falla uno de los dos tests.
console.log("\n== Simulador del editor vs. el Mega (tools/escenarios.json) ==");
const COMBOS = [[1, 5, 30, 40, 25, 45], [1, 10, 28, 42, 23, 47], [2, 5, 26, 44, 20, 50],
                [2, 10, 30, 40, 20, 50], [1, 5, 26, 44, 25, 45]];
const PHASES = [[250, 1100], [900, 2600], [1650, 4300]];
// Como en el Mega: cada escenario con la otra fuente en su pareja (el normal).
T.SCENARIO_PROFILES.forEach(prof => {
  reset(); ctx.applyProfile(T.SCENARIO_FILE.pairWith[otherOf(prof.source)]); ctx.applyProfile(prof.key);
  const fallas = [];
  COMBOS.forEach(([i, tol, nmin, nmax, cmin, cmax]) => PHASES.forEach(([cp, tp]) => {
    const r = T.SIM.run(T.state, {intensity: i, frequency: 10, tolerance: tol,
      tempNormal: {min: nmin, max: nmax}, tempCritical: {min: cmin, max: cmax},
      durationS: prof.minDuration + 120, mode: "x"}, {stepMs: 50, cemPhaseMs: cp, tempPhaseMs: tp});
    const e = prof.expect;
    const ok = e.reason === "completed"
      ? !r.cut && (e.alerts || []).every(a => r.events.some(ev => ev.type === a))
      : r.cut && r.cut.source === e.source && r.cut.type === e.type &&
        Math.abs(r.cut.at - e.at) <= 15 && r.cut.at <= prof.minDuration;
    if (!ok) fallas.push(i + " mT " + tol + "%, " + nmin + "~" + nmax + "/" + cmin + "~" + cmax +
                         ", fase " + cp + "/" + tp + ": " + (r.cut ? r.cut.type + " de " + r.cut.source +
                         " a los " + r.cut.at.toFixed(1) + " s" : "completo"));
  }));
  check("simulador: " + prof.name + " -> " + ctx.expectText(prof), fallas.length === 0, fallas.slice(0, 3).join(" | "));
});
reset();
T.state.control.map = [];
T.state.detector.sources[0].sensor = "scenario";
const nulo = T.SIM.run(T.state, {intensity: 1, frequency: 10, tolerance: 5, tempNormal: {min: 30, max: 40},
  tempCritical: {min: 25, max: 45}, durationS: 60, mode: "null"});
check("simulador: campo nulo sin punto del mapa no inicia (como planStart)", nulo.refused === "nomap");
check("simulador: niveles <-> valores ida y vuelta",
      [-3, -1.5, -0.4, 0, 0.7, 1.3, 2.6].every(l => {
        const r = {normalMin: 30, normalMax: 40, criticalMin: 25, criticalMax: 45};
        return Math.abs(T.SIM.valueToLevel(r, T.SIM.levelToValue(r, l)) - l) < 1e-9;
      }));
const ruido = T.SIM.makeNoise(0x7654321);
check("simulador: el ruido queda en [-1, 1]", [ruido(), ruido(), ruido(), ruido()].every(v => v >= -1 && v <= 1));

console.log("\n== Escenarios: puntos de control ==");
expectError("mas de 8 puntos bloquea la descarga",
  st => { st.detector.sources[1].sensor = "scenario";
          st.detector.sources[1].scenario.points = Array.from({length: 9}, (_, k) => [k * 10, 0]); }, "maximo es 8");
expectError("segundos fuera de orden bloquean la descarga",
  st => { st.detector.sources[1].sensor = "scenario";
          st.detector.sources[1].scenario.points = [[60, 1], [30, 2]]; }, "en orden");
expectError("un segundo con decimales bloquea la descarga",
  st => { st.detector.sources[1].sensor = "scenario";
          st.detector.sources[1].scenario.points = [[30.5, 1]]; }, "entero");
expectError("un nivel mayor a 10 bloquea la descarga",
  st => { st.detector.sources[1].sensor = "scenario";
          st.detector.sources[1].scenario.points = [[0, 0], [60, 12]]; }, "entre -10 y 10");
expectError("un texto de puntos que no se entiende bloquea la descarga",
  st => { st.detector.sources[1].sensor = "scenario";
          st.detector.sources[1].scenario.points = "0:0 sesenta:2"; }, "no se entiende");
check("el texto de puntos acepta coma decimal y salto en el mismo segundo",
      JSON.stringify(ctx.textToPoints("0:0  60:0 60:2,5")) === "[[0,0],[60,0],[60,2.5]]");
check("puntos -> texto -> puntos no cambia nada",
      JSON.stringify(ctx.textToPoints(ctx.pointsToText([[0, 0], [60, -1.25]]))) === "[[0,0],[60,-1.25]]");
expectWarn("una oscilacion de 4 s en CEM1 (medida cada 2 s) avisa que no se ve",
  st => { st.detector.sources[0].sensor = "scenario";
          st.detector.sources[0].scenario.oscAmp = 1.6; st.detector.sources[0].scenario.oscPeriod = 4; },
  "demasiado rapida");
expectWarn("planta de CEM1 con el lazo apagado avisa que el campo queda en 0",
  st => { st.detector.sources[0].sensor = "scenario"; st.control.enabled = false; }, "lazo apagado");

// Una tarjeta escrita con el formato de la tarjeta 22 se lee como la misma
// senal en puntos, y no vuelve a escribir las claves viejas (el firmware ya
// no las lee).
const viejo = sc => ctx.normalizeScenario(Object.assign({noise: 0.1, oscAmp: 0, oscPeriod: 0, dropAt: 0}, sc));
check("formato viejo: salto -> dos puntos en el mismo segundo",
      JSON.stringify(viejo({base: 0, ramp: 0, stepAt: 60, step: 2.5}).points) === "[[0,0],[60,0],[60,2.5]]");
check("formato viejo: rampa -> recta hasta el tope de nivel",
      JSON.stringify(viejo({base: 0, ramp: 1, stepAt: 0, step: 0}).points) === "[[0,0],[600,10]]");
check("formato viejo: todo en 0 -> sin puntos",
      JSON.stringify(viejo({base: 0, ramp: 0, stepAt: 0, step: 0}).points) === "[]");
const conv = viejo({base: 0, ramp: 0, stepAt: 60, step: 2.5});
check("formato viejo: no quedan base/ramp/stepAt/step",
      !["base", "ramp", "stepAt", "step"].some(k => k in conv));
reset();
const cargado = JSON.parse(JSON.stringify(T.DEFAULTS));
cargado.detector.sources[1].sensor = "scenario";
cargado.detector.sources[1].scenario = {base: 0, noise: 0.1, ramp: 0, stepAt: 60, step: 2.5, oscAmp: 0, oscPeriod: 0, dropAt: 0};
T.loadParsed(cargado);
check("cargar un archivo con el formato viejo lo convierte a puntos",
      JSON.stringify(T.state.detector.sources[1].scenario.points) === "[[0,0],[60,0],[60,2.5]]");

console.log("\n== Coherencia entre pasos (fase 3) ==");
// Experimento = sensores reales + Detector, sin excepcion.
reset();
T.state.runType = "test"; T.state.detector.sources[0].sensor = "scenario"; T.state.detector.sources[1].sensor = "none";
T.state.detector.enabled = false;
T.state.runType = "normal"; ctx.enforceExperiment(); T.validate();
check("pasar a experimento vuelve las fuentes a sus sensores reales y enciende el Detector",
      T.state.detector.sources[0].sensor === "mlx90393" && T.state.detector.sources[1].sensor === "ds18b20" &&
      T.state.detector.enabled === true && T.errors.length === 0, T.errors.join(" | "));
ctx.undoExperiment();
check("deshacer devuelve lo que habia y la deja como prueba",
      T.state.runType === "test" && T.state.detector.sources[0].sensor === "scenario" &&
      T.state.detector.sources[1].sensor === "none" && T.state.detector.enabled === false);
expectError("un archivo de experimento con un sensor simulado no se puede enviar",
  st => { st.runType = "normal"; st.detector.sources[1].sensor = "sim"; }, "solo con sensores reales");
expectError("un archivo de experimento con el Detector apagado no se puede enviar",
  st => { st.runType = "normal"; st.detector.enabled = false; }, "Detector apagado");
reset(); ctx.applyProfile("tempAlta");
check("elegir un escenario pasa la corrida a prueba", T.state.runType === "test");
reset(); T.state.runType = "normal";
check("en experimento el paso Escenarios avisa que hace falta una prueba y no ofrece escenarios",
      /data-to-test/.test(ctx.sScenarios()) && !/data-scnpick/.test(ctx.sScenarios()));
reset(); T.state.runType = "test";
check("en prueba el paso Escenarios ofrece un selector por fuente",
      (ctx.sScenarios().match(/data-scnpick="/g) || []).length === 2);
// Cada error/aviso queda asociado a su paso (la pestaña se marca).
reset(); T.state.detector.sources[1].bufferSize = 99; T.validate();
check("un error de Fuentes marca el paso 5", (ctx.secCountFor("5") || {}).e > 0);
reset(); T.state.runType = "test"; T.state.detector.sources[1].sensor = "scenario";
T.state.detector.sources[1].scenario.noise = -1; T.validate();
check("un error de un escenario marca el paso 5b, no el 5",
      (ctx.secCountFor("5b") || {}).e > 0 && !((ctx.secCountFor("5") || {}).e > 0));

console.log("\n== Limites por campo (fase 3) ==");
// Los limites del campo y validate() tienen que decir lo mismo: un valor en el
// borde es valido y uno apenas afuera es error. Si alguien cambia uno solo,
// el campo dejaria escribir lo que despues bloquea la descarga (o al reves).
const BORDES = [
  // [campo, texto EXACTO de su error en validate()]: con una clave suelta
  // aparecen errores legitimos de otras reglas (bufferSize 8 deja afuera al
  // threshold 16 de frecuencia, que es justo la dependencia que se busca).
  ["detector.sources.1.bufferSize", "bufferSize tiene que ser un entero"],
  ["detector.sources.1.maxMissedSamples", "lecturas perdidas antes del corte tienen que ser"],
  ["detector.sources.0.range.criticalMultiplier", "criticalMultiplier fuera del rango"],
  ["coils.0.calibrationFactor", "calibrationFactor fuera del rango"],
  ["detector.sources.1.rules.critical.cooldown", "cooldown tiene que ser"],
  ["detector.sources.1.rules.critical.maxEvents", "maxEvents tiene que ser"],
  ["control.balanceMax", "balanceMax tiene que estar"],
];
const setP = (p, v) => { const parts = p.split("."); const last = parts.pop(); parts.reduce((o, k) => o[k], T.state)[last] = v; };
let bordesOk = true;
BORDES.forEach(([p, clave]) => {
  const lim = ctx.limitsFor(p);
  const paso = lim.int ? 1 : 0.01;
  [[lim.min, true], [lim.max, true], [lim.min - paso, false], [lim.max + paso, false]].forEach(([v, valido]) => {
    reset(); setP(p, v); T.validate();
    const hay = T.errors.some(e => e.includes(clave));
    if (hay === valido) { bordesOk = false; console.log("       " + p + " = " + v + (valido ? " deberia ser valido" : " deberia ser error")); }
  });
});
check("los limites de cada campo coinciden con los de validate()", bordesOk);
reset(); setP("detector.sources.1.bufferSize", 16);
check("el tope de threshold sigue al bufferSize de su fuente",
      ctx.limitsFor("detector.sources.1.rules.streak.threshold").max === 16);

// Al salir del campo, un valor fuera de rango vuelve al ultimo valido.
const campo = (path, value) => ({ dataset: {path, kind: "int"}, value: String(value),
  classList: {toggle(){}, remove(){}}, closest: () => null });
reset();
ctx.onEdit({type: "input", target: campo("detector.sources.1.bufferSize", 20)});
ctx.onEdit({type: "input", target: campo("detector.sources.1.bufferSize", 99)});
check("mientras se escribe, el valor fuera de rango queda (y validate lo marca)",
      T.state.detector.sources[1].bufferSize === 99 && T.errors.some(e => e.includes("bufferSize")));
const el = campo("detector.sources.1.bufferSize", 99);
ctx.onEdit({type: "change", target: el});
check("al salir del campo vuelve al ultimo valido (20)",
      T.state.detector.sources[1].bufferSize === 20 && el.value == 20 && !T.errors.some(e => e.includes("bufferSize")));
reset();
const vacio = campo("detector.sources.1.maxMissedSamples", "");
ctx.onEdit({type: "change", target: vacio});
check("un campo vaciado tampoco queda: vuelve a un valor valido", ctx.limitsFor("detector.sources.1.maxMissedSamples") &&
      Number.isInteger(T.state.detector.sources[1].maxMissedSamples) &&
      T.state.detector.sources[1].maxMissedSamples >= 1);

console.log("\n== Direccion del DS18B20 (TEMP1) ==");
// La del sensor original: real, su CRC tiene que dar.
const ROM = "28:3F:E5:57:04:E1:3D:ED";
reset(); T.state.detector.sources[1].address = "283fe55704e13ded"; T.validate();
check("una direccion valida sin separadores no da error", T.errors.length === 0, T.errors.join(" | "));
check("sale normalizada como la imprime el Mega",
      T.buildJson().detector.sources[1].address === ROM);
expectError("un digito mal copiado bloquea por CRC",
  st => { st.detector.sources[1].address = "28:3F:E5:57:04:E1:3E:ED"; }, "CRC");
expectError("otra familia OneWire no es un DS18B20",
  st => { st.detector.sources[1].address = "10:3F:E5:57:04:E1:3D:ED"; }, "no empieza con 28");
expectError("largo incorrecto",
  st => { st.detector.sources[1].address = "28:3F:E5"; }, "16 digitos");
reset(); T.state.detector.sources[1].address = "  ";
check("vacia no se escribe (rige la del firmware)", !("address" in T.buildJson().detector.sources[1]));
check("sin direccion el archivo sigue igual al ejemplo del esquema", (() => {
  reset(); return JSON.stringify(T.buildJson()) === JSON.stringify(example);
})());
reset();
const conDireccion = JSON.parse(JSON.stringify(T.DEFAULTS));
conDireccion.detector.sources[1].address = ROM;
T.loadParsed(conDireccion);
check("cargar un archivo conserva la direccion", T.buildJson().detector.sources[1].address === ROM);

console.log("\n== Cargar un archivo existente (tarjeta 24) ==");
// Antes "Cargar archivo" no traia wifi ni broker: editar una tarjeta y
// volver a descargarla borraba las credenciales en silencio.
reset();
T.loadParsed({ schemaVersion: 1, wifi: [{ ssid: "LAB", password: "x1" }], broker: { server: "b.example.com" } });
const conCred = T.buildJson();
check("cargar conserva las redes WiFi del archivo", conCred.wifi.length === 1 && conCred.wifi[0].ssid === "LAB");
check("cargar conserva el broker del archivo", conCred.broker.server === "b.example.com");
check("las filas de WiFi del formulario quedan completas", T.state.wifi.length === 5);
check("abierto con doble clic (no embebido) el archivo lleva wifi y broker",
      "wifi" in conCred && "broker" in conCred);

console.log("\n== Mapa de calibracion y campo nulo ==");
const punto = (i, f, d, b) => ({ intensity: i, frequency: f, duty: d, balance: b });
const completo = s => {
  s.control.map = [punto(1, 10, 0.3, 0.05), punto(1, 50, 0.31, 0.04),
                   punto(2, 10, 0.6, -0.03), punto(2, 50, 0.62, 0.02)];
};
reset(); completo(T.state); T.validate();
check("un mapa completo no da errores ni el aviso de cobertura",
      T.errors.length === 0 && !T.warns.some(w => w.includes("Campo nulo no disponible")),
      T.errors.concat(T.warns).join(" | "));

expectError("mas de 3 intensidades en el menu",
  s => s.menus.fieldIntensity.push({label:"3 mT", value:3}, {label:"4 mT", value:4}), "no pasa de 3 x 3");
expectError("mas de 3 frecuencias en el menu",
  s => s.menus.frequency.push({label:"20 Hz", value:20}, {label:"30 Hz", value:30}), "no pasa de 3 x 3");
check("3 intensidades y 3 frecuencias entran (9 combinaciones)", (() => {
  reset();
  T.state.menus.fieldIntensity.push({label:"3 mT", value:3});
  T.state.menus.frequency.push({label:"20 Hz", value:20});
  T.validate();
  return !T.errors.some(e => e.includes("no pasa de 3 x 3"));
})());
expectError("mas de 9 puntos en el mapa",
  s => { s.control.map = Array.from({length: 10}, (_, k) => punto(1 + k, 10, 0.3, 0)); }, "9 como maximo");
expectError("punto con duty 0 (sin medir)",
  s => { completo(s); s.control.map[0].duty = 0; }, "todavia sin medir");
expectError("punto con duty mayor que 1",
  s => { completo(s); s.control.map[0].duty = 1.2; }, "el duty tiene que ser mayor que 0");
expectError("punto con intensidad 0",
  s => { completo(s); s.control.map[0].intensity = 0; }, "la intensidad tiene que estar");
expectError("balance mayor que balanceMax (el Mega rechazaria el arranque)",
  s => { completo(s); s.control.map[0].balance = 0.3; }, "supera control.balanceMax");
expectError("balance fuera del tope absoluto 0.5",
  s => { completo(s); s.control.map[0].balance = 0.7; }, "entre -0.5 y 0.5");
expectError("balanceMax fuera de 0..0.5",
  s => { s.control.balanceMax = 0.9; }, "control.balanceMax tiene que estar");
expectError("dos puntos para la misma combinacion",
  s => { completo(s); s.control.map[1] = punto(1, 10, 0.4, 0.0); }, "Un punto por combinacion");
expectWarn("combinaciones del menu sin punto: campo nulo no disponible",
  s => { completo(s); s.control.map.pop(); }, "Campo nulo no disponible para 2 mT / 50 Hz");
// Desde la fase 3 es error: el punto se elige de los menus y uno que no esta
// en ellos no se usaria nunca.
expectError("punto del mapa que no coincide con ningun menu",
  s => { completo(s); s.control.map[0].intensity = 1.5; }, "no coinciden con ninguna combinacion");
check("la frecuencia del mapa puede escribirse entera o con decimales (el firmware la lee como float)", (() => {
  reset(); completo(T.state); T.state.control.map[0].frequency = 10.0; T.validate();
  return T.errors.length === 0;
})());
check("el mapa sigue indexado por valor si se cambia el menu de frecuencias", (() => {
  reset(); completo(T.state);
  T.state.menus.frequency = [{label:"20 Hz", value:20}, {label:"50 Hz", value:50}];
  T.validate();
  return T.warns.some(w => w.includes("1 mT / 20 Hz")) && T.errors.some(w => w.includes("no coinciden"));
})());
check("'agregar las combinaciones que faltan' deja los puntos sin medir (duty 0) y no inventa valores", (() => {
  reset(); T.state.control.map = [punto(1, 10, 0.3, 0.05)];
  ctx.fillMapGaps();
  const m = T.state.control.map;
  return m.length === 4 && m.slice(1).every(p => p.duty === 0 && p.balance === 0);
})());
check("el mapa se serializa dentro de control y sobrevive a cargar el archivo", (() => {
  reset(); completo(T.state);
  const json = JSON.parse(JSON.stringify(T.buildJson()));
  reset(); T.loadParsed(json);
  return JSON.stringify(T.buildJson().control.map) === JSON.stringify(json.control.map) &&
         T.buildJson().control.balanceMax === 0.2;
})());
check("un archivo sin mapa (anterior a esta clave) carga con balanceMax y mapa de fabrica", (() => {
  reset(); T.loadParsed({ schemaVersion: 1, control: { enabled: true, kp: 0.1, maxStep: 0.05, deadBand: 0.01 } });
  return T.state.control.balanceMax === 0.2 && Array.isArray(T.state.control.map) && T.state.control.map.length === 0;
})());

check("un archivo viejo con control.tare se lee y no se vuelve a escribir", (() => {
  reset(); T.loadParsed({ schemaVersion: 1, control: { tare: { samples: 4, maxSpread: 10, maxAmbient: 300 } } });
  const j = JSON.parse(JSON.stringify(T.buildJson()));
  return j.control.tare === undefined && T.state.control.tare === undefined;
})());

console.log(fails === 0 ? "\nTODO OK\n" : "\n" + fails + " FALLA(S)\n");
process.exit(fails ? 1 : 0);
