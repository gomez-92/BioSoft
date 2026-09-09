#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "debugconfig.hpp"
#include "currentmanager.hpp"
#include "rmscalculator.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_CURRENTSENSOR = true;

/*
 * ============================================================================
 *  CurrentSensorSct013
 * ============================================================================
 *
 * Driver para un transformador de corriente SCT013 conectado a un ADS1115.
 *
 * Responsabilidades:
 *
 *  - Leer un canal diferencial del ADS1115.
 *  - Adquirir muestras durante una ventana de tiempo configurable.
 *  - Calcular el valor RMS de la tensión.
 *  - Convertir Vrms -> Irms.
 *  - Aplicar un factor de calibración.
 *
 * Esta clase NO inicializa ni configura el ADS1115.
 * Se asume que el ADS ya fue inicializado externamente.
 *
 * Compatible con sensores:
 *
 *      SCT013-005   (5A -> 1Vrms)
 *      SCT013-010  (10A -> 1Vrms)
 *      SCT013-020  (20A -> 1Vrms)
 *      etc...
 */

struct CurrentSensorConfig {
    // Direccion I2C del modulo ADS1115 que atiende a este sensor. Un ADS
    // tiene solo 2 pares diferenciales, asi que los 4 canales del gabinete
    // necesitan DOS modulos: 0x48 (ADDR a GND, el habitual) y 0x49 (ADDR a
    // VDD).
    //
    // Sin inicializador por defecto a proposito: con uno, la struct deja de
    // ser un agregado en C++11 (que es el estandar del build AVR) y no se
    // podria seguir construyendo con llaves desde el .ino.
    uint8_t address;

    // Canal diferencial. Solo 0 y 1 son validos -- no son las 4 entradas
    // simples del ADS: el SCT013 se lee en modo diferencial y el chip tiene
    // dos pares.
    // 0 -> AIN0-AIN1
    // 1 -> AIN2-AIN3
    uint8_t channel;

    // Corriente nominal del sensor.
    float ratedCurrent;

    // Tensión RMS entregada por el sensor a la corriente nominal.
    float ratedVoltage;

    // Factor de calibración.
    float calibration;

    // Velocidad del ADS1115 (Samples Per Second).
    uint16_t sampleRate;

    // Tiempo de integración del cálculo RMS.
    // Se recomienda:
    //   200 ms -> 10 ciclos @50Hz
    uint16_t integrationTimeMs;

    // Nombre descriptivo.
    const char* name;
};

class CurrentSensorSct013 : public ICurrentSensor {
  public:
    static constexpr uint8_t MaxNameLength = 16;  // 15 caracteres + terminador

  private:

    Adafruit_ADS1115& _ads;
    CurrentSensorConfig _config;
    // Copia propia del nombre. CurrentSensorConfig::name es un const char*, y
    // cuando la config llega por config_current esa cadena vive en el
    // JsonDocument del frame recibido, que muere apenas se procesa: apuntarla
    // dejaria un puntero colgado.
    char _name[MaxNameLength] = "";

    float _currentRms = 0.0f;
    bool _isValid = false;
    // false mientras no se haya confirmado que el modulo contesta en el bus.
    // Es la guarda que evita el cuelgue descrito en begin()/update().
    bool _moduleReady = false;

  private:

    int16_t readRaw();

  public:

    CurrentSensorSct013(
        Adafruit_ADS1115& ads,
        const CurrentSensorConfig& config);

    // Reconfigura el sensor despues de construido -- lo usa Engine al aplicar
    // la seccion currentSensors del archivo de la SD. El objeto queda ligado
    // de por vida al ADS que recibio en el constructor: la direccion I2C es
    // parte de su identidad fisica, no algo que se pueda reasignar.
    void setConfig(const CurrentSensorConfig& config);
    // Devuelve la config vigente. Engine la usa para sembrar su plantilla con
    // los defaults compilados del .ino: sin esto, una seccion currentSensors
    // ausente en el archivo dejaria al sensor con una config en cero (y
    // ratedVoltage 0 hace que update() ni siquiera mida).
    const CurrentSensorConfig& getConfig() const;

    void begin() override;
    void update() override;

    float getCurrent() const override;
    bool isValid() const override;
    const char* getName() const override;
};



//=============================================================================
// Constructor
//=============================================================================

inline CurrentSensorSct013::CurrentSensorSct013(
  Adafruit_ADS1115& ads,
  const CurrentSensorConfig& config)
  :
  _ads(ads)
{
  setConfig(config);
}

inline void CurrentSensorSct013::setConfig(const CurrentSensorConfig& config) {
  _config = config;

  if (config.name != nullptr) {
    strncpy(_name, config.name, MaxNameLength - 1);
    _name[MaxNameLength - 1] = '\0';
  }
  // _config.name queda apuntando a lo que trajo el llamador; getName()
  // devuelve _name, que es la copia. No se usa mas _config.name.
  _config.name = nullptr;

  _currentRms = 0.0f;
  _isValid = false;
}



//=============================================================================
// Inicialización
//=============================================================================

