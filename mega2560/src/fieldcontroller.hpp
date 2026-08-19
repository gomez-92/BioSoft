#pragma once

#include <Arduino.h>

class FieldController {
  public:

    struct Config {
        float outputMin = 0.0f;
        float outputMax = 1.0f;
        float coarseStep = 0.05f;
        float fineStep = 0.01f;
        float sampleTime = 0.2f;
        float deadBand = 0.01f;
        float driftThreshold = 0.02f;
        uint8_t driftWindow = 5;
        float integralGain = 0.001f;
        float integralLimit = 0.2f;
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
    float _lastOutput;
    float _window[MAX_WINDOW_SIZE];
    uint8_t _windowIndex;
    bool _windowFilled;
    void updateWindow(float value);
    float windowVariation() const;
    float clamp(float value, float minimum, float maximum) const;
};

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
  bool outputChanged = fabs(_output - _lastOutput) > 0.0001f;

  if (_windowFilled && !outputChanged && variation > _config.driftThreshold) {
    _slowIntegral += error * _config.sampleTime * _config.integralGain;
    _slowIntegral = clamp(_slowIntegral, -_config.integralLimit, _config.integralLimit);
    _output += _slowIntegral;
  }

  if (fabs(error) > _config.deadBand) {
      float step = (fabs(error) > 0.1f) ? _config.coarseStep : _config.fineStep;
      _output += (error > 0.0f) ? step : -step;
  }

  _output = clamp(_output, _config.outputMin, _config.outputMax);
  _lastOutput = _output;
  return _output;
}

inline float FieldController::getOutput() const {
    return _output;
}

inline void FieldController::reset() {
  _setpoint = 0.0f;
  _output = 0.0f;
  _slowIntegral = 0.0f;
  _lastOutput = -1.0f;
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
