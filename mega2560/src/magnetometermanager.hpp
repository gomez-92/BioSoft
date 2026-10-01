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

    // Sensores que miden por muestreo sin bloquear (el MLX90393 real, ver
    // fieldsampler.hpp). update() de un sensor asincrono no hace nada: Engine
    // llama pollAll() en cada vuelta de loop() y el sensor avisa cuando tiene
    // un valor de ventana nuevo. Los simulados y los escenarios siguen siendo
    // sincronicos y heredan estos defaults.
    virtual bool isAsync() const { return false; }
    virtual bool poll() { return false; }
    virtual bool beginSampling(float freqHz, uint32_t windowMs) { return false; }
    virtual void endSampling() {}
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
    void pollAll();
    void beginSamplingAll(float freqHz, uint32_t windowMs);
    void endSamplingAll();
    void publishMeasures(JsonDocument& measures) const;
    uint8_t magnetometerCount() const;
    void setMagnetometerListener(IMagnetometerListener* listener);
};

inline MagnetometerManager::MagnetometerManager() : _magnetometerCount(0), _listener(nullptr)  {}

inline uint8_t MagnetometerManager::addMagnetometer(IMagnetometer* magnetometer) {
  if (_magnetometerCount >= MAX_MAGNETOMETERS || magnetometer == nullptr) return 0xFF;

  // Bring-up INT-001 (2026-09-01): solo llama begin() si el magnetometro
  // TODAVIA no esta valido. Engine::_applySourceSettings() hace
  // clearMagnetometers() + addMagnetometer() en cada "start" (para poder
  // alternar sim/real desde el archivo de la SD sin reflashear) -- con
  // begin() incondicional, eso reinicializaba
  // MagnetometerMlx90393 (recrea el Adafruit_I2CDevice interno + reset
  // completo del chip via Adafruit_MLX90393::_init()) en cada experimento,
  // aunque ya hubiera arrancado bien en setup(). Esa reinicializacion en
  // caliente, segundos despues del begin() inicial, fallaba de forma
  // reproducible en banco (begin_I2C -> FALLO la segunda vez, primera vez
  // siempre OK) dejando el sensor invalido para el resto del experimento sin
  // ningun problema de cableado real. Si el sensor SI esta invalido (nunca
  // conecto, o se desconecto en medio de un experimento anterior), begin()
  // sigue llamandose para reintentar la inicializacion.
  if (!magnetometer->isValid()) {
    magnetometer->begin();
  }

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
    if (m.magnetometer->isAsync()) continue;
    m.magnetometer->update();
    if (!m.magnetometer->isValid()) continue;
    if (_listener) {
      _listener->onMagnetometerSample(m.magnetometer);
    }
  }
}

inline void MagnetometerManager::pollAll() {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    auto& m = _magnetometers[i];
    if (!m.magnetometer || !m.magnetometer->isAsync()) continue;
    if (!m.magnetometer->poll()) continue;
    if (!m.magnetometer->isValid()) continue;
    if (_listener) {
      _listener->onMagnetometerSample(m.magnetometer);
    }
  }
}

inline void MagnetometerManager::beginSamplingAll(float freqHz, uint32_t windowMs) {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    auto* m = _magnetometers[i].magnetometer;
    if (m && m->isAsync()) m->beginSampling(freqHz, windowMs);
  }
}

inline void MagnetometerManager::endSamplingAll() {
  for (uint8_t i = 0; i < _magnetometerCount; i++) {
    auto* m = _magnetometers[i].magnetometer;
    if (m && m->isAsync()) m->endSampling();
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
