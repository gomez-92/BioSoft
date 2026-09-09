#pragma once

#include <Arduino.h>
#include <math.h>

// Timer de hardware al que esta cableado un pin PWM del Mega2560.
// PwmDriver solo maneja frecuencia sobre los timers de 16 bits que el core
// de Arduino configura igual (1/3/4/5); Timer0 y Timer2 quedan afuera a
// proposito -- ver timerForPin().
enum class PwmTimer : uint8_t { None, Timer1, Timer3, Timer4, Timer5 };

namespace PwmTiming {

  // Los 5 prescalers que comparten los timers 1/3/4/5, con los bits que
  // van en los 3 bits bajos de su registro TCCRnB.
  struct PrescalerOption { uint16_t divisor; uint8_t bits; };
  inline constexpr PrescalerOption Prescalers[] = {
    {1,    0b001},
    {8,    0b010},
    {64,   0b011},
    {256,  0b100},
    {1024, 0b101},
  };
  inline constexpr uint8_t PrescalerCount = 5;

  inline constexpr float ClockHz = 16000000.0f;

  // Frecuencia real de un pin PWM del Mega2560 para un prescaler dado.
  //
  // El factor 2 no es opcional: el core de Arduino pone los timers 1/3/4/5
  // en PWM *phase correct* de 8 bits (wiring.c, "put timer N in 8-bit
  // phase correct pwm mode"), y en ese modo el contador sube y baja, por
  // lo que un periodo son 2*top ticks, no top. Omitirlo -- como hacia la
  // version original de este driver -- da el doble de la frecuencia real y
  // hace elegir el prescaler equivocado: pedir los 490 Hz del default
  // terminaba seleccionando el divisor 256 (~122 Hz reales) en vez del 64
  // que el timer ya traia de fabrica, degradando en silencio una
  // configuracion que estaba bien.
  inline float frequencyFor(uint16_t divisor, uint8_t resolution) {
    const float top = static_cast<float>(1u << resolution);
    return ClockHz / (2.0f * static_cast<float>(divisor) * top);
  }

  // Prescaler cuya frecuencia resultante queda mas cerca de la pedida.
  // Devuelve los bits listos para escribir en TCCRnB. Funcion pura: es el
  // punto de entrada de los tests, ya que el registro AVR en si no es
  // observable en host.
  inline uint8_t selectPrescalerBits(uint16_t frequency, uint8_t resolution) {
    uint8_t bestBits = 0b011;
    float bestDiff = -1.0f;

    for (uint8_t i = 0; i < PrescalerCount; i++) {
      float diff = fabs(frequencyFor(Prescalers[i].divisor, resolution) - static_cast<float>(frequency));
      if (bestDiff < 0.0f || diff < bestDiff) {
        bestDiff = diff;
        bestBits = Prescalers[i].bits;
      }
    }

    return bestBits;
  }

  // Que timer gobierna cada pin PWM del Mega2560.
  //
  // Quedan deliberadamente afuera (devuelven None, o sea "este driver no
  // toca la frecuencia de ese pin", aunque analogWrite() ahi siga dando
  // PWM real):
  //   - Timer0 (pines 4 y 13): es el que cuenta millis()/micros()/delay().
  //     Cambiarle el prescaler descalibra todo el tiempo del sistema, y el
  //     Timer del proyecto se apoya en millis() para cada tarea periodica.
  //   - Timer2 (pines 9 y 10): tiene 7 prescalers (suma /32 y /128), asi
  //     que la tabla de Prescalers de arriba le asignaria bits que
  //     significan otro divisor.
  // Los pines analogicos A0..A15 no tienen timer asociado: ahi
  // analogWrite() degrada a un digitalWrite binario, no es PWM.
  inline PwmTimer timerForPin(uint8_t pin) {
    switch (pin) {
      case 11: case 12:           return PwmTimer::Timer1;
      case 2:  case 3:  case 5:   return PwmTimer::Timer3;
      case 6:  case 7:  case 8:   return PwmTimer::Timer4;
      case 44: case 45: case 46:  return PwmTimer::Timer5;
      default:                    return PwmTimer::None;
    }
  }

};

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

// Ajusta el prescaler del timer que gobierna _pin al valor cuya frecuencia
// resultante queda mas cerca de _frequency (ver PwmTiming). Es un no-op en
// los pines que timerForPin() no cubre: ahi analogWrite() sigue dando PWM,
// pero a la frecuencia que dejo el core, sin que este driver la toque.
//
// OJO -- la frecuencia es POR TIMER, no por pin: los 3 pines de un mismo
// timer comparten prescaler. Con varios PwmDriver sobre el mismo timer
// (p.ej. dos bobinas en 44 y 45), el ultimo enable() define la frecuencia
// de todos, sin aviso. Si dos canales necesitan frecuencias distintas,
// tienen que estar en timers distintos.
//
// Se llama desde enable(), no desde el constructor: PwmDriver es un objeto
// global y su constructor corre antes que el init() de Arduino, que
// pisaria cualquier registro de timer tocado antes de tiempo.
inline void PwmDriver::applyFrequency() {
  PwmTimer timer = PwmTiming::timerForPin(_pin);
  if (timer == PwmTimer::None) return;

  uint8_t bits = PwmTiming::selectPrescalerBits(_frequency, _resolution);

#ifndef UNIT_TEST
  switch (timer) {
    case PwmTimer::Timer1: TCCR1B = (TCCR1B & 0xF8) | bits; break;
    case PwmTimer::Timer3: TCCR3B = (TCCR3B & 0xF8) | bits; break;
    case PwmTimer::Timer4: TCCR4B = (TCCR4B & 0xF8) | bits; break;
    case PwmTimer::Timer5: TCCR5B = (TCCR5B & 0xF8) | bits; break;
    case PwmTimer::None:   break;
  }
#else
  (void)bits;
#endif
}