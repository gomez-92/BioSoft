#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_THERMOMETERS 10

// =====================================================
// Interfaz de termómetro
// =====================================================

class IThermometer {
public:
    virtual ~IThermometer() = default;
    virtual void begin() = 0;
    virtual void update() = 0;
    virtual float getTemperature() const = 0;
    virtual bool isValid() const = 0;
    virtual const char* getName() const = 0;

    // Sensores cuya conversion tarda (el DS18B20, ~750 ms): update() solo la
    // DISPARA y no bloquea, y poll() -- Engine la llama en cada vuelta de
    // loop() -- devuelve true cuando hay una lectura nueva. Los simulados y
    // los escenarios son instantaneos y heredan estos defaults.
    virtual bool isAsync() const { return false; }
    virtual bool poll() { return false; }

    // Sensores de bus con direccion propia (el DS18B20, por su ROM code de 8
    // bytes): la direccion llega de la SD (detector.sources[TEMP1].address).
    // Los que no tienen direccion devuelven false y no cambian nada.
    virtual bool setAddress(const uint8_t* /*address*/) { return false; }
};

// =====================================================
// Listener (eventos hacia Engine)
// =====================================================

class IThermometerListener {
public:
    virtual ~IThermometerListener() = default;
    virtual void onThermometerSample(IThermometer* thermometer) = 0;
};

// =====================================================
// Manager
// =====================================================

class ThermometerManager {
private:
    struct ManagedThermometer {
        IThermometer* thermometer = nullptr;
        uint8_t thermometerId = 0;
    };
    ManagedThermometer _thermometers[MAX_THERMOMETERS];
    uint8_t _thermometerCount = 0;
    IThermometerListener* _listener = nullptr;

public:
    ThermometerManager();
    uint8_t addThermometer(IThermometer* thermometer);
    void clearThermometers();
    IThermometer* findByName(const char* name) const;
    void updateAll();
    void pollAll();
    void publishMeasures(JsonDocument& measures) const;
    uint8_t thermometerCount() const;
    void setThermometerListener(IThermometerListener* listener);
};

// =====================================================
// Constructor
// =====================================================
inline ThermometerManager::ThermometerManager() : _thermometerCount(0), _listener(nullptr) {}

// =====================================================
// Add thermometer
// =====================================================
inline uint8_t ThermometerManager::addThermometer(IThermometer* thermometer) {
  if (_thermometerCount >= MAX_THERMOMETERS || thermometer == nullptr) return 0xFF;
  // Mismo criterio que MagnetometerManager::addMagnetometer(): Engine vuelve
  // a registrar el termometro en cada start (_applySourceSettings), y uno que
  // ya esta leyendo bien no necesita reinicializarse -- en el DS18B20,
  // begin() vuelve a escanear el bus OneWire. Uno que nunca leyo, o que dejo
  // de leer, si se reintenta.
  if (!thermometer->isValid()) {
    thermometer->begin();
  }
  ManagedThermometer& t = _thermometers[_thermometerCount];
  t.thermometer = thermometer;
  t.thermometerId = _thermometerCount;
  _thermometerCount++;
  return t.thermometerId;
}

// =====================================================
// Clear all thermometers
// =====================================================
inline void ThermometerManager::clearThermometers() {
  for (uint8_t i = 0; i < _thermometerCount; i++) {
    _thermometers[i] = {};
  }
  _thermometerCount = 0;
}

// =====================================================
// Find by name
// =====================================================
inline IThermometer* ThermometerManager::findByName(const char* name) const {
  for (uint8_t i = 0; i < _thermometerCount; i++) {
    auto* t = _thermometers[i].thermometer;
    if (t && strcmp(t->getName(), name) == 0) {
        return t;
    }
  }
  return nullptr;
}

// =====================================================
// Update all (adquisición + evento)
// =====================================================
inline void ThermometerManager::updateAll() {
  for (uint8_t i = 0; i < _thermometerCount; i++) {
    auto& t = _thermometers[i];
    if (!t.thermometer) continue;
    t.thermometer->update();
    // Uno asincrono recien dispara la conversion: la muestra sale de pollAll().
    if (t.thermometer->isAsync()) continue;
    if (!t.thermometer->isValid()) continue;
    if (_listener) {
      _listener->onThermometerSample(t.thermometer);
    }
  }
}

// =====================================================
// Poll all (sensores asincronos: avisa cuando termino la conversion)
// =====================================================
inline void ThermometerManager::pollAll() {
  for (uint8_t i = 0; i < _thermometerCount; i++) {
    auto& t = _thermometers[i];
    if (!t.thermometer || !t.thermometer->isAsync()) continue;
    if (!t.thermometer->poll()) continue;
    if (!t.thermometer->isValid()) continue;
    if (_listener) {
      _listener->onThermometerSample(t.thermometer);
    }
  }
}

// =====================================================
// Publish measures
// =====================================================
inline void ThermometerManager::publishMeasures(JsonDocument& measures) const {
  for (uint8_t i = 0; i < _thermometerCount; i++) {
    auto* t = _thermometers[i].thermometer;
    if (!t) continue;
    measures[t->getName()] = t->getTemperature();
  }
}

// =====================================================
// Count
// =====================================================
inline uint8_t ThermometerManager::thermometerCount() const {
  return _thermometerCount;
}

// =====================================================
// Listener
// =====================================================
inline void ThermometerManager::setThermometerListener(IThermometerListener* listener) {
  _listener = listener;
}





