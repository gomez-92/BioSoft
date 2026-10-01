#pragma once
#include <Arduino.h>

class EmergencyButtonListener {
  public:
    virtual void onEmergencyButtonPressed() = 0;
};

class EmergencyButton {
  public:
    explicit EmergencyButton(uint8_t pin, bool useInternalPullup = true, unsigned long debounceMs = 30);
    void begin();
    void update();
    void setListener(EmergencyButtonListener* listener);
    // Nivel crudo, sin debounce ni flanco ni callback: lo usan las esperas
    // largas para abandonar lo que hacen. No consume el flanco que update()
    // despues convierte en onEmergencyButtonPressed().
    bool isPressed() const { return _readPressed(); }

  private:
    uint8_t _pin;
    bool _useInternalPullup;
    unsigned long _debounceMs;
    bool _lastPressed;
    unsigned long _lastEdgeTime;
    EmergencyButtonListener* _listener;
    bool _readPressed() const;
};

inline EmergencyButton::EmergencyButton(uint8_t pin, bool useInternalPullup, unsigned long debounceMs)
  : _pin(pin), _useInternalPullup(useInternalPullup), _debounceMs(debounceMs),
    _lastPressed(false), _lastEdgeTime(0), _listener(nullptr) {}

inline void EmergencyButton::begin() {
  pinMode(_pin, _useInternalPullup ? INPUT_PULLUP : INPUT);
  _lastPressed = _readPressed();
}

// Con pull-up interno, el pulsador conecta a GND al presionar -> LOW = presionado.
// Sin pull-up interno, se asume logica activa-alta (HIGH = presionado).
inline bool EmergencyButton::_readPressed() const {
  int level = digitalRead(_pin);
  return _useInternalPullup ? (level == LOW) : (level == HIGH);
}

inline void EmergencyButton::update() {
  bool pressed = _readPressed();

  // Flanco no-presionado -> presionado, con debounce por tiempo (un
  // pulsador mecanico puede rebotar varias veces en pocos ms; sin esto,
  // el loop -que corre en microsegundos- podria disparar el callback
  // multiples veces por una sola pulsacion real).
  if (pressed && !_lastPressed) {
    unsigned long now = millis();
    if (now - _lastEdgeTime >= _debounceMs) {
      _lastEdgeTime = now;
      if (_listener != nullptr) {
        _listener->onEmergencyButtonPressed();
      }
    }
  }

  _lastPressed = pressed;
}

inline void EmergencyButton::setListener(EmergencyButtonListener* listener) {
  _listener = listener;
}
