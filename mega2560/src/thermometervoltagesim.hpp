#pragma once
#include <Arduino.h>
#include "thermometermanager.hpp"

// Termometro SIMULADO para pruebas de banco: en vez de un DS18B20 por
// OneWire, lee un voltaje analogico (potenciometro o fuente externa) y lo
// mapea linealmente a grados C. Mismo patron que MagnetometerVoltageSim.
//
// Sirve para lo que el sensor real no deja hacer en banco: provocar a
// voluntad una temperatura fuera de rango y ver al Detector levantar flags y
// cortar el experimento, sin calentar nada. NO mide temperatura -- un
// experimento con animales nunca deberia correr con TEMP1 en "sim".
//
// A diferencia del DS18B20, update() no bloquea: analogRead tarda ~100 us,
// contra los ~750 ms de requestTemperatures().
class ThermometerVoltageSim : public IThermometer {
  private:
    const char* _name;
    uint8_t _analogPin;
    float _referenceVoltage;
    float _maxTemperature;
    float _temperature;
    bool _isValid;

  public:
    ThermometerVoltageSim(const char* name, uint8_t analogPin, float maxTemperature = 50.0f, float referenceVoltage = 5.0f);
    void begin() override;
    void update() override;
    float getTemperature() const override;
    bool isValid() const override;
    const char* getName() const override;
};

inline ThermometerVoltageSim::ThermometerVoltageSim(const char* name, uint8_t analogPin, float maxTemperature, float referenceVoltage)
  : _name(name), _analogPin(analogPin), _referenceVoltage(referenceVoltage), _maxTemperature(maxTemperature),
    _temperature(0.0f), _isValid(false) {}

inline void ThermometerVoltageSim::begin() {
  _isValid = true;
}

inline void ThermometerVoltageSim::update() {
  int raw = analogRead(_analogPin);
  float voltage = (raw / 1023.0f) * _referenceVoltage;
  _temperature = (voltage / _referenceVoltage) * _maxTemperature;
  _isValid = true;
}

inline float ThermometerVoltageSim::getTemperature() const {
  return _temperature;
}

inline bool ThermometerVoltageSim::isValid() const {
  return _isValid;
}

inline const char* ThermometerVoltageSim::getName() const {
  return _name;
}
