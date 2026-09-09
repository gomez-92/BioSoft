#pragma once
#include <Arduino.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_COILEXCITATION = true;

// Contrato minimo que CoilExcitation necesita de un generador de
// senal. Existe por el mismo motivo que IMagnetometer en
// magnetometermanager.hpp: permite testear el manager en host con un doble,
// sin arrastrar AD9833.h/SPI. SignalGenerator la implementa.
class ISignalGenerator {
  public:
    virtual ~ISignalGenerator() = default;
    virtual void begin() = 0;
    virtual void setSineWave(float freq = 0) = 0;
    virtual void off() = 0;
};

// Modo de experimento. No es una propiedad de la senal sino del DISENO del
// experimento: define si el segundo par de bobinas suma al campo o lo
// cancela.
//
//   X    -- los dos grupos en fase: campo electromagnetico presente.
//   Null -- el grupo invertible recibe la senal desfasada 180 grados, de
//           modo que los campos se cancelan. Es la condicion del GRUPO
//           CONTROL: mismo aparato, mismo ruido, misma vibracion, sin campo
//           neto.
//
// Los grupos son ALTERNADOS, no pares contiguos: bobinas 1 y 3 fijas, 2 y 4
// invertibles. Asi cualquier subconjunto de bobinas consecutivas tiene una
// de cada grupo, y el campo nulo se puede ejercitar con solo dos montadas
// (ver SoftMega2560.ino, donde el default son BOB1 y BOB2).
enum class FieldMode : uint8_t { X, Null };

// Unico punto del firmware que decide QUE senal reciben las bobinas.
//
// Encapsula dos cosas que hasta ahora Engine hacia a mano: prender/apagar
// la senoidal del generador, y seleccionar el modo de experimento. Engine
// pasa a pedir "arranca en modo nulo" en vez de saber que eso significa un
// pin en alto y un AD9833 configurado.
//
// La inversion de 180 grados NO se hace acá ni en ningun otro lado del
// firmware: es analogica (op-amp inversor) y este modulo solo mueve el
// multiplexor que elige entre la rama directa y la invertida. Por eso el
// "modo" es un unico pin digital y no hay ninguna secuencia critica que
// respetar. El porque de esa arquitectura -- y por que NO son dos AD9833
// con fase programada por software -- esta en
// docs/adr/001-inversion-de-fase.md: dos generadores con osciladores
// independientes derivan en fase y el campo nulo dejaria de serlo a los
// pocos minutos, contaminando el grupo control sin que nada lo advierta.
class CoilExcitation {
  public:
    // nullLevel: nivel logico del pin selector que corresponde a campo
    // NULO, igual que el ActivationType de Relay. Depende de como quede
    // cableado el multiplexor, asi que se declara en el .ino y no se
    // asume acá.
    CoilExcitation(ISignalGenerator& generator, uint8_t modePin, uint8_t nullLevel = HIGH);

    void begin();

    // Devuelve false y NO cambia nada si el experimento esta corriendo:
    // pasar de campo X a nulo (o al reves) a mitad de una corrida cambia
    // la condicion experimental de los animales que ya estan expuestos, lo
    // que invalida el experimento en curso. El modo se elige antes de
    // arrancar.
    bool setMode(FieldMode mode);
    FieldMode mode() const;

    void start(float frequency);
    void stop();
    bool isRunning() const;

  private:
    ISignalGenerator& _generator;
    uint8_t _modePin;
    uint8_t _nullLevel;
    FieldMode _mode;
    bool _running;

    void _applyMode();
};

// El constructor no toca hardware a proposito (mismo criterio que
// PwmDriver::applyFrequency): este objeto se declara global en el .ino y su
// constructor corre antes del init() de Arduino. El pinMode va en begin().
inline CoilExcitation::CoilExcitation(ISignalGenerator& generator, uint8_t modePin, uint8_t nullLevel)
  : _generator(generator), _modePin(modePin), _nullLevel(nullLevel), _mode(FieldMode::X), _running(false) {
}

inline void CoilExcitation::begin() {
  pinMode(_modePin, OUTPUT);
  // Deja el selector en un estado conocido desde el arranque en vez de
  // depender de como quede el pin al resetear. Sin senal del generador el
  // modo no tiene efecto, pero asi mode() nunca miente sobre el hardware.
  _applyMode();
  _generator.begin();
}

inline bool CoilExcitation::setMode(FieldMode mode) {
  if (_running) {
    DEBUG_PRINTLN(DEBUG_COILEXCITATION, F("[COIL] RECHAZADO: no se cambia el modo con el experimento en curso"));
    return false;
  }

  _mode = mode;
  _applyMode();
  return true;
}

inline FieldMode CoilExcitation::mode() const {
  return _mode;
}

inline void CoilExcitation::start(float frequency) {
  DEBUG_PRINT(DEBUG_COILEXCITATION, F("[COIL] Excitando bobinas en modo "));
  DEBUG_PRINT(DEBUG_COILEXCITATION, _mode == FieldMode::Null ? F("NULO (grupo control)") : F("X"));
  DEBUG_PRINT(DEBUG_COILEXCITATION, F(" a "));
  DEBUG_PRINT(DEBUG_COILEXCITATION, frequency);
  DEBUG_PRINTLN(DEBUG_COILEXCITATION, F(" Hz"));

  _generator.setSineWave(frequency);
  _running = true;
}

inline void CoilExcitation::stop() {
  DEBUG_PRINTLN(DEBUG_COILEXCITATION, F("[COIL] Cortando excitacion de bobinas"));
  _generator.off();
  _running = false;
}

inline bool CoilExcitation::isRunning() const {
  return _running;
}

inline void CoilExcitation::_applyMode() {
  uint8_t inactiveLevel = (_nullLevel == HIGH) ? LOW : HIGH;
  digitalWrite(_modePin, (_mode == FieldMode::Null) ? _nullLevel : inactiveLevel);
}
