#pragma once

#include <Arduino.h>
#include <math.h>

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
    void applyFrequency();
};

inline PwmDriver::PwmDriver(uint8_t pin, uint16_t frequency, uint8_t resolution) : _pin(pin), _frequency(frequency), _resolution(resolution), _enabled(false) {
  pinMode(_pin, OUTPUT);
  analogWrite(_pin, 0);
}

inline void PwmDriver::enable() {
  applyFrequency();
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

// Ajusta el prescaler del Timer5 al valor mas cercano a _frequency. Solo
// tiene efecto si _pin es 44/45/46 (unico timer de este driver donde hoy
// se soporta frecuencia configurable) -- ver MOD-010 en Trello para el
// detalle completo de por que se eligio Timer5/pin 44 y que pines quedan
// sin cubrir. Se llama desde enable(), no desde el constructor: PwmDriver
// es un objeto global y su constructor corre antes que el init() de
// Arduino, que pisaria cualquier registro de timer tocado antes de tiempo.
inline void PwmDriver::applyFrequency() {
  if (_pin != 44 && _pin != 45 && _pin != 46) return;

  struct PrescalerOption { uint16_t divisor; uint8_t bits; };
  static const PrescalerOption options[] = {
    {1,    0b001},
    {8,    0b010},
    {64,   0b011},
    {256,  0b100},
    {1024, 0b101},
  };

  const float fClk = 16000000.0f;
  const float top = static_cast<float>(1u << _resolution);
  uint8_t bestBits = 0b011;
  float bestDiff = -1.0f;

  for (uint8_t i = 0; i < 5; i++) {
    float actualFreq = fClk / (options[i].divisor * top);
    float diff = fabs(actualFreq - static_cast<float>(_frequency));
    if (bestDiff < 0.0f || diff < bestDiff) {
      bestDiff = diff;
      bestBits = options[i].bits;
    }
  }

#ifndef UNIT_TEST
  TCCR5B = (TCCR5B & 0xF8) | bestBits;
#endif
}