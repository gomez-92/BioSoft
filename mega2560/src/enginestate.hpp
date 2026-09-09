#pragma once
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro). Apagado (2026-09-01) durante bring-up INT-001
// centrado en el circuito de campo magnetico -- transiciones de estado
// genericas, no aportan a esa depuracion.
constexpr bool DEBUG_ENGINESTATE = false;

namespace State {
  constexpr const char* Idle      = "idle";
  constexpr const char* Ready     = "ready";
  constexpr const char* Running   = "running";
  constexpr const char* Finished  = "finished";
};

class EngineStateListener {
  public:
    virtual void onReady() = 0;
    virtual void onStart() = 0;
    virtual void onFinish() = 0;    
};

class EngineState {
  private:
    const char* _state;
    EngineStateListener* _listener;
  public:
    EngineState();
    const char* getState() const;
    void setState(const char* state);
    void setListener(EngineStateListener* listener);
};

inline EngineState::EngineState() : _state(State::Idle), _listener(nullptr) {}

inline const char* EngineState::getState() const {
  return _state;
}

inline void EngineState::setState(const char* state) {
  DEBUG_PRINT(DEBUG_ENGINESTATE, "_engineState.setState(");
  DEBUG_PRINT(DEBUG_ENGINESTATE, state);
  DEBUG_PRINTLN(DEBUG_ENGINESTATE, ")");

  _state = state;
  if(_listener == nullptr) {
    DEBUG_PRINTLN(DEBUG_ENGINESTATE, "_listener es null");
    return;
  }
  if(_state == State::Ready) {
    _listener->onReady();
  }
  else if(_state == State::Running) {
    _listener->onStart();
  }
  else if(_state == State::Finished) {
    _listener->onFinish();
  }
}

inline void EngineState::setListener(EngineStateListener* listener) {
  _listener = listener;
}