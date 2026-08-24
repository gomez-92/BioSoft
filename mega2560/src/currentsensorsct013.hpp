#pragma once

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include "currentmanager.hpp"

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
    // Canal diferencial:
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
  private:

    Adafruit_ADS1115& _ads;
    CurrentSensorConfig _config;

    float _currentRms = 0.0f;
    bool _isValid = false;

  private:

    int16_t readRaw();

  public:

    CurrentSensorSct013(
        Adafruit_ADS1115& ads,
        const CurrentSensorConfig& config);

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
  _ads(ads),
  _config(config)
{}



//=============================================================================
// Inicialización
//=============================================================================

inline void CurrentSensorSct013::begin() {
  // El ADS1115 ya fue inicializado externamente.
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

  float sumSquares = 0;
  float dcOffset = 0;

  for (uint16_t i = 0; i < samples; i++) {
    int16_t raw = readRaw();

    // Conversión automática a Volts según la ganancia
    // configurada en el ADS1115.
    float volts = _ads.computeVolts(raw);

    // Filtro de remoción de offset DC de un solo paso (igual al usado por
    // EmonLib/OpenEnergyMonitor): dcOffset converge hacia la media de la
    // señal muestra a muestra, sin necesidad de guardar toda la ventana.
    dcOffset += (volts - dcOffset) / (i + 1);
    float filtered = volts - dcOffset;

    sumSquares += filtered * filtered;
  }

  float rmsVoltage = sqrt(sumSquares / samples);

  if (isnan(rmsVoltage) || isinf(rmsVoltage))
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
    return _config.name;
}
