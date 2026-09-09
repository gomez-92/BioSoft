#pragma once

#include <Arduino.h>

// Regulador de intensidad de CEM. Combina dos mecanismos MUTUAMENTE
// EXCLUYENTES en cada update() (rama if/else sobre el mismo `error`, ver
// mas abajo):
//   1) Paso proporcional continuo: si |error| > deadBand, mueve _output
//      por `kp * error`, clampeado a +-maxStep. Es lo que hace la mayor
//      parte del trabajo, y ahora escala con la magnitud del desvio en
//      vez de saltar entre dos velocidades fijas (ver MOD-011 mas abajo).
//   2) Integral lenta condicional: solo se evalua cuando el paso de arriba
//      NO corrio en esta misma llamada (|error| <= deadBand, "asentado")
//      pero la medicion sigue variando mas de lo esperado dentro de una
//      ventana reciente -- indicio de drift que el paso proporcional no
//      corrige porque el error promedio ya esta dentro de la deadband.
//
// Bug corregido (auditoria MOD-011, 2026-08-24): antes, el gate de la
// integral comparaba _output contra un _lastOutput que se resincronizaba
// con _output al final de CADA llamada -- por lo que, a partir de la 2da
// llamada, esa comparacion daba siempre "no cambio" sin importar si el
// paso proporcional habia corrido o no, y el if de la integral corria
// ANTES del if del paso proporcional (bloques independientes, no
// if/else), asi que ambos podian aplicar en la misma llamada. Se
// reemplazo por un unico if/else sobre el mismo `error` de esta llamada:
// asi la exclusion mutua es estructural, no depende de recordar estado
// entre llamadas.
//
// Paso proporcional discreto -> continuo (auditoria MOD-011, 2026-09-01):
// el paso proporcional original saltaba entre dos velocidades fijas
// (coarseStep si |error| > 0.1, fineStep si no) sin relacion con la
// magnitud real del error dentro de cada banda -- un error de 0.011 y uno
// de 0.099 producian el mismo salto de fineStep, y ese salto se repetia
// entero en cada llamada a update() mientras el error siguiera fuera de
// la deadBand. Eso se sentia como un duty que "rebota" ante la minima
// desviacion en vez de acercarse suavemente. Reemplazado por un paso
// proporcional real (kp * error, clampeado a maxStep) que escala de forma
// continua con el error.
class FieldController {
  public:

    struct Config {
        float outputMin = 0.0f;
        float outputMax = 1.0f;
        float kp = 0.1f;            // ganancia proporcional: step = kp * error
        float maxStep = 0.05f;      // clamp del paso proporcional por llamada a update()
        // Tiempo (segundos) que update() ASUME que paso desde la llamada
        // anterior -- no se mide con millis(), es un valor fijo usado para
        // escalar el termino integral. Tiene que coincidir con la cadencia
        // real de llamadas a update() o integralGain no tiene el efecto
        // esperado.
        //
        // NO fijarlo a mano: usar makeFieldControllerConfig(), que lo deriva
        // del intervalo de medicion. Este default de 0.2s existe solo para
        // que un Config construido suelto (tests) tenga algo razonable; en
        // el firmware real lo pisa la derivacion.
        float sampleTime = 0.2f;
        float deadBand = 0.01f;        // |error| por debajo de esto: no se toca el output
        float driftThreshold = 0.02f;  // variacion (max-min) en driftWindow que dispara la integral lenta
        uint8_t driftWindow = 5;       // cantidad de muestras recientes que mira windowVariation()
        float integralGain = 0.001f;
        float integralLimit = 0.2f;    // clamp de _slowIntegral, no del output final
    };

    explicit FieldController(const Config& config);
    void setSetpoint(float field);
    float update(float measuredField);
    float getOutput() const;
    void reset();

  private:
    static constexpr uint8_t MAX_WINDOW_SIZE = 10;
    Config _config;
    float _setpoint;
    float _output;
    float _slowIntegral;
    float _window[MAX_WINDOW_SIZE];
    uint8_t _windowIndex;
    bool _windowFilled;
    void updateWindow(float value);
    float windowVariation() const;
    float clamp(float value, float minimum, float maximum) const;
};

