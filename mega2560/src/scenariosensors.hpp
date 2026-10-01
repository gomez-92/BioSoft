#pragma once
#include <Arduino.h>
#include "scenario.hpp"
#include "magnetometermanager.hpp"
#include "thermometermanager.hpp"

// Los dos drivers de escenario: el mismo IMagnetometer / IThermometer que un
// sensor real, pero el valor sale de la senal de scenario.hpp. Engine los
// elige con `sensor: "scenario"` y en cada start les copia la senal de la
// tarjeta SD (setSignal) y los rangos de la corrida (setRanges), que son los
// que dan sentido a los niveles.

// La planta de CEM1 necesita el duty comun que calcula el lazo. Se pasa como
// puntero a funcion y no como referencia al FieldController para poder
// probarlo sin armar un controlador entero.
typedef float (*DutySource)();

class MagnetometerScenario : public IMagnetometer {
  private:
    const char* _name;
    DutySource _dutySource;
    ScenarioSignal _signal;
    ScenarioRanges _ranges;
    uint32_t _noiseState;
    float _magneticField;
    bool _isValid;

  public:
    MagnetometerScenario(const char* name, DutySource dutySource, uint32_t seed = 0x1234567u)
      : _name(name), _dutySource(dutySource), _noiseState(seed), _magneticField(0.0f), _isValid(false) {}

    void setSignal(const ScenarioSignal& signal) { _signal = signal; }
    void setRanges(const ScenarioRanges& ranges) { _ranges = ranges; }

    void begin() override { _isValid = true; }

    void update() override {
      float t = Scenario::secondsSinceStart(millis());
      _isValid = Scenario::validAt(_signal, t);
      if (!_isValid) return;
      float duty = (_dutySource != nullptr) ? _dutySource() : 0.0f;
      float level = Scenario::levelAt(_signal, t, Scenario::nextUnitNoise(_noiseState));
      _magneticField = Scenario::fieldValue(_signal, _ranges, level, duty);
      // Un magnetometro reporta modulo: un campo negativo (perturbacion
      // grande hacia abajo con el duty en 0) no tiene sentido fisico.
      if (_magneticField < 0.0f) _magneticField = 0.0f;
    }

    float getMagneticField() const override { return _magneticField; }
    bool isValid() const override { return _isValid; }
    const char* getName() const override { return _name; }
};

class ThermometerScenario : public IThermometer {
  private:
    const char* _name;
    ScenarioSignal _signal;
    ScenarioRanges _ranges;
    uint32_t _noiseState;
    float _temperature;
    bool _isValid;

  public:
    ThermometerScenario(const char* name, uint32_t seed = 0x7654321u)
      : _name(name), _noiseState(seed), _temperature(0.0f), _isValid(false) {}

    void setSignal(const ScenarioSignal& signal) { _signal = signal; }
    void setRanges(const ScenarioRanges& ranges) { _ranges = ranges; }

    void begin() override { _isValid = true; }

    void update() override {
      float t = Scenario::secondsSinceStart(millis());
      _isValid = Scenario::validAt(_signal, t);
      if (!_isValid) return;
      // La temperatura no tiene planta: setpointDuty se ignora.
      float level = Scenario::levelAt(_signal, t, Scenario::nextUnitNoise(_noiseState));
      _temperature = Scenario::levelToValue(_ranges, level);
    }

    float getTemperature() const override { return _temperature; }
    bool isValid() const override { return _isValid; }
    const char* getName() const override { return _name; }
};
