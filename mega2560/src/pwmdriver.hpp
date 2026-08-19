#pragma once

#include <Arduino.h>

class PwmDriver {
  public:
    PwmDriver(uint8_t pin, uint16_t frequency = 490, uint8_t resolution = 8);
    void enable();
    void disable();
    bool isEnabled() const;
    void write(float output);

  private:
    uint8_t _pin;
    uint16_t _frequency;
    uint8_t _resolution;
    bool _enabled;
    float clamp(float value) const;
    uint16_t outputToDuty(float output) const;
};

inline PwmDriver::PwmDriver(uint8_t pin, uint16_t frequency, uint8_t resolution) : _pin(pin), _frequency(frequency), _resolution(resolution), _enabled(false) {
  pinMode(_pin, OUTPUT);
  analogWrite(_pin, 0);
}

inline void PwmDriver::enable() {
  _enabled = true;
}

inline void PwmDriver::disable() {
  _enabled = false;
  analogWrite(_pin, 0);
}

inline bool PwmDriver::isEnabled() const {
  return _enabled;
}

inline void PwmDriver::write(float output) {
  if (!_enabled) {
    analogWrite(_pin, 0);
    return;
  }
  uint16_t duty = outputToDuty(output);
  analogWrite(_pin, duty);
}

inline float PwmDriver::clamp(float value) const {
  if (value < 0.0f)
    return 0.0f;

  if (value > 1.0f)
    return 1.0f;

  return value;
}

inline uint16_t PwmDriver::outputToDuty(float output) const {
  output = clamp(output);
  const uint16_t maxDuty = (1u << _resolution) - 1u;
  return static_cast<uint16_t>(output * maxDuty);
}