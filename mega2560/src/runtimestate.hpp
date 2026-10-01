#pragma once

#include <Arduino.h>
#include <string.h>

class RuntimeStateListener {
  public:
    virtual void onTarget() = 0;
    virtual void onProgress() = 0;
    virtual void onResult() = 0;
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

  // Los campos de abajo son la MISMA informacion que ya venia armada dentro
  // de `description`, pero en crudo. Existen porque la pantalla Resultado
  // del ESP32 tiene que redactar su propio texto -- distinto segun la
  // fuente, el tipo de regla y cuantas alertas quedaron activas -- y sacarlo
  // de la prosa de `description` significaria parsear castellano.
  // `description` sigue viajando: es el texto de fallback y lo que se lee en
  // el log del Mega.

  // Solo con reason == "critical": que fuente corto el experimento, con que
  // tipo de regla ("critical", "streak" o "frequency" -- cualquiera de las
  // tres corta al llegar a SU maxEvents) y en que cuenta.
  char source[16] = "";
  char type[12] = "";
  uint16_t count = 0;
  uint16_t limit = 0;

  // Solo con reason == "stopped": true si la orden vino del pulsador fisico
  // de emergencia y no del boton Detener de la pantalla. A los fines del
  // experimento es lo mismo, pero no es lo mismo para quien despues lee el
  // resultado.
  bool fromEmergency = false;

  // Solo con reason == "refused": el experimento ni siquiera arranco, y esto
  // dice por que, como codigo corto ("nomap", "balance").
  // La pantalla Resultado de la ESP32 redacta el texto con el codigo; viaja
  // ademas de `description` por la misma razon que los campos de arriba.
  char cause[12] = "";
};

class RuntimeState {
  private:
    TargetData _target;
    ProgressData _progress;
    ResultData _result;
    unsigned long _start;
    RuntimeStateListener* _listener;

  public:
    RuntimeState();
    void reset();
    const TargetData& target() const;
    const ProgressData& progress() const;
    const ResultData& result() const;
    void setTarget(float cemTarget, int frequencyTarget, unsigned long durationTarget);
    void updateProgress();
    void setResult(const char* reason, const char* description);
    // Version canonica: la de dos argumentos arma un ResultData sin los
    // campos crudos y delega aca.
    void setResult(const ResultData& data);
    void setListener(RuntimeStateListener* listener);

  private:
    void writeElapsed(unsigned long elapsedMs);
    void notifyTarget();
    void notifyProgress();
    void notifyResult();
};

inline RuntimeState::RuntimeState() : _start(0), _listener(nullptr) {}

inline void RuntimeState::reset() {
  _target = {};
  _progress = {};
  _result = {};
  strcpy(_progress.elapsed, "00:00:00");
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
  ResultData data;
  strncpy(data.reason, reason, sizeof(data.reason) - 1);
  data.reason[sizeof(data.reason) - 1] = '\0';
  strncpy(data.description, description, sizeof(data.description) - 1);
  data.description[sizeof(data.description) - 1] = '\0';
  setResult(data);
}

inline void RuntimeState::setResult(const ResultData& data) {
  // La guarda de "mismo resultado, no notifiques" mira razon y descripcion:
  // dos cortes con la misma causa y el mismo texto SON el mismo resultado,
  // y los campos crudos se derivan de ellos.
  if (strcmp(_result.reason, data.reason) == 0 &&
      strcmp(_result.description, data.description) == 0) { return; }

  _result = data;
  notifyResult();
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

inline void RuntimeState::writeElapsed(unsigned long elapsedMs) {
  unsigned long totalSeconds = elapsedMs / 1000;
  unsigned int hours = totalSeconds / 3600;
  unsigned int minutes = (totalSeconds % 3600) / 60;
  unsigned int seconds = totalSeconds % 60;
  snprintf(_progress.elapsed, sizeof(_progress.elapsed), "%02u:%02u:%02u", hours, minutes, seconds);
}