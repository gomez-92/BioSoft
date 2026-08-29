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
#include "detector.hpp"
#include "emergencybutton.hpp"
#include "engine.hpp"
#include "debugconfig.hpp"
#include <avr/wdt.h>

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_MAIN = true;

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



Timer timer;
SerialLink serial(Serial1);
SPIClass spi;

/*===== inputs =====*/
MagnetometerManager magnetometermanager;
ThermometerManager thermometermanager;
Adafruit_ADS1115 ads1;
CurrentSensorsManager currentsensormanager;

/*===== outputs =====*/
SignalGenerator signalGenerator(spi, SPI_CS_PIN);
PwmDriver pwmDriver(PWM_PIN);
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
  signalGenerator,
  pwmDriver,
  fieldController,
  relayManager,
  detector,
  emergencyButton
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

  /* ===== magnetometers =====*/
  magnetometermanager.clearMagnetometers();
  // PENDIENTE (bloqueado por hardware): descomentar cuando haya
  // bobinas/acceso al laboratorio -- ver MOD-007 en Trello.
  //MagnetometerMlx90393* magnetometer = new MagnetometerMlx90393("CEM1");
  //magnetometermanager.addMagnetometer(magnetometer);

  // SIMULADO (2026-08-24): lee un voltaje en A0 (libre desde que PWM_PIN
  // se movio a 44) y lo mapea a un CEM ficticio en mT -- solo para validar
  // la logica de monitoreo/deteccion, NO sirve para control real de
  // intensidad. Reemplazar por MagnetometerMlx90393 antes de cualquier
  // prueba con bobinas reales.
  MagnetometerVoltageSim* magnetometer = new MagnetometerVoltageSim("CEM1", A0);
  magnetometermanager.addMagnetometer(magnetometer);

  /* ===== thermometers =====*/
  thermometermanager.clearThermometers();
  // Registro deshabilitado temporalmente (2026-08-29): bring-up sin DS18B20
  // cableado. ThermometerManager::updateAll() con cero termometros
  // registrados es un loop vacio (no toca hardware), pero CON el sensor
  // registrado, ThermometerDS18B20::update() llama
  // dallasThermometer.requestTemperatures(), que bloquea ~750ms por
  // conversion SIEMPRE (este delay no depende de si hay sensor real en el
  // bus) -- confirmado en el log de bring-up (MEASURE_TEMPERATURE tardo 752
  // ms). El dato en si ya se filtraba como invalido (-127), pero el bloqueo
  // en si segui pasando cada 5s. Volver a habilitar cuando el DS18B20 este
  // cableado (MOD-007 en Trello).
  //OneWire* oneWire = new OneWire(WIRE_PIN);
  //DallasTemperature* dallas = new DallasTemperature(oneWire);
  //dallas->begin();
  //DeviceAddress temp1Address = TEMP1_ADDRESS;
  //ThermometerDS18B20* thermometer1 = new ThermometerDS18B20(*dallas, "TEMP1", temp1Address);
  //thermometermanager.addThermometer(thermometer1);

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
  // Registro deshabilitado temporalmente (2026-08-29): bring-up sin ADS1115
  // cableado -- CurrentSensorSct013::update() (via readADC_Differential_*(),
  // Adafruit_ADS1115) CUELGA INDEFINIDAMENTE el loop() esperando un ACK I2C
  // que nunca llega con el chip desconectado/sin pull-ups. Confirmado en el
  // log de bring-up: se corto en seco despues de "Iniciando:
  // MEASURE_CURRENT", sin mas ticks del Timer (ni siquiera el log de RAM
  // cada 10s) -- coincide con esta llamada, no con un problema de duracion.
  // CurrentSensorsManager::updateAll() con cero sensores registrados es un
  // loop vacio y no toca el bus I2C. Volver a habilitar cuando el ADS1115
  // este cableado (MOD-006 en Trello).
  /*
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
  */

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
