// BioSoft - Banco de pruebas del modulo SD (ESP32)
//
// Ejercita esp32/src/sdstorage.hpp contra una tarjeta real y reporta el
// resultado por el monitor serie. NO es firmware del sistema: no levanta
// pantalla, ni WiFi, ni el enlace serie con el Mega.
//
// Los archivos de prueba se crean en la RAIZ de la tarjeta con prefijo
// "bstest_" y se borran al terminar. SdStorage no expone mkdir, asi que no
// se puede usar un subdirectorio propio. NO toca /biosoft/config.json.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "sdstorage.hpp"

// Pines de la ESP32-2432S028 (CYD): la SD va por VSPI (SCK=18, MISO=19,
// MOSI=23, CS=5). NO comparte el bus con el TFT, que va por HSPI en
// 14/12/13 -- con los pines del TFT la tarjeta nunca contesta y el core
// reporta "sdSelectCard(): Select Failed" antes de intentar montar.
static SdStorage almacenamiento(5, 18, 19, 23);

static const char* PathBasico   = "/bstest_a.txt";
static const char* PathAnexado  = "/bstest_b.txt";
static const char* PathVacio    = "/bstest_c.txt";
static const char* PathGrande   = "/bstest_d.txt";
static const char* PathInexiste = "/bstest_no_existe.txt";

// El archivo grande usa el mismo tamanio que el buffer real con el que se
// lee la configuracion en el firmware (8 KB): es el caso que mas se parece
// al uso de produccion.
static const size_t TamanioGrande = 8192;

// Estaticos, no en el stack: loopTask de Arduino tiene 8192 bytes en total
// y estos dos buffers solos ya lo desbordarian.
static char bufferGrandeEscritura[TamanioGrande + 1];
static char bufferGrandeLectura[TamanioGrande + 1];
static char bufferLectura[256];

static uint16_t _ok = 0;
static uint16_t _fallas = 0;

// ---------------------------------------------------------------- helpers

static void titulo(const char* texto) {
  Serial.println();
  Serial.print(F("=== "));
  Serial.print(texto);
  Serial.println(F(" ==="));
}

static void check(const char* nombre, bool condicion) {
  Serial.print(condicion ? F("[OK]    ") : F("[FALLA] "));
  Serial.println(nombre);
  if (condicion) _ok++; else _fallas++;
}

static void checkTexto(const char* nombre, const char* esperado, const char* obtenido) {
  bool ok = (obtenido != nullptr) && (strcmp(esperado, obtenido) == 0);
  check(nombre, ok);
  if (!ok) {
    Serial.print(F("        esperado: "));
    Serial.println(esperado);
    Serial.print(F("        obtenido: "));
    Serial.println(obtenido != nullptr ? obtenido : "(null)");
  }
}

static void checkNumero(const char* nombre, unsigned long esperado, unsigned long obtenido) {
  bool ok = (esperado == obtenido);
  check(nombre, ok);
  if (!ok) {
    Serial.print(F("        esperado: "));
    Serial.print(esperado);
    Serial.print(F(" / obtenido: "));
    Serial.println(obtenido);
  }
}

// Aviso antes de cada prueba cuyo caso exitoso ES que falle: sdstorage.hpp
// tiene DEBUG_SDSTORAGE en true y va a imprimir su propia linea
// "[SD] ERROR: ...". Sin este aviso, ese error parece un problema del banco.
static void esperandoErrorDelModulo() {
  Serial.println(F("        (la linea [SD] ERROR de abajo es la esperada)"));
}

// ------------------------------------------------------- info de hardware

