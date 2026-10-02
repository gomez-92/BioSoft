#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "timer.hpp"
#include "seriallink.hpp"
#include "magnetometermanager.hpp"
#include "magnetometermlx90993.hpp"
#include "magnetometervoltagesim.hpp"
#include "thermometermanager.hpp"
#include "thermometerds18b20.hpp"
#include "thermometervoltagesim.hpp"
#include "scenariosensors.hpp"
#include "currentmanager.hpp"
#include "currentsensorsct013.hpp"
#include "mainpowerswitch.hpp"
#include "intervals.hpp"
#include "fieldcontroller.hpp"
#include "pwmdriver.hpp"
#include "coilchannel.hpp"
#include "signalgenerator.hpp"
#include "coilexcitation.hpp"
#include "detector.hpp"
#include "emergencybutton.hpp"
#include "engine.hpp"
#include "debugconfig.hpp"
#include <avr/wdt.h>

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro). Apagado (2026-09-01) durante bring-up INT-001
// centrado en el circuito de campo magnetico -- reset cause/RAM no aportan
// a esa depuracion. Volver a true si hace falta diagnosticar boot/memoria.
constexpr bool DEBUG_MAIN = false;
// Listado de los DS18B20 del bus al arrancar: encendido aunque DEBUG_MAIN no
// lo este, porque es la forma de leer la direccion de un sensor nuevo.
constexpr bool DEBUG_ONEWIRE = true;

void printResetCause() {

  uint8_t resetCause = MCUSR;

  MCUSR = 0;

  DEBUG_PRINT(DEBUG_MAIN, F("[RESET] MCUSR = 0x"));
  DEBUG_PRINTLN(DEBUG_MAIN, resetCause, HEX);

  if (resetCause & _BV(PORF)) {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[RESET] Power-on reset"));
  }

  if (resetCause & _BV(EXTRF)) {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[RESET] External reset"));
  }

  if (resetCause & _BV(BORF)) {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[RESET] Brown-out reset"));
  }

  if (resetCause & _BV(WDRF)) {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[RESET] WATCHDOG reset"));
  }

  if (resetCause & _BV(JTRF)) {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[RESET] JTAG reset"));
  }
}

// Diagnostico temporal bring-up INT-001 (2026-09-01): MagnetometerMlx90393::begin()
// viene fallando (begin_I2C addr=0xC -> FALLO) con el sensor recien cableado --
// este scan barre las 127 direcciones I2C posibles y loguea cuales responden,
// para distinguir "el bus esta vivo pero el sensor esta en otra direccion"
// de "el sensor no esta en el bus para nada" (cableado/alimentacion). Gateado
// con el flag del magnetometro (no DEBUG_MAIN) para que quede visible durante
// este bring-up sin tener que reactivar el ruido general del modulo main.
void scanI2CBus() {
  DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F("[I2C] Escaneando bus..."));
  uint8_t found = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F("[I2C] Dispositivo encontrado en 0x"));
      DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, address, HEX);
      found++;
    }
  }
  if (found == 0) {
    DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F("[I2C] Ningun dispositivo respondio -- bus muerto (cableado/alimentacion/pull-ups), no es un problema de direccion"));
  } else {
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, F("[I2C] Escaneo completo, "));
    DEBUG_PRINT(DEBUG_MAGNETOMETER_MLX90393, found);
    DEBUG_PRINTLN(DEBUG_MAGNETOMETER_MLX90393, F(" dispositivo(s) encontrado(s)"));
  }
}

// Lista los DS18B20 del bus con su direccion, en el formato que va en la SD
// (detector.sources[TEMP1].address). Es la forma de averiguar la de un
// sensor nuevo: conectarlo, arrancar y copiarla de aca.
void printOneWireDevices(DallasTemperature& dallas) {
  uint8_t count = dallas.getDeviceCount();
  DEBUG_PRINT(DEBUG_ONEWIRE, F("[ONEWIRE] DS18B20 en el bus: "));
  DEBUG_PRINTLN(DEBUG_ONEWIRE, count);
  DeviceAddress address;
  char text[OneWireAddress::TextLength];
  for (uint8_t i = 0; i < count; i++) {
    if (!dallas.getAddress(address, i)) continue;
    OneWireAddress::format(address, text);
    DEBUG_PRINT(DEBUG_ONEWIRE, F("[ONEWIRE]   "));
    DEBUG_PRINTLN(DEBUG_ONEWIRE, text);
  }
}

