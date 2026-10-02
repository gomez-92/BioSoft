#pragma once
#include <Arduino.h>
#include "thermometermanager.hpp"
#include <DallasTemperature.h>

// Rango de temperatura fisicamente plausible para este experimento -- se
// usa para descartar lecturas invalidas. En particular filtra el -127.0
// que DallasTemperature devuelve como codigo de error (sensor desconectado
// / fallo de lectura CRC), pero de paso tambien rechaza cualquier lectura
// <=0 o >=127, no solo el valor exacto de error.
constexpr float DALLAS_MIN_PLAUSIBLE_TEMP_C = 0.0f;
constexpr float DALLAS_MAX_PLAUSIBLE_TEMP_C = 127.0f;

class ThermometerDS18B20 : public IThermometer {
  private:
    const char* _name;
    DeviceAddress _address;
    DallasTemperature& _dallasThermometer;
    float _temperature;
    bool _isValid;

    // Conversion asincrona: update() la dispara, poll() la recoge ~800 ms
    // despues. requestTemperatures() con waitForConversion en true bloquea
    // ~750 ms POR LLAMADA, aunque no haya sensor en el bus: en ese tiempo no se
    // lee el boton de emergencia ni se muestrea el campo. 800 ms cubre los
    // 12 bits (el peor caso del chip) sea cual sea la resolucion configurada.
    static constexpr unsigned long ConversionWaitMs = 800;
    bool _converting = false;
    unsigned long _requestedAtMs = 0;

  public:
    ThermometerDS18B20(DallasTemperature& dallasThermometer, const char* name, const DeviceAddress& address);
    void begin() override;
    void update() override;
    bool isAsync() const override { return true; }
    bool poll() override;
    float getTemperature() const override;
    bool isValid() const override;
    const char* getName() const override;
    const DeviceAddress& getAddress() const;
    bool setAddress(const uint8_t* address) override;
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
  // Si ya hay una conversion en curso se deja terminar: el Timer puede
  // disparar de nuevo antes de que poll() la haya recogido.
  if (_converting) return;
  _dallasThermometer.setWaitForConversion(false);
  _dallasThermometer.requestTemperatures();
  _requestedAtMs = millis();
  _converting = true;
}

// Lee el resultado cuando la conversion termino. Lo que bloquea es solo la
// lectura del scratchpad por OneWire (~10 ms), no la conversion. Un sensor
// ausente da -127: queda invalido y no hay muestra (la regla de silencio del
// Detector se ocupa).
inline bool ThermometerDS18B20::poll() {
  if (!_converting) return false;
  if (millis() - _requestedAtMs < ConversionWaitMs) return false;
  _converting = false;
  _temperature = _dallasThermometer.getTempC(_address);
  _isValid = _temperature > DALLAS_MIN_PLAUSIBLE_TEMP_C && _temperature < DALLAS_MAX_PLAUSIBLE_TEMP_C;
  return true;
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

// La lectura en curso era de la direccion anterior: se descarta, y el sensor
// queda invalido hasta la primera lectura buena de la nueva.
inline bool ThermometerDS18B20::setAddress(const uint8_t* address) {
  memcpy(_address, address, sizeof(DeviceAddress));
  _converting = false;
  _isValid = false;
  return true;
}