static void infoTarjeta() {
  titulo("Informacion de la tarjeta");

  uint8_t tipo = SD.cardType();
  const char* nombreTipo = "desconocido";
  switch (tipo) {
    case CARD_NONE: nombreTipo = "ninguna"; break;
    case CARD_MMC:  nombreTipo = "MMC";     break;
    case CARD_SD:   nombreTipo = "SDSC";    break;
    case CARD_SDHC: nombreTipo = "SDHC";    break;
    default: break;
  }

  Serial.print(F("Tipo       : "));
  Serial.println(nombreTipo);
  Serial.printf("Tamanio    : %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  Serial.printf("Total (FS) : %llu MB\n", SD.totalBytes() / (1024ULL * 1024ULL));
  Serial.printf("Usado (FS) : %llu MB\n", SD.usedBytes() / (1024ULL * 1024ULL));
}

static void diagnosticoDeMontaje() {
  Serial.println();
  Serial.println(F("No se pudo montar la tarjeta. Cosas a revisar, en orden:"));
  Serial.println(F("  1. CS en GPIO 5, y el bus en SCK=18 / MISO=19 / MOSI=23 (VSPI)."));
  Serial.println(F("  2. Tarjeta formateada en FAT32 (no exFAT, no NTFS)."));
  Serial.println(F("  3. Tarjeta de 32 GB o menos."));
  Serial.println(F("  4. Que este bien insertada y haga contacto."));
  Serial.println(F("  5. Alimentacion: el modulo tira picos de corriente al montar."));
  Serial.println();
  Serial.println(F("El resto de las pruebas no se corre: sin tarjeta montada no"));
  Serial.println(F("prueban nada util, solo repetirian este mismo fallo."));
}

// --------------------------------------------------------------- pruebas

static void pruebaEscrituraYLectura() {
  titulo("Escritura y lectura basica");

  const char* contenido = "BioSoft SD test";
  check("writeFile() escribe un archivo nuevo",
        almacenamiento.writeFile(PathBasico, contenido));

  check("exists() encuentra el archivo recien escrito",
        almacenamiento.exists(PathBasico));

  size_t largo = 0;
  bool leido = almacenamiento.readFile(PathBasico, bufferLectura, sizeof(bufferLectura), &largo);
  check("readFile() devuelve true con un buffer holgado", leido);
  checkTexto("readFile() devuelve el contenido exacto", contenido, leido ? bufferLectura : nullptr);
  checkNumero("readFile() reporta la longitud exacta", strlen(contenido), largo);
}

static void pruebaTruncadoAlReescribir() {
  titulo("Reescritura (writeFile trunca, no anexa)");

  // sdstorage.hpp asume que FILE_WRITE trunca (semantica "w" del core
  // arduino-esp32 3.x) y lo deja anotado como suposicion sin verificar.
  // Esta es la prueba que la verifica: si el core resultara ser
  // append-only, un config.json reescrito quedaria con la cola del
  // anterior pegada al final y el JSON dejaria de parsear.
  const char* largoTexto = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  const char* cortoTexto = "corto";

  check("writeFile() escribe el contenido largo",
        almacenamiento.writeFile(PathBasico, largoTexto));
  check("writeFile() reescribe con el contenido corto",
        almacenamiento.writeFile(PathBasico, cortoTexto));

  size_t largo = 0;
  bool leido = almacenamiento.readFile(PathBasico, bufferLectura, sizeof(bufferLectura), &largo);
  checkTexto("la reescritura NO dejo cola del contenido anterior",
             cortoTexto, leido ? bufferLectura : nullptr);
  checkNumero("el tamanio quedo en el del contenido corto", strlen(cortoTexto), largo);
}

static void pruebaAnexado() {
  titulo("Anexado");

  check("writeFile() crea el archivo base",
        almacenamiento.writeFile(PathAnexado, "uno"));
  check("appendFile() agrega al final",
        almacenamiento.appendFile(PathAnexado, "dos"));

  bool leido = almacenamiento.readFile(PathAnexado, bufferLectura, sizeof(bufferLectura));
  checkTexto("el contenido es base + agregado", "unodos", leido ? bufferLectura : nullptr);

  check("appendFile() crea el archivo si no existe",
        almacenamiento.appendFile(PathInexiste, "creado"));
  leido = almacenamiento.readFile(PathInexiste, bufferLectura, sizeof(bufferLectura));
  checkTexto("el archivo creado por appendFile tiene el contenido", "creado",
             leido ? bufferLectura : nullptr);
  almacenamiento.remove(PathInexiste);
}

static void pruebaLimiteDeBuffer() {
  titulo("Limite del buffer (no truncar en silencio)");

  // El motivo de existir del `return false` de readFile: quien lee un
  // archivo de configuracion no puede distinguir "JSON invalido" de "JSON
  // cortado por buffer chico" si esto devolviera true.
  const char* contenido = "0123456789";  // 10 bytes
  const size_t largoContenido = strlen(contenido);

  check("writeFile() prepara el archivo del caso limite",
        almacenamiento.writeFile(PathBasico, contenido));

  // Buffer justo: 10 bytes de contenido + 1 del terminador.
  memset(bufferLectura, 0, sizeof(bufferLectura));
  bool justo = almacenamiento.readFile(PathBasico, bufferLectura, largoContenido + 1);
  check("readFile() acepta un buffer que entra JUSTO (len + 1)", justo);
  checkTexto("y devuelve el contenido completo", contenido, justo ? bufferLectura : nullptr);

  // Un byte menos: no hay lugar para el terminador, tiene que rechazar.
  Serial.println(F("Caso: buffer un byte mas chico de lo necesario"));
  esperandoErrorDelModulo();
  bool cortito = almacenamiento.readFile(PathBasico, bufferLectura, largoContenido);
  check("readFile() RECHAZA un buffer un byte mas chico (len)", !cortito);

  check("readFile() rechaza maxLength = 0",
        !almacenamiento.readFile(PathBasico, bufferLectura, 0));

  check("readFile() rechaza un buffer nulo",
        !almacenamiento.readFile(PathBasico, nullptr, sizeof(bufferLectura)));
}

static void pruebaArchivoInexistente() {
  titulo("Archivo inexistente");

  almacenamiento.remove(PathInexiste);  // por si quedo de una corrida previa

  check("exists() devuelve false para un archivo que no esta",
        !almacenamiento.exists(PathInexiste));
  check("readFile() devuelve false para un archivo que no esta",
        !almacenamiento.readFile(PathInexiste, bufferLectura, sizeof(bufferLectura)));
  check("remove() devuelve false para un archivo que no esta",
        !almacenamiento.remove(PathInexiste));
}

static void pruebaBorrado() {
  titulo("Borrado");

  check("writeFile() prepara el archivo a borrar",
        almacenamiento.writeFile(PathBasico, "para borrar"));
  check("remove() borra un archivo existente",
        almacenamiento.remove(PathBasico));
  check("exists() ya no lo encuentra despues de borrarlo",
        !almacenamiento.exists(PathBasico));
}

static void pruebaArchivoVacio() {
  titulo("Archivo vacio");

  check("writeFile() escribe contenido vacio",
        almacenamiento.writeFile(PathVacio, ""));
  check("exists() encuentra el archivo vacio",
        almacenamiento.exists(PathVacio));

  size_t largo = 123;  // valor sucio a proposito: readFile tiene que pisarlo
  memset(bufferLectura, 'X', sizeof(bufferLectura));
  bool leido = almacenamiento.readFile(PathVacio, bufferLectura, sizeof(bufferLectura), &largo);
  check("readFile() devuelve true para un archivo de 0 bytes", leido);
  checkNumero("readFile() reporta longitud 0", 0, largo);
  checkTexto("el buffer queda como cadena vacia", "", leido ? bufferLectura : nullptr);
}

static void pruebaArchivoGrande() {
  titulo("Archivo grande (8 KB, el tamanio del buffer de config)");

  // Patron no uniforme: si la SD devolviera bloques desordenados o
  // repetidos, un relleno de un solo caracter no lo mostraria.
  for (size_t i = 0; i < TamanioGrande; i++) {
    bufferGrandeEscritura[i] = static_cast<char>('A' + (i % 26));
  }
  bufferGrandeEscritura[TamanioGrande] = '\0';

  unsigned long inicio = millis();
  bool escrito = almacenamiento.writeFile(PathGrande, bufferGrandeEscritura);
  unsigned long msEscritura = millis() - inicio;
  check("writeFile() escribe 8192 bytes completos", escrito);

  size_t largo = 0;
  memset(bufferGrandeLectura, 0, sizeof(bufferGrandeLectura));
  inicio = millis();
  bool leido = almacenamiento.readFile(PathGrande, bufferGrandeLectura,
                                       sizeof(bufferGrandeLectura), &largo);
  unsigned long msLectura = millis() - inicio;

  check("readFile() lee los 8192 bytes", leido);
  checkNumero("la longitud leida coincide", TamanioGrande, largo);
  check("el contenido leido es identico al escrito",
        leido && memcmp(bufferGrandeEscritura, bufferGrandeLectura, TamanioGrande) == 0);

  Serial.print(F("        escritura: "));
  Serial.print(msEscritura);
  Serial.print(F(" ms / lectura: "));
  Serial.print(msLectura);
  Serial.println(F(" ms"));

  // Y el mismo archivo contra un buffer de 8 KB justos: 8192 bytes no
  // entran en 8192 porque falta el lugar del terminador.
  Serial.println(F("Caso: 8192 bytes contra un buffer de 8192"));
  esperandoErrorDelModulo();
  check("readFile() rechaza el archivo de 8 KB en un buffer de 8 KB justos",
        !almacenamiento.readFile(PathGrande, bufferGrandeLectura, TamanioGrande));
}

static void limpieza() {
  titulo("Limpieza");

  almacenamiento.remove(PathBasico);
  almacenamiento.remove(PathAnexado);
  almacenamiento.remove(PathVacio);
  almacenamiento.remove(PathGrande);
  almacenamiento.remove(PathInexiste);

  bool limpio = !almacenamiento.exists(PathBasico)
             && !almacenamiento.exists(PathAnexado)
             && !almacenamiento.exists(PathVacio)
             && !almacenamiento.exists(PathGrande)
             && !almacenamiento.exists(PathInexiste);
  check("no quedan archivos de prueba en la tarjeta", limpio);
}

static void resumen() {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.print(F("  RESULTADO: "));
  Serial.print(_ok);
  Serial.print(F(" de "));
  Serial.print(_ok + _fallas);
  Serial.println(F(" pruebas OK"));
  if (_fallas > 0) {
    Serial.print(F("  FALLARON: "));
    Serial.println(_fallas);
  }
  Serial.println(F("========================================"));
  Serial.println();
  Serial.println(F("Enter en el monitor para volver a correr."));
}

static void correrTodo() {
  _ok = 0;
  _fallas = 0;

  Serial.println();
  Serial.println(F("##########################################"));
  Serial.println(F("  BioSoft - Banco de pruebas del modulo SD"));
  Serial.println(F("##########################################"));

  titulo("Montaje");
  if (!almacenamiento.begin()) {
    check("begin() monta la tarjeta", false);
    diagnosticoDeMontaje();
    resumen();
    return;
  }
  check("begin() monta la tarjeta", true);
  check("isReady() queda en true despues de un begin() exitoso",
        almacenamiento.isReady());

  infoTarjeta();

  pruebaEscrituraYLectura();
  pruebaTruncadoAlReescribir();
  pruebaAnexado();
  pruebaLimiteDeBuffer();
  pruebaArchivoInexistente();
  pruebaBorrado();
  pruebaArchivoVacio();
  pruebaArchivoGrande();
  limpieza();

  resumen();
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // margen para abrir el monitor y no perderse el arranque
  correrTodo();
}

void loop() {
  // Re-correr con Enter: en el banco uno prueba sacar y poner la tarjeta,
  // cambiar el cableado o probar otra SD sin tener que resetear la placa.
  if (Serial.available() > 0) {
    while (Serial.available() > 0) Serial.read();
    correrTodo();
  }
  delay(50);
}
