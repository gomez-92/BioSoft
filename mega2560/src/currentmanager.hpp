#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_CURRENT_SENSORS 10

// =====================================================
// Interfaz de sensor de corriente
// =====================================================
class ICurrentSensor {
  public:
    virtual ~ICurrentSensor() = default;
    virtual void begin() = 0;
    virtual void update() = 0;
    virtual float getCurrent() const = 0;
    virtual bool isValid() const = 0;
    virtual const char* getName() const = 0;
};

// =====================================================
// Listener (eventos hacia Engine)
// =====================================================
class ICurrentSensorListener {
  public:
    virtual ~ICurrentSensorListener() = default;
    virtual void onCurrentSensorSample(ICurrentSensor* currentSensor) = 0;
};

// =====================================================
// Manager
// =====================================================
class CurrentSensorsManager {
  private:
    struct ManagedCurrentSensor {
      ICurrentSensor* currentSensor = nullptr;
      uint8_t currentSensorId = 0;
    };
    ManagedCurrentSensor _currentSensors[MAX_CURRENT_SENSORS];
    uint8_t _currentSensorsCount = 0;
    ICurrentSensorListener* _listener;

  public:
    CurrentSensorsManager();
    uint8_t addCurrentSensor(ICurrentSensor* currentSensor);
    void clearCurrentSensors();
    void updateAll();
    ICurrentSensor* findByName(const char* name) const;
    void publishMeasures(JsonDocument& measures) const;
    uint8_t currentSensorsCount() const;
    void setCurrentSensorListener(ICurrentSensorListener* listener);
  private:
};

// =====================================================
// Constructor
// =====================================================
inline CurrentSensorsManager::CurrentSensorsManager() : _currentSensorsCount(0), _listener(nullptr) {}

// =====================================================
// Add current sensors
// =====================================================
inline uint8_t CurrentSensorsManager::addCurrentSensor(ICurrentSensor* currentSensor) {
  if (_currentSensorsCount >= MAX_CURRENT_SENSORS || currentSensor == nullptr) return 0xFF;
  currentSensor->begin();
  ManagedCurrentSensor& c = _currentSensors[_currentSensorsCount];
  c.currentSensor = currentSensor;
  c.currentSensorId = _currentSensorsCount;
  _currentSensorsCount++;
  return c.currentSensorId;
}

// =====================================================
// Clear all current sensors
// =====================================================
inline void CurrentSensorsManager::clearCurrentSensors() {
  for (uint8_t i = 0; i < _currentSensorsCount; i++) {
    _currentSensors[i] = {};
  }
  _currentSensorsCount = 0;
}

// =====================================================
// Find by name
// =====================================================
inline ICurrentSensor* CurrentSensorsManager::findByName(const char* name) const {
  for (uint8_t i = 0; i < _currentSensorsCount; i++) {
    auto* c = _currentSensors[i].currentSensor;
    if (c && strcmp(c->getName(), name) == 0) {
        return c;
    }
  }
  return nullptr;
}

// =====================================================
// Update all (adquisición + evento)
// =====================================================
inline void CurrentSensorsManager::updateAll() {
  for (uint8_t i = 0; i < _currentSensorsCount; i++) {
    auto& c = _currentSensors[i];
    if (!c.currentSensor) continue;
    c.currentSensor->update();
    if(!c.currentSensor->isValid()) continue;
    if (_listener) {
        _listener->onCurrentSensorSample(c.currentSensor);
    }
  }
}

// =====================================================
// Publish measures
// =====================================================
inline void CurrentSensorsManager::publishMeasures(JsonDocument& measures) const {
  for (uint8_t i = 0; i < _currentSensorsCount; i++) {
    auto* c = _currentSensors[i].currentSensor;
    if (!c) continue;
    measures[c->getName()] = c->getCurrent();
  }
}

// =====================================================
// Count
// =====================================================
inline uint8_t CurrentSensorsManager::currentSensorsCount() const {
  return _currentSensorsCount;
}

// =====================================================
// Listener
// =====================================================
inline void CurrentSensorsManager::setCurrentSensorListener(ICurrentSensorListener* listener) {
  _listener = listener;
}


