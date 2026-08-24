#pragma once
#include <Arduino.h>
#include "magnetometermanager.hpp"

// Sensor de campo magnetico SIMULADO para pruebas de laboratorio: en vez
// de leer un MLX90393 real por I2C, lee un voltaje analogico (potenciometro
// o fuente de tension externa) y lo mapea linealmente a un valor de campo
// en mT. NO sirve para control real de intensidad (MOD-011) -- solo para
// validar la logica de monitoreo/deteccion (registro en el Detector,
// umbrales, flags) sin necesitar bobinas reales. Reemplazar por
// MagnetometerMlx90393 antes de cualquier prueba con hardware real.
class MagnetometerVoltageSim : public IMagnetometer {
  private:
    const char* _name;
    uint8_t _analogPin;
    float _referenceVoltage;
    float _maxFieldMt;
    float _magneticField;
    bool _isValid;

  public:
    MagnetometerVoltageSim(const char* name, uint8_t analogPin, float maxFieldMt = 3.0f, float referenceVoltage = 5.0f);
    void begin() override;
    void update() override;
    float getMagneticField() const override;
    bool isValid() const override;
    const char* getName() const override;
};

inline MagnetometerVoltageSim::MagnetometerVoltageSim(const char* name, uint8_t analogPin, float maxFieldMt, float referenceVoltage)
  : _name(name), _analogPin(analogPin), _referenceVoltage(referenceVoltage), _maxFieldMt(maxFieldMt),
    _magneticField(0.0f), _isValid(false) {}

inline void MagnetometerVoltageSim::begin() {
  _isValid = true;
}

inline void MagnetometerVoltageSim::update() {
  int raw = analogRead(_analogPin);
  float voltage = (raw / 1023.0f) * _referenceVoltage;
  _magneticField = (voltage / _referenceVoltage) * _maxFieldMt;
  _isValid = true;
}

inline float MagnetometerVoltageSim::getMagneticField() const {
  return _magneticField;
}

inline bool MagnetometerVoltageSim::isValid() const {
  return _isValid;
}

inline const char* MagnetometerVoltageSim::getName() const {
  return _name;
}
