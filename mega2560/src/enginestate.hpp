#pragma once

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
  Serial.print("_engineState.setState(");
  Serial.print(state);
  Serial.println(")");

  _state = state;
  if(_listener == nullptr) {
    Serial.println("_listener es null");
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