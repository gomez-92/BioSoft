#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_MAGNETOMETERS 10

class IMagnetometer {
  public:
    virtual ~IMagnetometer() = default;
    virtual void begin() = 0;
    virtual void update() = 0;
    virtual float getMagneticField() const = 0;
    virtual bool isValid() const = 0;
    virtual const char* getName() const = 0;
};

class IMagnetometerListener {
  public:
    virtual ~IMagnetometerListener() = default;
    virtual void onMagnetometerSample(IMagnetometer* magnetometer) = 0;
};

class MagnetometerManager {
  private:
    struct ManagedMagnetometer {
        IMagnetometer* magnetometer = nullptr;
        uint8_t magnetometerId = 0;
    };

    ManagedMagnetometer _magnetometers[MAX_MAGNETOMETERS];
    uint8_t _magnetometerCount = 0;
    IMagnetometerListener* _listener = nullptr;

public:
    MagnetometerManager();
    uint8_t addMagnetometer(IMagnetometer* magnetometer);
    void clearMagnetometers();
    IMagnetometer* findByName(const char* name) const;
    void updateAll();
    void publishMeasures(JsonDocument& measures) const;
    uint8_t magnetometerCount() const;
    void setMagnetometerListener(IMagnetometerListener* listener);
};

inline MagnetometerManager::MagnetometerManager() : _magnetometerCount(0), _listener(nullptr)  {}

inline uint8_t MagnetometerManager::addMagnetometer(IMagnetometer* magnetometer) {
  if (_magnetometerCount >= MAX_MAGNETOMETERS || magnetometer == nullptr) return 0xFF;
  magnetometer->begin();
  
  ManagedMagnetometer& m = _magnetometers[_magnetometerCount];
  m.magnetometer = magnetometer;
  m.magnetometerId = _magnetometerCount;
  _magnetometerCount++;
  return m.magnetometerId;

}

inline void MagnetometerManager::clearMagnetometers() {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    _magnetometers[i] = {};
  }
  _magnetometerCount = 0;
}

inline IMagnetometer* MagnetometerManager::findByName(const char* name) const {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    auto* m = _magnetometers[i].magnetometer;
    if (m && strcmp(m->getName(), name) == 0) {
        return m;
    }
  }
  return nullptr;
}

inline void MagnetometerManager::updateAll() {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    auto& m = _magnetometers[i];
    if (!m.magnetometer) continue;
    m.magnetometer->update();
    if (!m.magnetometer->isValid()) continue;
    if (_listener) {
      _listener->onMagnetometerSample(m.magnetometer);
    }
  }
}

inline void MagnetometerManager::publishMeasures(JsonDocument& measures) const {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    auto* m = _magnetometers[i].magnetometer;
    if (!m) continue;
    measures[m->getName()] = m->getMagneticField();
  }
}

inline uint8_t MagnetometerManager::magnetometerCount() const {
  return _magnetometerCount;
}

inline void MagnetometerManager::setMagnetometerListener(IMagnetometerListener* listener) {
  _listener = listener;
}
