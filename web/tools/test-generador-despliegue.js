// Verificacion de web/tools/generador-despliegue.html.
//
//   node web/tools/test-generador-despliegue.js
//
// Mismo enfoque que tools/test-generador.js: extrae el <script> del HTML y lo
// corre en node con un DOM minimo. Lo que se fija aca son las validaciones,
// porque cada una atrapa un error que de otro modo aparece RECIEN DESPLEGADO,
// cuando el servicio no levanta o levanta y no recibe nada.
//
// No necesita instalar nada: solo node.
const fs = require("fs");
const vm = require("vm");
const path = require("path");

const html = fs.readFileSync(path.join(__dirname, "generador-despliegue.html"), "utf8");
const code = html.match(/<script>([\s\S]*)<\/script>/)[1];

const nodes = {};
function fakeEl(id){
  return { id, innerHTML:"", textContent:"", value:"", checked:false, style:{},
           disabled:false, addEventListener(){}, click(){} };
}
const document = {
  getElementById: id => nodes[id] || (nodes[id] = fakeEl(id)),
  addEventListener(){},
  createElement: () => fakeEl("tmp")
};

const ctx = {
  document, console, JSON, Math, RegExp, Object, Array, String, Number, Boolean,
  Uint8Array, crypto: { getRandomValues: a => { a.fill(7); return a; } },
  navigator: {}, Blob: class {}, URL: { createObjectURL: () => "", revokeObjectURL(){} }
};
vm.createContext(ctx);
vm.runInContext(code + `
var __t = {
  get errors(){ return errors; },
  get warns(){ return warns; },
  validate: validate, envFile: envFile, comandos: comandos
};`, ctx);
const T = ctx.__t;

/* ------------------------------ harness ------------------------------ */

let fallos = 0, total = 0;

function check(nombre, condicion, detalle){
  total++;
  if(condicion){ console.log("  ok   " + nombre); return; }
  fallos++;
  console.log("  FALLA " + nombre + (detalle ? "\n         " + detalle : ""));
}

// Una configuracion completa y correcta, de la que parte cada caso.
function base(extra){
  return Object.assign({
    plataforma: "fly",
    app: "biosoft-monitor",
    region: "eze",
    mongo: "mongodb+srv://backend:Cl4veLarga@cluster.ab12c.mongodb.net/biosoft",
    mqtt: "mqtts://abc123.ala.sa-east-1.emqxsl.com:8883",
    mqttUser: "backend",
    mqttPass: "clave-del-broker",
    clientId: "biosoft-backend-prod",
    clientIdPlaca: "biosoft-esp32",
    ca: "",
    jwt: "a".repeat(96),
    seedUser: "mario",
    seedPass: "una-clave-larga",
    deviceId: "biosoft-01",
    confirmaClientId: true
  }, extra || {});
}

function errorDe(extra){
  T.validate(base(extra));
  return T.errors.join(" | ");
}

/* ------------------------------- casos ------------------------------- */

console.log("\nconfiguracion completa");
check("una configuracion correcta pasa", T.validate(base()), T.errors.join(" | "));
check("y no deja avisos", T.warns.length === 0, T.warns.join(" | "));

console.log("\nURI de Atlas");
// El error de copiado mas comun del panel de Atlas. Falla al conectar, ya
// desplegado, con un mensaje de autenticacion que no menciona el marcador.
check("rechaza el marcador <password> sin reemplazar",
  /marcador/.test(errorDe({ mongo: "mongodb+srv://backend:<password>@c.ab12c.mongodb.net/biosoft" })));
// Sin nombre de base, Mongo usa "test": el monitor escribiria en otra base y
// el historico aparece vacio sin ningun error.
check("exige el nombre de la base al final",
  /nombre de la base/.test(errorDe({ mongo: "mongodb+srv://u:p@c.ab12c.mongodb.net" })));
// Una @ sin escapar parte la URI en el lugar equivocado.
check("detecta un @ sin escapar en la contraseña",
  /@ sin escapar/.test(errorDe({ mongo: "mongodb+srv://u:cl@ve@c.ab12c.mongodb.net/biosoft" })));
check("exige el esquema mongodb",
  /mongodb\+srv/.test(errorDe({ mongo: "https://cluster.mongodb.net/biosoft" })));