int freeMemory() {
    extern int __heap_start, *__brkval;
    int v;
    return (int) &v - (__brkval == 0
    ? (int) &__heap_start
    : (int) __brkval);
}


/* ===== definiciones =====*/
#define WIRE_PIN 2
#define RELE1_PIN 3
#define EMERGENCY_BUTTON_PIN 4
#define TEMP1_ADDRESS { 0x28, 0x3F, 0xE5, 0x57, 0x04, 0xE1, 0x3D, 0xED }
// Un canal PWM por bobina. 44/45/46 son Timer5 y 6 es Timer4: los dos timers
// se configuran a la misma frecuencia, asi que el reparto entre ellos es
// transparente. Timer3 (2,3,5) no esta disponible -- OneWire, rele y SPI CS --
// y Timer0/Timer2 los excluye PwmDriver a proposito (ver pwmdriver.hpp).
#define COIL1_PWM_PIN 44
#define COIL2_PWM_PIN 45
#define COIL3_PWM_PIN 46
#define COIL4_PWM_PIN 6   // Timer4; confirmar contra el ruteo real de la placa

// Habilitacion individual por bobina: pines digitales sin timer, para no
// gastar los que sirven para PWM.
#define COIL1_ENABLE_PIN 23
#define COIL2_ENABLE_PIN 24
#define COIL3_ENABLE_PIN 25
#define COIL4_ENABLE_PIN 26

#define SPI_CS_PIN 5
#define TEMP_SIM_PIN A1     // Entrada del termometro simulado (0-5 V -> 0-50 C).
#define FIELD_MODE_PIN 22  // Selector del mux de fase (campo X / campo nulo).
                           // Pin sin timer a proposito: es una senal digital
                           // simple y los pines con PWM (2-13, 44-46) quedan
                           // libres para los canales de intensidad por bobina
                           // (ver docs/coil-excitation.md).



Timer timer;
SerialLink serial(Serial1);
SPIClass spi;

/*===== inputs =====*/
MagnetometerManager magnetometermanager;
// Ambos magnetometros quedan instanciados a la vez (sim + real) para poder
// alternar por software cual esta activo, sin reflashear en el laboratorio:
// lo elige la clave `sensor` de detector.sources en el archivo de la SD, y
// Engine::_applySourceSettings() aplica esa eleccion en cada start.
MagnetometerVoltageSim magnetometerSim("CEM1", A0);
MagnetometerMlx90393 magnetometerReal("CEM1");
ThermometerManager thermometermanager;
// TEMP1 simulado: solo se registra si la tarjeta SD pide
// detector.sources[TEMP1].sensor = "sim". Con la escala 0-5 V -> 0-50 C cada
// volt son 10 grados, asi que con un potenciometro se recorren todas las
// zonas de los menus (normal 30~40, critico 25~45 de fabrica).
ThermometerVoltageSim thermometerSim("TEMP1", TEMP_SIM_PIN, 50.0f);

// Lecturas sinteticas (tarjeta 22): CEM1 y TEMP1 calculados a partir del
// escenario de la tarjeta SD (detector.sources[].scenario), sin sensores. Su
// valor recorre toda la logica real: Detector, flags, corte, pantalla y
// monitor remoto. El de CEM1 responde al duty comun del lazo (la "planta"),
// que se lee de fieldController -- declarado mas abajo, por eso una funcion.
float currentCommonDuty();
MagnetometerScenario magnetometerScenario("CEM1", currentCommonDuty);
ThermometerScenario thermometerScenario("TEMP1");
// DOS modulos ADS1115: cada uno tiene solo 2 pares diferenciales (AIN0-AIN1
// y AIN2-AIN3) y el SCT013 se lee en diferencial, asi que los 4 canales del
// gabinete necesitan dos chips. 0x48 es ADDR a GND (el default) y 0x49 es
// ADDR a VDD.
//
// El segundo se declara aunque todavia no exista fisicamente: no se le habla
// hasta que conteste en el bus (ver CurrentSensorSct013::begin()), y tenerlo
// declarado hace que agregarlo en el futuro sea editar el archivo de la SD
// en vez de reflashear.
#define ADS1_ADDRESS 0x48
#define ADS2_ADDRESS 0x49
Adafruit_ADS1115 ads1;
Adafruit_ADS1115 ads2;
CurrentSensorsManager currentsensormanager;

