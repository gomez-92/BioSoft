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
#include "currentmanager.hpp"
#include "currentsensorsct013.hpp"
#include "relaymanager.hpp"
#include "fieldcontroller.hpp"
#include "pwmdriver.hpp"
#include "signalgenerator.hpp"
#include "coilexcitationmanager.hpp"
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
#define PWM_PIN 44  // Pin con timer de hardware real (Timer5/OC5C). A0 no tiene
                     // timer asociado en el Mega2560: analogWrite() ahi cae a un
                     // digitalWrite binario, no genera PWM real. Requiere mover
                     // el cable de A0 a pin 44 en la placa fisica.
#define SPI_CS_PIN 5
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
// Bring-up de pruebas INT-001 (2026-08-31): ambos magnetometros quedan
// instanciados a la vez (sim + real) para poder alternar por software cual
// esta activo via el parametro "testMode" del comando start, sin reflashear
// en el laboratorio. Ninguno se registra en magnetometermanager en setup();
// Engine::_applyTestMode() decide cual agregar en cada start. Ver tarjeta
// Trello "Prueba de integracion -- Control de intensidad CEM (INT-001)".
MagnetometerVoltageSim magnetometerSim("CEM1", A0);
MagnetometerMlx90393 magnetometerReal("CEM1");
ThermometerManager thermometermanager;
Adafruit_ADS1115 ads1;
CurrentSensorsManager currentsensormanager;

/*===== outputs =====*/
SignalGenerator signalGenerator(spi, SPI_CS_PIN);
// Selector de modo de experimento: un unico pin hacia el multiplexor
// analogico que elige si el par de bobinas 3-4 recibe la senal directa
// (campo X) o la invertida por el op-amp (campo nulo, grupo control). La
// inversion de 180 grados es analogica, no por software -- ver
// docs/adr/001-inversion-de-fase.md. nullLevel queda en HIGH hasta que se
// confirme como se cablea el mux en la placa.
CoilExcitationManager coilExcitation(signalGenerator, FIELD_MODE_PIN, HIGH);
// 3906 Hz (prescaler 8) es la frecuencia acordada como contrato de salida
// para la excitacion de bobinas -- ver docs/coil-excitation.md. Es la mas
// alta alcanzable sin cambiar el modo del timer, y deja margen holgado para
// que la etapa de potencia filtre el PWM a un nivel DC sin que el rizado se
// mezcle con la senoidal de trabajo (10-50 Hz).
PwmDriver pwmDriver(PWM_PIN, 3906);
FieldController::Config config;
FieldController fieldController(config);
RelayManager relayManager;
Detector detector;
EmergencyButton emergencyButton(EMERGENCY_BUTTON_PIN);

Engine engine(
  timer,
  serial,
  magnetometermanager,
  thermometermanager,
  currentsensormanager,
  coilExcitation,
  pwmDriver,
  fieldController,
  relayManager,
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
  scanI2CBus();

  /* ===== magnetometers =====*/
  magnetometermanager.clearMagnetometers();
  // Bring-up de pruebas INT-001 (2026-08-31): se inicializan los dos
  // sensores (sim y real) pero ninguno se registra aca -- Engine decide
  // cual activar en cada "start" segun el parametro "testMode" (1-6, ver
  // Engine::_applyTestMode). MagnetometerMlx90393::begin() no cuelga el
  // loop si el sensor no responde (begin_I2C devuelve false y update()
  // queda en no-op), asi que es seguro llamarlo aunque el MLX90393 todavia
  // no este cableado.
  magnetometerSim.begin();
  magnetometerReal.begin();

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
  DeviceAddress temp1Address = TEMP1_ADDRESS;
  ThermometerDS18B20* thermometer1 = new ThermometerDS18B20(*dallas, "TEMP1", temp1Address);
  thermometermanager.addThermometer(thermometer1);

  /* ===== current sensors =====*/
  ads1.begin();
  /*
  if (!ads1.begin()) {
    DEBUG_PRINTLN(DEBUG_MAIN, "ERROR: ADS1115 no encontrado");
    while (true) {
      delay(1000);
    }
  }
  */

  ads1.setGain(GAIN_TWO);
  ads1.setDataRate(RATE_ADS1115_860SPS);

  currentsensormanager.clearCurrentSensors();
  // Re-habilitado (2026-08-30): ADS1115 cableado y confirmado con ACK en
  // 0x48 (scan de diagnostico, ya retirado). Antes de esto,
  // CurrentSensorSct013::update() colgaba loop() indefinidamente con el chip
  // desconectado -- ver CLAUDE.md.
  CurrentSensorConfig sensor1Config = {
    .channel = 0,              // AIN0-AIN1
    .ratedCurrent = 5.0f,
    .ratedVoltage = 1.0f,
    .calibration = 0.775f,
    .sampleRate = 860,
    .integrationTimeMs = 200,
    .name = "SCT013-1"
  };
  CurrentSensorSct013* currentSensor1 = new CurrentSensorSct013(
    ads1,
    sensor1Config
  );
  currentsensormanager.addCurrentSensor(currentSensor1);

  /* ===== relays =====*/
  Relay* rele1 = new Relay(RELE1_PIN, Relay::ACTIVE_LOW);
  relayManager.addRelay(rele1);
  relayManager.beginAll();

  /* ===== detector =====*/
  detector.clear();
  // CEM1 deshabilitado temporalmente (2026-08-29): bring-up de enlace
  // serie ESP32<->Mega y pantallas (duracion + boton Detener) con A0 sin
  // cablear -- un pin flotante genera lecturas fuera de rango que disparan
  // un falso flag "critical" y cortan el experimento antes de tiempo (ver
  // MagnetometerVoltageSim::update(), magnetometervoltagesim.hpp). Volver a
  // habilitar esta linea apenas A0 tenga el potenciometro/fuente real
  // cableado (MOD-007 en Trello).
  //detector.addSource("CEM1");
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
