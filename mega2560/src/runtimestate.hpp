#pragma once

#include <Arduino.h>
#include <string.h>

class RuntimeStateListener {
  public:
    virtual void onTarget() = 0;
    virtual void onProgress() = 0;
    virtual void onResult() = 0;
    virtual void onHealth() = 0;
};

struct TargetData {
  float cemTarget = 0.0f;
  int frequencyTarget = 0;
  unsigned long durationTarget = 0;
};

struct ProgressData {
  float progress = 0.0f;
  char elapsed[16] = "00:00:00";
};

struct ResultData {
  char reason[16] = "";
  char description[64] = "";
};

struct HealthData {
  char health[16] = "NORMAL";
};

class RuntimeState {
  private:
    TargetData _target;
    ProgressData _progress;
    ResultData _result;
    HealthData _health;
    unsigned long _start;
    RuntimeStateListener* _listener;

  public:
    RuntimeState();
    void reset();
    const TargetData& target() const;
    const ProgressData& progress() const;
    const ResultData& result() const;
    const HealthData& health() const;
    void setTarget(float cemTarget, int frequencyTarget, unsigned long durationTarget);
    void updateProgress();
    void setResult(const char* reason, const char* description);
    void setHealth(const char* health);
    void setListener(RuntimeStateListener* listener);

  private:
    void writeElapsed(unsigned long elapsedMs);
    void notifyTarget();
    void notifyProgress();
    void notifyResult();
    void notifyHealth();
};

inline RuntimeState::RuntimeState() : _start(0), _listener(nullptr) {}

inline void RuntimeState::reset() {
  _target = {};
  _progress = {};
  _result = {};
  _health = {};
  strcpy(_progress.elapsed, "00:00:00");
  strcpy(_health.health, "NORMAL");
  _start = millis();
}

inline const TargetData& RuntimeState::target() const {
  return _target;
}

inline const ProgressData& RuntimeState::progress() const {
  return _progress;
}

inline const ResultData& RuntimeState::result() const {
  return _result;
}

inline const HealthData& RuntimeState::health() const {
  return _health;
}

inline void RuntimeState::setTarget(float cemTarget, int frequencyTarget, unsigned long durationTarget) {
  if (_target.cemTarget == cemTarget && _target.frequencyTarget == frequencyTarget && _target.durationTarget == durationTarget) { return; }
  _target.cemTarget = cemTarget;
  _target.frequencyTarget = frequencyTarget;
  _target.durationTarget = durationTarget;
  notifyTarget();
}

inline void RuntimeState::updateProgress() {
  if (_target.durationTarget == 0) return;
  
  unsigned long elapsed = millis() - _start;
  float newProgress = (elapsed * 100.0f) / _target.durationTarget;
  
  if (newProgress > 100.0f)
    newProgress = 100.0f;
  
  if (newProgress == _progress.progress)
    return;
  
  _progress.progress = newProgress;
  writeElapsed(elapsed);
  notifyProgress();
}

inline void RuntimeState::setResult(const char* reason, const char* description) {
  if (strcmp(_result.reason, reason) == 0 && strcmp(_result.description, description) == 0) { return; }
  strncpy(_result.reason, reason, sizeof(_result.reason) - 1);
  _result.reason[sizeof(_result.reason) - 1] = '\0';
  strncpy(_result.description, description, sizeof(_result.description) - 1);
  _result.description[sizeof(_result.description) - 1] = '\0';
  notifyResult();
}

inline void RuntimeState::setHealth(const char* health) {
  if (strcmp(_health.health, health) == 0) return;
  strncpy(_health.health, health, sizeof(_health.health) - 1);
  _health.health[sizeof(_health.health) - 1] = '\0';
  notifyHealth();
}

inline void RuntimeState::setListener(RuntimeStateListener* listener) {
  _listener = listener;
}

inline void RuntimeState::notifyTarget() {
  if (_listener) _listener->onTarget();
}

inline void RuntimeState::notifyProgress() {
  if (_listener) _listener->onProgress();
}

inline void RuntimeState::notifyResult() {
  if (_listener) _listener->onResult();
}

inline void RuntimeState::notifyHealth() {
  if (_listener) _listener->onHealth();
}

inline void RuntimeState::writeElapsed(unsigned long elapsedMs) {
  unsigned long totalSeconds = elapsedMs / 1000;
  unsigned int hours = totalSeconds / 3600;
  unsigned int minutes = (totalSeconds % 3600) / 60;
  unsigned int seconds = totalSeconds % 60;
  snprintf(_progress.elapsed, sizeof(_progress.elapsed), "%02u:%02u:%02u", hours, minutes, seconds);
}