// Un objeto por entrada fisica posible, estaticos igual que los CoilChannel.
// Cada uno queda ligado de por vida a su modulo; el archivo de la SD decide
// cuales se habilitan y con que parametros (Engine los reconfigura via
// setConfig()). Estaticos y no `new` a proposito:
// CurrentSensorsManager::clearCurrentSensors() no libera, asi que
// reinstanciarlos en cada start seria una fuga lenta en 8 KB de RAM.
CurrentSensorSct013 currentSensor1(ads1, { ADS1_ADDRESS, 0, 5.0f, 1.0f, 0.775f, 860, 200, "SCT013-1" });
CurrentSensorSct013 currentSensor2(ads1, { ADS1_ADDRESS, 1, 5.0f, 1.0f, 0.775f, 860, 200, "SCT013-2" });
CurrentSensorSct013 currentSensor3(ads2, { ADS2_ADDRESS, 0, 5.0f, 1.0f, 0.775f, 860, 200, "SCT013-3" });
CurrentSensorSct013 currentSensor4(ads2, { ADS2_ADDRESS, 1, 5.0f, 1.0f, 0.775f, 860, 200, "SCT013-4" });

/*===== outputs =====*/
SignalGenerator signalGenerator(spi, SPI_CS_PIN);
// Selector de modo de experimento: un unico pin hacia el multiplexor
// analogico que elige si el par de bobinas 3-4 recibe la senal directa
// (campo X) o la invertida por el op-amp (campo nulo, grupo control). La
// inversion de 180 grados es analogica, no por software -- ver
// docs/adr/001-inversion-de-fase.md. nullLevel queda en HIGH hasta que se
// confirme como se cablea el mux en la placa.
CoilExcitation coilExcitation(signalGenerator, FIELD_MODE_PIN, HIGH);
// 3906 Hz (prescaler 8) es la frecuencia acordada como contrato de salida
// para la excitacion de bobinas -- ver docs/coil-excitation.md. Es la mas
// alta alcanzable sin cambiar el modo del timer, y deja margen holgado para
// que la etapa de potencia filtre el PWM a un nivel DC sin que el rizado se
// mezcle con la senoidal de trabajo (10-50 Hz).
PwmDriver pwmCoil1(COIL1_PWM_PIN, 3906);
PwmDriver pwmCoil2(COIL2_PWM_PIN, 3906);
PwmDriver pwmCoil3(COIL3_PWM_PIN, 3906);
PwmDriver pwmCoil4(COIL4_PWM_PIN, 3906);

// Los factores de calibracion arrancan en 1.0 (sin corregir) y se cargan
// recien cuando se ejecute la calibracion en banco -- ver
// docs/protocolo-calibracion-intensidad.md y la tarjeta 8b. Un 1.0 no
// significa "calibrado y sin desvio": significa "todavia sin medir".
// AGRUPAMIENTO DE FASE (cableado, no software): las bobinas 1 y 3 reciben la
// senoidal directa -- grupo FIJO. Las bobinas 2 y 4 reciben la rama que pasa
// por el multiplexor: en fase con las otras (campo X) o invertida 180 grados
// (campo nulo), segun el selector de CoilExcitation.
//
// El agrupamiento es alternado (1,3 vs 2,4) y no por pares contiguos (1,2 vs
// 3,4) para que cualquier subconjunto de dos bobinas consecutivas tenga una
// de cada grupo. Con 1 y 2 solas -- el default de abajo -- ya se pueden
// ejercitar los dos modos; con 1 y 2 en el mismo grupo, el campo nulo no
// seria probable hasta tener las 4 montadas.
CoilChannel coil1(pwmCoil1, COIL1_ENABLE_PIN, "BOB1");  // grupo fijo
CoilChannel coil2(pwmCoil2, COIL2_ENABLE_PIN, "BOB2", HIGH, CoilGroup::Invertible);  // grupo invertible
CoilChannel coil3(pwmCoil3, COIL3_ENABLE_PIN, "BOB3");  // grupo fijo
CoilChannel coil4(pwmCoil4, COIL4_ENABLE_PIN, "BOB4", HIGH, CoilGroup::Invertible);  // grupo invertible
CoilChannels coilChannels;
// Parametros del regulador de intensidad. Este es el UNICO lugar donde se
// fijan: cuando la configuracion por SD este implementada (ver
// docs/config-schema.md), es aca donde el archivo los va a pisar.
//
// kp, maxStep y deadBand son valores de CALIBRACION y hoy no estan calibrados
// contra bobinas reales -- son los defaults del header, no numeros medidos.
// El procedimiento para obtenerlos esta en
// docs/protocolo-calibracion-intensidad.md. Dos cosas de ahi que conviene
// tener a mano antes de tocarlos:
//
//   - kp NO se lee del duty de equilibrio: en equilibrio el error es cero y
//     kp no interviene. Se calcula como alfa/K, donde K es la ganancia de
//     planta (mT por unidad de duty) medida en lazo abierto, con alfa entre
//     0.1 y 0.3. kp*error es un paso de duty, asi que kp esta en duty/mT.
//   - maxStep tiene que ser MAYOR que el paso tipico kp*error en operacion,
//     o el clamp actua siempre y el control vuelve a ser de paso fijo.
//
// sampleTime no se fija a mano: sale derivado del intervalo real de medicion.
FieldController::Config config = makeFieldControllerConfig(Intervals::MeasureMagneticField);
FieldController fieldController(config);
// Interruptor general de la etapa de potencia. El Relay se declara acá y no
// dentro de MainPowerSwitch para que el pin y el tipo de activacion queden
// visibles junto al resto del cableado de la placa.
Relay mainRelay(RELE1_PIN, Relay::ACTIVE_LOW);
MainPowerSwitch mainPowerSwitch(mainRelay);
Detector detector;
EmergencyButton emergencyButton(EMERGENCY_BUTTON_PIN);