// Confirma que el modulo esta en el bus ANTES de hablarle, y recien ahi lo
// inicializa.
//
// Sin esto, un sensor habilitado sobre un ADS1115 ausente cuelga loop()
// indefinidamente: readADC_Differential_*() espera un ACK de I2C que nunca
// llega y no tiene timeout (confirmado en banco -- el log se cortaba justo
// despues de "Iniciando: MEASURE_CURRENT" y Tasks::Finish no disparaba nunca
// porque el MCU estaba congelado). Con la configuracion viniendo de un
// archivo eso pasa de "no descomentar esa linea" a algo que cualquiera puede
// provocar sin tocar el codigo, asi que la proteccion vive acá.
//
// beginTransmission()/endTransmission() es una transaccion de direccion sola:
// devuelve != 0 limpiamente si nadie contesta. Misma tecnica que el scan de
// diagnostico del .ino.
inline void CurrentSensorSct013::begin() {
  _moduleReady = false;

  // El ADS1115 tiene 2 pares diferenciales, no 4 entradas: un canal fuera de
  // {0,1} caeria en el `default` de readRaw(), que devuelve 0 -- un sensor
  // que mide 0 A para siempre y parece estar funcionando. Se rechaza acá.
  if (_config.channel > 1) {
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, F("[CURRENT] "));
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, _name);
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, F(": canal invalido "));
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, _config.channel);
    DEBUG_PRINTLN(DEBUG_CURRENTSENSOR, F(" (el ADS1115 solo tiene los pares 0 y 1)"));
    return;
  }

  Wire.beginTransmission(_config.address);
  if (Wire.endTransmission() != 0) {
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, F("[CURRENT] "));
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, _name);
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, F(": no hay ADS1115 en 0x"));
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, _config.address, HEX);
    DEBUG_PRINTLN(DEBUG_CURRENTSENSOR, F(" -- sensor deshabilitado (no se mide, pero el sistema sigue)"));
    return;
  }

  if (!_ads.begin(_config.address)) {
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, F("[CURRENT] "));
    DEBUG_PRINT(DEBUG_CURRENTSENSOR, _name);
    DEBUG_PRINTLN(DEBUG_CURRENTSENSOR, F(": el ADS1115 contesta pero begin() fallo"));
    return;
  }

  // Ganancia y data rate son del MODULO, no del canal: los dos sensores de
  // un mismo ADS los comparten y gana el ultimo que escribe. Por eso la
  // ganancia queda fija en firmware (no se expone en el esquema) y
  // sampleRate, que si es configurable, solo deberia diferir entre modulos.
  _ads.setGain(GAIN_TWO);
  _ads.setDataRate(RATE_ADS1115_860SPS);

  _moduleReady = true;

  DEBUG_PRINT(DEBUG_CURRENTSENSOR, F("[CURRENT] "));
  DEBUG_PRINT(DEBUG_CURRENTSENSOR, _name);
  DEBUG_PRINT(DEBUG_CURRENTSENSOR, F(": listo en 0x"));
  DEBUG_PRINT(DEBUG_CURRENTSENSOR, _config.address, HEX);
  DEBUG_PRINT(DEBUG_CURRENTSENSOR, F(" canal "));
  DEBUG_PRINTLN(DEBUG_CURRENTSENSOR, _config.channel);
}



//=============================================================================
// Lectura diferencial
//=============================================================================

inline int16_t CurrentSensorSct013::readRaw() {
  switch (_config.channel) {
    case 0:
      return _ads.readADC_Differential_0_1();
    case 1:
      return _ads.readADC_Differential_2_3();
    default:
      return 0;
  }
}



//=============================================================================
// Actualización de la medición RMS
//=============================================================================

inline void CurrentSensorSct013::update() {
  _isValid = false;
  _currentRms = 0;

  // Sin modulo confirmado no se toca el bus: es la guarda que impide el
  // cuelgue si el chip desaparece o nunca estuvo (ver begin()).
  if (!_moduleReady)
      return;

  // Configuración inválida.
  if (_config.sampleRate == 0)
      return;

  if (_config.ratedVoltage <= 0)
      return;

  // Cantidad de muestras necesarias para cubrir la
  // ventana de integración deseada.
  uint16_t samples =
      (_config.sampleRate * _config.integrationTimeMs) / 1000;

  if (samples == 0)
      return;

  // RMS + remoción de offset DC: ver RmsAccumulator (rmscalculator.hpp),
  // testeado en host en test_rmsaccumulator.
  RmsAccumulator rms;

  for (uint16_t i = 0; i < samples; i++) {
    int16_t raw = readRaw();

    // Conversión automática a Volts según la ganancia
    // configurada en el ADS1115.
    float volts = _ads.computeVolts(raw);

    rms.addSample(volts);
  }

  float rmsVoltage;
  if (!rms.result(rmsVoltage))
    return;

  _currentRms =
      rmsVoltage *
      (_config.ratedCurrent / _config.ratedVoltage) *
      _config.calibration;

  _isValid = true;
}



//=============================================================================
// Getters
//=============================================================================

inline float CurrentSensorSct013::getCurrent() const {
    return _currentRms;
}

inline bool CurrentSensorSct013::isValid() const {
    return _isValid;
}

inline const char* CurrentSensorSct013::getName() const {
    return _name;
}

inline const CurrentSensorConfig& CurrentSensorSct013::getConfig() const {
    return _config;
}