// Construye un Config con sampleTime derivado de la cadencia REAL con que se
// va a llamar a update() -- en el Mega, Intervals::MeasureMagneticField.
//
// Existe para que ese valor tenga una sola fuente de verdad. Antes el
// sampleTime era un literal (0.2s) que no coincidia con el intervalo real
// (500ms): el termino integral corria escalado a menos de la mitad de lo
// configurado, y nada lo delataba porque las dos constantes vivian en
// archivos distintos y ninguna referenciaba a la otra. Derivandolo, cambiar
// la cadencia de medicion ajusta el regulador solo.
//
// Los parametros de calibracion (kp, maxStep, deadBand) NO se tocan acá: se
// fijan en el .ino, que es el punto donde los va a pisar la configuracion de
// la SD cuando exista. Ver docs/protocolo-calibracion-intensidad.md para
// como se obtienen -- kp en particular se calcula como alfa/K, no se mide.
inline FieldController::Config makeFieldControllerConfig(unsigned long sampleIntervalMs) {
  FieldController::Config config;
  config.sampleTime = static_cast<float>(sampleIntervalMs) / 1000.0f;
  return config;
}

inline FieldController::FieldController(const Config& config) : _config(config) {
    reset();
}

inline void FieldController::setSetpoint(float field) {
    _setpoint = field;
}

inline float FieldController::update(float measuredField) {
  updateWindow(measuredField);
  float error = _setpoint - measuredField;
  float variation = windowVariation();

  // Paso proporcional continuo (escala con la magnitud del error, clampeado
  // a maxStep) e integral lenta (solo si hay ventana llena y la medicion
  // sigue variando mas que driftThreshold) son ramas de un mismo if/else:
  // nunca pueden aplicar los dos en la misma llamada a update().
  if (fabs(error) > _config.deadBand) {
      float step = clamp(_config.kp * error, -_config.maxStep, _config.maxStep);
      _output += step;
  } else if (_windowFilled && variation > _config.driftThreshold) {
    _slowIntegral += error * _config.sampleTime * _config.integralGain;
    _slowIntegral = clamp(_slowIntegral, -_config.integralLimit, _config.integralLimit);
    _output += _slowIntegral;
  }

  _output = clamp(_output, _config.outputMin, _config.outputMax);
  return _output;
}

inline float FieldController::getOutput() const {
    return _output;
}

inline void FieldController::reset() {
  _setpoint = 0.0f;
  _output = 0.0f;
  _slowIntegral = 0.0f;
  _windowIndex = 0;
  _windowFilled = false;

  for (uint8_t i = 0; i < MAX_WINDOW_SIZE; i++) {
    _window[i] = 0.0f;
  }
}

inline void FieldController::updateWindow(float value) {
  _window[_windowIndex] = value;
  _windowIndex++;
  if (_windowIndex >= _config.driftWindow) {
      _windowIndex = 0;
      _windowFilled = true;
  }
}

// Rango (max-min) de las ultimas driftWindow muestras -- mide inestabilidad
// de la medicion en si, no el error contra el setpoint. Con menos de 2
// muestras acumuladas no hay rango que calcular, devuelve 0 (no dispara la
// integral lenta en update()).
inline float FieldController::windowVariation() const {
  uint8_t samples = _windowFilled ? _config.driftWindow : _windowIndex;
  if (samples < 2) return 0.0f;
  float minimum = _window[0];
  float maximum = _window[0];

  for (uint8_t i = 1; i < samples; i++) {
    if (_window[i] < minimum)
      minimum = _window[i];

    if (_window[i] > maximum)
      maximum = _window[i];
  }
  return maximum - minimum;
}

inline float FieldController::clamp(float value, float minimum, float maximum) const {
  if (value < minimum)
    return minimum;

  if (value > maximum)
    return maximum;

  return value;
}
