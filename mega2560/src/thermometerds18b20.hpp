#pragma once
#include <Arduino.h>
#include "thermometermanager.hpp"
#include <DallasTemperature.h>

class ThermometerDS18B20 : public IThermometer {
  private:
    const char* _name;
    DeviceAddress _address;
    DallasTemperature& _dallasThermometer;
    float _temperature;
    bool _isValid;

  public:
    ThermometerDS18B20(DallasTemperature& dallasThermometer, const char* name, const DeviceAddress& address);
    void begin() override;
    void update() override;
    float getTemperature() const override;
    bool isValid() const override;
    const char* getName() const override;
    const DeviceAddress& getAddress() const;
};

// =====================================================
// Constructor
// =====================================================
inline ThermometerDS18B20::ThermometerDS18B20(DallasTemperature& dallasThermometer, const char* name, const DeviceAddress& address)
     :_name(name), 
     _dallasThermometer(dallasThermometer), 
     _temperature(0.0f), 
     _isValid(false)
{
    memcpy(this->_address, address, sizeof(DeviceAddress));
}

// =====================================================
// Inicialización
// =====================================================
inline void ThermometerDS18B20::begin() {
  _dallasThermometer.begin();
}

// =====================================================
// Actualización
// =====================================================
inline void ThermometerDS18B20::update() {
  _dallasThermometer.requestTemperatures();
  _temperature = _dallasThermometer.getTempC(_address);
  _isValid = _temperature > 0.0f && _temperature < 127.0f;
}

// =====================================================
// Leer temperatura
// =====================================================
inline float ThermometerDS18B20::getTemperature() const {
  return _temperature;
}

// =====================================================
// Validación
// =====================================================
inline bool ThermometerDS18B20::isValid() const {
  return _isValid;
}

// =====================================================
// Get nombre
// =====================================================
inline const char* ThermometerDS18B20::getName() const {
  return _name;
}

// =====================================================
// Get address
// =====================================================
inline const DeviceAddress& ThermometerDS18B20::getAddress() const {
  return _address;
}