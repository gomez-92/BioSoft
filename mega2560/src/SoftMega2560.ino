#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "timer.hpp"
#include "seriallink.hpp"
#include "magnetometermanager.hpp"
#include "magnetometermlx90993.hpp"
#include "thermometermanager.hpp"
#include "thermometerds18b20.hpp"
#include "currentmanager.hpp"
#include "currentsensorsct013.hpp"
#include "relaymanager.hpp"
#include "fieldcontroller.hpp"
#include "pwmdriver.hpp"
#include "signalgenerator.hpp"
#include "detector.hpp"
#include "engine.hpp"
#include <avr/wdt.h>

void printResetCause() {

  uint8_t resetCause = MCUSR;

  MCUSR = 0;

  Serial.print(F("[RESET] MCUSR = 0x"));
  Serial.println(resetCause, HEX);

  if (resetCause & _BV(PORF)) {
    Serial.println(F("[RESET] Power-on reset"));
  }

  if (resetCause & _BV(EXTRF)) {
    Serial.println(F("[RESET] External reset"));
  }

  if (resetCause & _BV(BORF)) {
    Serial.println(F("[RESET] Brown-out reset"));
  }

  if (resetCause & _BV(WDRF)) {
    Serial.println(F("[RESET] WATCHDOG reset"));
  }

  if (resetCause & _BV(JTRF)) {
    Serial.println(F("[RESET] JTAG reset"));
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
#define TEMP1_ADDRESS { 0x28, 0x3F, 0xE5, 0x57, 0x04, 0xE1, 0x3D, 0xED }
#define PWM_PIN A0
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
  detector
);


void setup() {
  Serial.begin(115200);
  Serial.println(F(""));
  Serial.println(F("========== BOOT =========="));
  printResetCause();
  wdt_disable();

  Serial1.begin(9600);
  delay(1000);
  spi.begin();
  Wire.begin();

  /* ===== magnetometers =====*/
  magnetometermanager.clearMagnetometers();
  //MagnetometerMlx90393* magnetometer = new MagnetometerMlx90393("CEM1");
  //magnetometermanager.addMagnetometer(magnetometer);

  /* ===== thermometers =====*/
  thermometermanager.clearThermometers();
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
    Serial.println("ERROR: ADS1115 no encontrado");
    while (true) {
      delay(1000);
    }
  }
  */

  ads1.setGain(GAIN_TWO);
  ads1.setDataRate(RATE_ADS1115_860SPS);

  currentsensormanager.clearCurrentSensors();
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
  //detector.addSource("CEM1");
  detector.addSource("TEMP1");

  /* ===== engine =====*/
  engine.begin();

}

void loop() {
  static unsigned long lastRamLog = 0;
  if (millis() - lastRamLog >= 10000) {
    lastRamLog = millis();
    Serial.print("[RAM] libre = ");
    Serial.println(freeMemory());
  }
  engine.update();
}