Engine engine(
  timer,
  serial,
  magnetometermanager,
  thermometermanager,
  currentsensormanager,
  coilExcitation,
  coilChannels,
  fieldController,
  mainPowerSwitch,
  detector,
  emergencyButton,
  magnetometerSim,
  magnetometerReal
);


void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN(DEBUG_MAIN, F(""));
  DEBUG_PRINTLN(DEBUG_MAIN, F("========== BOOT =========="));
  printResetCause();
  wdt_disable();

  Serial1.begin(9600);
  delay(1000);
  spi.begin();
  Wire.begin();
  // Tope de 25 ms a cualquier transaccion I2C. Sin esto, un modulo que deja de
  // contestar (cable suelto, bus trabado) cuelga el loop() en el ACK que no
  // llega -- y con el, la lectura del boton de emergencia. En timeout el
  // hardware TWI se reinicia y la transaccion falla limpia.
  Wire.setWireTimeout(25000, true);
  scanI2CBus();

  /* ===== magnetometers =====*/
  magnetometermanager.clearMagnetometers();
  // Se inicializan los dos sensores (sim y real); cual queda activo lo
  // decide la clave `sensor` de detector.sources en el archivo de la SD,
  // que Engine aplica en cada "start" (_applySourceSettings).
  // MagnetometerMlx90393::begin() no cuelga el loop si el sensor no
  // responde (begin_I2C devuelve false y update() queda en no-op), asi que
  // es seguro llamarlo aunque el MLX90393 todavia no este cableado.
  magnetometerSim.begin();
  magnetometerReal.begin();
  // Default compilado: el MLX90393 real, igual que docs/config.example.json
  // (hasta la tarjeta 22 era el sim de A0; ver Engine::_syntheticField).
  // Hace falta registrarlo aca y no solo en el start para que haya lecturas
  // de CEM1 en pantalla desde el arranque; Engine lo reemplaza por el sim o
  // el escenario si el archivo lo pide. Sin MLX cableado, simplemente no
  // hay lecturas.
  magnetometermanager.addMagnetometer(&magnetometerReal);
  engine.registerScenarioSensors(&magnetometerScenario, &thermometerScenario);

  /* ===== thermometers =====*/
  thermometermanager.clearThermometers();
  // Re-habilitado (2026-08-29): DS18B20 cableado para bring-up. TEMP1_ADDRESS
  // es el ROM code de 64 bits del sensor original -- si el DS18B20 fisico
  // conectado ahora es OTRA unidad, esta direccion no va a matchear y
  // getTempC() va a devolver -127 (filtrado como invalido por
  // ThermometerDS18B20, no crashea, pero tampoco vas a ver lecturas).
  OneWire* oneWire = new OneWire(WIRE_PIN);
  DallasTemperature* dallas = new DallasTemperature(oneWire);
  dallas->begin();
  printOneWireDevices(*dallas);
  DeviceAddress temp1Address = TEMP1_ADDRESS;
  ThermometerDS18B20* thermometer1 = new ThermometerDS18B20(*dallas, "TEMP1", temp1Address);
  // Default compilado: el DS18B20, registrado ya para que haya lecturas de
  // TEMP1 en pantalla desde el arranque. En cada start Engine lo reemplaza
  // por el simulado, o no registra ninguno, segun la clave `sensor` de
  // detector.sources[TEMP1] (_applySourceSettings).
  thermometermanager.addThermometer(thermometer1);
  engine.registerThermometers(thermometer1, &thermometerSim);

  /* ===== current sensors =====*/
  // No se llama begin() a los ADS acá: lo hace CurrentSensorSct013::begin()
  // cuando Engine registra el sensor, y recien despues de confirmar que el
  // modulo contesta en el bus. Un chip ausente no puede colgar el arranque
  // (antes habia un `while(true)` comentado que hacia justo eso) ni el
  // loop() (readADC_Differential espera un ACK que no llega, sin timeout).
  currentsensormanager.clearCurrentSensors();
  // Los 4 slots se le presentan a Engine; cuales terminan registrados en el
  // manager lo decide la seccion currentSensors del archivo de la SD, y se
  // aplica en cada start. Sin tarjeta queda el default compilado: solo
  // SCT013-1, que es el unico canal cableado hoy.
  engine.registerCurrentSensor(&currentSensor1, ADS1_ADDRESS, 0);
  engine.registerCurrentSensor(&currentSensor2, ADS1_ADDRESS, 1);
  engine.registerCurrentSensor(&currentSensor3, ADS2_ADDRESS, 0);
  engine.registerCurrentSensor(&currentSensor4, ADS2_ADDRESS, 1);
  engine.enableCurrentSensor(ADS1_ADDRESS, 0, true);

  /* ===== canales de bobina =====*/
  // begin() de cada canal lo hace Engine::begin() via beginAll(), igual que
  // con CoilExcitation y MainPowerSwitch.
  coilChannels.clearChannels();
  // Por defecto solo BOB1 y BOB2: una de cada grupo de fase, que es el
  // minimo con el que los dos modos de experimento tienen sentido. Las 4
  // estan declaradas y listas -- descomentar a medida que se monten, o
  // dejarlo librado al archivo de configuracion cuando exista (ver
  // docs/config-schema.md).
  coilChannels.addChannel(&coil1);
  coilChannels.addChannel(&coil2);
  //coilChannels.addChannel(&coil3);
  //coilChannels.addChannel(&coil4);

  /* ===== interruptor general =====*/
  // No se llama begin() acá: lo hace Engine::begin(), igual que con
  // CoilExcitation. Antes estaba duplicado en los dos lados.

  /* ===== detector =====*/
  detector.clear();
  // Estado inicial de las fuentes, vigente hasta el primer "start": ahi
  // Engine::_applySourceSettings() registra/desregistra segun lo que haya
  // traido el archivo de la SD (detector.sources[].enabled), que es tambien
  // el interruptor para volver a vigilar CEM1 sin recompilar.
  //
  // CEM1 arranca sin registrar porque A0 puede estar sin cablear: un pin
  // flotante da lecturas fuera de rango que disparan un falso flag
  // "critical" y cortan el experimento (ver MagnetometerVoltageSim::update()).
  detector.addSource("TEMP1");

  /* ===== engine =====*/
  engine.begin();

}

void loop() {
  static unsigned long lastRamLog = 0;
  if (millis() - lastRamLog >= 10000) {
    lastRamLog = millis();
    DEBUG_PRINT(DEBUG_MAIN, "[RAM] libre = ");
    DEBUG_PRINTLN(DEBUG_MAIN, freeMemory());
  }
  engine.update();
}

// Duty comun que calcula el lazo (antes de los factores por bobina): es la
// entrada de la planta del escenario de CEM1. Con las bobinas bloqueadas
// (campo sintetico) ese duty no sale a ningun pin, pero es el que el lazo
// "cree" estar aplicando, y eso es lo que hay que simular.
float currentCommonDuty() {
  return fieldController.getOutput();
}
