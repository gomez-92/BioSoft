// Prueba una URI de MongoDB antes de desplegar.
//
//   node tools/probar-mongo.js "mongodb+srv://usuario:clave@host/biosoft"
//
// Existe porque descubrir que la URI esta mal DESPUES de desplegar cuesta un
// ciclo entero de build y deploy, y el error que se ve alla (un timeout de
// `users.findOne()`) no dice cual de las cuatro cosas fallo. Aca se prueban
// las cuatro por separado y cada una dice que hacer:
//
//   1. el hostname resuelve           -> si no, el cluster no es ese
//   2. se puede conectar               -> si no, falta 0.0.0.0/0 en Atlas
//   3. la credencial es valida         -> si no, usuario o contraseña
//   4. se puede ESCRIBIR               -> si no, el usuario es de solo lectura
//
// La cuarta es la que mas engaña: con un usuario de solo lectura el monitor
// arranca, se conecta, no da ningun error visible, y no guarda nada.

const dns = require('node:dns').promises;

const uri = process.argv[2];
if (!uri) {
  console.error('uso: node tools/probar-mongo.js "<uri>"');
  process.exit(1);
}

// Se oculta la contraseña en todo lo que se imprime: este script se corre en
// una terminal que queda en el historial y a veces se pega en un chat.
const visible = uri.replace(/\/\/([^:]+):([^@]+)@/, '//$1:****@');

function host(u) {
  const m = /^mongodb(\+srv)?:\/\/[^@]+@([^/?]+)/.exec(u);
  return m ? m[2].split(',')[0].split(':')[0] : null;
}

function baseDeDatos(u) {
  const resto = u.replace(/^mongodb(\+srv)?:\/\//, '');
  const barra = resto.indexOf('/');
  return barra < 0 ? '' : resto.slice(barra + 1).split('?')[0];
}

(async () => {
  console.log(`\nProbando: ${visible}\n`);

  /* 1. DNS */
  const h = host(uri);
  if (!h) { console.error('X  La URI no tiene la forma esperada.'); process.exit(1); }

  const esSrv = uri.startsWith('mongodb+srv://');
  try {
    if (esSrv) await dns.resolveSrv('_mongodb._tcp.' + h);
    else await dns.resolve4(h);
    console.log(`1. DNS         OK   ${h} resuelve`);
  } catch (error) {
    console.error(`1. DNS         FALLA  ${h} no resuelve (${error.code})`);
    console.error('   El hostname del cluster esta mal, o ese cluster ya no existe.');
    console.error('   Copialo de nuevo desde Atlas: Database > Connect > Drivers.');
    process.exit(1);
  }

  /* 2-4. conexion, credencial y escritura */
  const nombre = baseDeDatos(uri);
  if (!nombre) {
    console.error('   AVISO: la URI no nombra una base. Mongo va a usar "test" y el');
    console.error('   historico se va a ver vacio. Agregale /biosoft antes del "?".');
  }

  const mongoose = require('mongoose');
  try {
    await mongoose.connect(uri, { serverSelectionTimeoutMS: 8000 });
    console.log('2. Conexion    OK   el servidor responde');
    console.log('3. Credencial  OK   usuario y contraseña aceptados');
  } catch (error) {
    const mensaje = String(error.message);
    if (/Authentication failed|bad auth/i.test(mensaje)) {
      console.error('3. Credencial  FALLA  usuario o contraseña incorrectos');
      console.error('   Si la contraseña tiene @ / : # o ?, hay que escaparlos (@ -> %40).');
    } else {
      console.error(`2. Conexion    FALLA  ${mensaje}`);
      console.error('   Lo mas probable: falta 0.0.0.0/0 en Network Access de Atlas.');
    }
    process.exit(1);
  }

  try {
    const prueba = mongoose.connection.collection('__prueba_de_escritura');
    await prueba.insertOne({ ts: new Date() });
    await prueba.drop();
    console.log(`4. Escritura   OK   se puede escribir en "${nombre}"`);
  } catch (error) {
    console.error(`4. Escritura   FALLA  ${error.message}`);
    console.error('   El usuario no tiene permiso de escritura. En Atlas, Database');
    console.error('   Access > Edit > "Read and write to any database".');
    await mongoose.disconnect();
    process.exit(1);
  }

  await mongoose.disconnect();
  console.log('\nTodo bien: esta URI sirve para desplegar.\n');
})();
