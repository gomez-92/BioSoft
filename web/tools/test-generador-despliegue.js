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
  validate: validate, envFile: envFile, comandos: comandos,
  pasosPrevios: pasosPrevios, diagnostico: diagnostico
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

// EL FORMATO IMPORTA. `flyctl secrets import` rechaza el archivo ENTERO si
// encuentra una linea que no sea NOMBRE=VALOR: falla con "Secrets must be
// provided as NAME=VALUE pairs" y no carga ninguna variable. Un encabezado de
// comentario alcanzaba para romperlo, y ya rompio una vez.
const lineasEnv = env.split("\n").filter(l => l !== "");
check("todas las lineas son NOMBRE=VALOR",
  lineasEnv.every(l => /^[A-Z_]+=/.test(l)),
  lineasEnv.filter(l => !/^[A-Z_]+=/.test(l)).join(" | "));
check("no hay comentarios", !lineasEnv.some(l => l.trim().startsWith("#")));
check("no hay lineas en blanco en el medio", !/\n\n/.test(env));
// Un BOM al principio hace que la primera linea no matchee y se pierda la
// primera variable, o falle el import entero.
check("no empieza con BOM", env.charCodeAt(0) !== 0xFEFF);
check("el archivo con certificado tambien es valido",
  envCa.split("\n").filter(l => l !== "").every(l => /^[A-Z_]+=/.test(l)));

console.log("\ncomandos");
const cmdFly = T.comandos(base());
// Los secretos se importan DESDE UN ARCHIVO: una contraseña con $ o ! la
// rompe el shell antes de que llegue a fly, sin dar error.
// PowerShell no soporta `<` para redirigir ("El operador '<' esta reservado
// para uso futuro"), y esta es una maquina Windows.
check("da el comando de PowerShell para importar",
  /Get-Content biosoft\.env \| flyctl secrets import/.test(cmdFly));
check("y menciona la variante de bash", /flyctl secrets import < biosoft\.env/.test(cmdFly));
// Solo las lineas EJECUTABLES: el bloque explica por que no se usa
// `fly secrets set`, asi que el texto aparece -- pero como comentario.
const ejecutables = cmdFly.split("\n").filter(l => l.trim() && !l.trim().startsWith("#"));
check("ninguna linea ejecutable arma un secrets set con los valores adentro",
  !ejecutables.some(l => /fly secrets set/.test(l)), ejecutables.join(" ; "));
check("usa el nombre y la region elegidos", /--name biosoft-monitor --region eze/.test(cmdFly));
const cmdRender = T.comandos(base({ plataforma: "render" }));
check("render no usa la CLI", !/fly /.test(cmdRender));
check("render menciona el Root Directory", /Root Directory/.test(cmdRender));


// Cada una de estas fija algo que costo un ciclo de despliegue averiguar.
console.log("\nlecciones del despliegue real");

// `flyctl launch` reescribe fly.toml con sus defaults y pisa
// auto_stop_machines=false y min_machines_running=1: la maquina se apaga sola
// y el monitor deja de escuchar. Paso, y no hay nada que lo avise.
check("avisa que fly launch pisa el fly.toml",
  /REESCRIBE fly\.toml/.test(cmdFly) && /git checkout fly\.toml/.test(cmdFly));

// El instalador de winget deja el comando como `flyctl`, no `fly`.
check("usa flyctl y no fly",
  /flyctl launch/.test(cmdFly) && !/^fly /m.test(cmdFly));

// Un ping que devuelve 200 no dice nada: el servidor responde igual con los
// dos enlaces caidos. Lo que hay que mirar son los booleanos.
check("la verificacion mira mongo y mqtt, no solo el 200",
  /mongo.*:true/.test(cmdFly) && /mqtt.*:true/.test(cmdFly));

check("manda a los logs cuando algo falla", /flyctl logs/.test(cmdFly));

const pasos = T.pasosPrevios(base()).join(" ");
// Probar la URI antes de desplegar es lo que convierte media hora en tres
// segundos.
check("el primer paso es probar la URI de Atlas", /probar-mongo\.js/.test(pasos));
// El permiso de escritura es el que mas engaña: sin el, el monitor arranca, no
// da error y no guarda nada.
check("menciona el permiso de escritura", /ESCRITURA/.test(pasos));
// Confundir la cuenta de Atlas con el usuario de base frena a cualquiera.
check("aclara que el usuario de base no es la cuenta de Atlas",
  /NO es tu cuenta de/.test(pasos));
// Con publicacion denegada, el simulador NO puede usar la credencial del
// backend -- y el sintoma es que no llega nada, sin ningun error.
check("avisa que el simulador necesita otra credencial",
  /el simulador necesita otra/.test(pasos));
check("advierte que Fly pide tarjeta",
  /pide tarjeta/.test(T.pasosPrevios(base({ plataforma: "fly" })).join(" ")));
check("advierte que el Free de Render duerme",
  /duerme a los 15 minutos/.test(T.pasosPrevios(base({ plataforma: "render" })).join(" ")));

// La tabla de diagnostico traduce sintoma -> causa. El sintoma que se ve (la
// pagina no actualiza) nunca nombra la causa.
const diag = T.diagnostico();
check("la tabla de diagnostico cubre los fallos que ocurrieron",
  diag.length >= 8
  && diag.some(f => /ENOTFOUND/.test(f[0]))
  && diag.some(f => /bad auth/.test(f[0]))
  && diag.some(f => /local issuer certificate/.test(f[0]))
  && diag.some(f => /Not authorized/.test(f[0])),
  "filas: " + diag.length);
check("cada fila dice que hacer, no solo que paso",
  diag.every(f => f[1] && f[1].length > 40));

/* ------------------------------ resumen ------------------------------ */

console.log("\n" + (fallos === 0
  ? `TODO BIEN (${total} comprobaciones)`
  : `${fallos} FALLAS de ${total} comprobaciones`));
process.exit(fallos === 0 ? 0 : 1);