check("avisa si la base no se llama biosoft",
  (T.validate(base({ mongo: "mongodb+srv://u:p@c.ab12c.mongodb.net/otra" })),
   T.warns.some(w => /biosoft/.test(w))));

console.log("\nbroker");
// El servidor se niega a arrancar con NODE_ENV=production y un broker sin TLS:
// esto seria un despliegue que directamente no levanta.
check("exige TLS en el broker",
  /mqtts/.test(errorDe({ mqtt: "mqtt://abc123.emqxsl.com:1883" })));
// LA trampa conocida del proyecto: dos clientes con el mismo clientId se
// desconectan mutuamente, y desde afuera parece que el broker anda mal.
check("rechaza el mismo Client ID que la placa",
  /desconectar mutuamente/.test(errorDe({ clientId: "biosoft-esp32", clientIdPlaca: "biosoft-esp32" })));
check("exige confirmar que el Client ID es distinto",
  /confirmar/.test(errorDe({ confirmaClientId: false })));
check("rechaza un certificado que no es PEM",
  /PEM/.test(errorDe({ ca: "el certificado esta en mi escritorio" })));

console.log("\nsesiones");
// Un JWT_SECRET corto hace que el servidor se niegue a arrancar en produccion.
check("rechaza un JWT_SECRET corto",
  /32 caracteres/.test(errorDe({ jwt: "corto" })));
check("rechaza una contraseña inicial corta",
  /8 caracteres/.test(errorDe({ seedPass: "1234" })));
check("avisa si la contraseña del monitor es la del broker",
  (T.validate(base({ seedPass: "clave-del-broker" })),
   T.warns.some(w => /se filtran las dos/.test(w))));

console.log("\navisos por plataforma");
// El plan gratuito de Render duerme, y este servicio no puede dormir: es el
// unico cliente MQTT que escucha la placa.
check("avisa que el plan gratuito de Render duerme",
  (T.validate(base({ plataforma: "render" })),
   T.warns.some(w => /duerme/.test(w))));

console.log("\narchivo de variables");
const env = T.envFile(base());
check("incluye NODE_ENV=production", /^NODE_ENV=production$/m.test(env));
check("incluye la URI de Atlas", /^MONGO_URI=mongodb\+srv:\/\//m.test(env));
check("incluye el JWT_SECRET", new RegExp("^JWT_SECRET=a{96}$", "m").test(env));
check("incluye el usuario inicial", /^SEED_USER=mario$/m.test(env));
// Un .env no admite valores multilinea: el PEM va con \n literales, que es
// como Node lo recibe y como lo usa el ingestor.
const envCa = T.envFile(base({ ca: "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----" }));
check("aplana el PEM del certificado a una sola linea",
  /^MQTT_CA=-----BEGIN CERTIFICATE-----\\nMIIB\\n-----END CERTIFICATE-----$/m.test(envCa),
  envCa.split("\n").filter(l => l.startsWith("MQTT_CA"))[0]);
check("sin certificado no escribe MQTT_CA", !/MQTT_CA/.test(env));

console.log("\ncomandos");
const cmdFly = T.comandos(base());
// Los secretos se importan DESDE UN ARCHIVO: una contraseña con $ o ! la
// rompe el shell antes de que llegue a fly, sin dar error.
check("fly importa los secretos desde el archivo", /fly secrets import < biosoft\.env/.test(cmdFly));
// Solo las lineas EJECUTABLES: el bloque explica por que no se usa
// `fly secrets set`, asi que el texto aparece -- pero como comentario.
const ejecutables = cmdFly.split("\n").filter(l => l.trim() && !l.trim().startsWith("#"));
check("ninguna linea ejecutable arma un secrets set con los valores adentro",
  !ejecutables.some(l => /fly secrets set/.test(l)), ejecutables.join(" ; "));
check("usa el nombre y la region elegidos", /--name biosoft-monitor --region eze/.test(cmdFly));
const cmdRender = T.comandos(base({ plataforma: "render" }));
check("render no usa la CLI", !/fly /.test(cmdRender));
check("render menciona el Root Directory", /Root Directory/.test(cmdRender));

/* ------------------------------ resumen ------------------------------ */

console.log("\n" + (fallos === 0
  ? `TODO BIEN (${total} comprobaciones)`
  : `${fallos} FALLAS de ${total} comprobaciones`));
process.exit(fallos === 0 ? 0 : 1);
