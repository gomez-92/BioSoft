#pragma once;
#include <Arduino.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro). Apagado (2026-09-01) durante bring-up INT-001
// centrado en el circuito de campo magnetico -- no aporta a esa depuracion.
constexpr bool DEBUG_RELAY = false;

const unsigned long BLANKING_TIME = 200; // ms protection time

class Relay {
  public:
    enum ActivationType { ACTIVE_HIGH, ACTIVE_LOW };
    
  private:
    int _pin;
    ActivationType _type;
    bool _state;            // true = closed(ON), false = open(OFF)
    unsigned long _lastChange;
  
  public:
    Relay(int pin, ActivationType type);
    void begin();           // Configures the pin as output and opens the relay (OFF)
    bool canToggle();
    void open();            // Relay open (OFF)
    void close();           // Relay closed (ON)
    void toggle();          // Switch current state
    bool isClosed();        // Returns true if ON
    bool isOpen();          // Returns true if OFF
  
  private:
    void applyState();

};

class RelayManager {
  private:
    static const int MAX_RELAYS = 10;   // Adjustable limit
    Relay* _relays[MAX_RELAYS];
    int _relayCount;

  public:
    RelayManager();
    bool addRelay(Relay* relay);   // Adds a relay to the manager
    int count() const;             // Number of relays
    Relay* getRelay(int index);    // Returns pointer to relay
    void beginAll();               // Calls begin() on all relays
    void openAll();                // Opens all
    void closeAll();               // Closes all
    void toggleAll();              // Toggles all
};

inline Relay::Relay(int pin, ActivationType type) : _pin(pin), _type(type), _state(false), _lastChange(0) {}

// No usa open() -- open() respeta el blanking, y con _lastChange en su
// valor inicial (0) un begin() llamado dentro de los primeros 200ms de
// millis() (tipico en setup()) quedaria bloqueado por canToggle() y el pin
// nunca se escribiria, dejando un rele ACTIVE_LOW fisicamente cerrado (ON)
// al arrancar pese a que begin() deberia garantizar OFF.
inline void Relay::begin() {
    pinMode(_pin, OUTPUT);
    _state = false;
    applyState();
    _lastChange = millis();
}

inline bool Relay::canToggle() {
    unsigned long now = millis();
    return (now - _lastChange >= BLANKING_TIME);
}

// open()/close()/toggle() son no-ops SILENCIOSOS (sin log, sin valor de
// retorno) si se llaman antes de que pase BLANKING_TIME desde el ultimo
// cambio -- proteccion de hardware contra conmutacion rapida. Quien llama
// no tiene forma de saber si el pedido realmente se aplico; si hace falta
// confirmarlo, consultar isClosed()/isOpen() despues.
inline void Relay::open() {
    if (canToggle()) {
        _state = false;
        applyState();
        _lastChange = millis();
    }
}

inline void Relay::close() {
    if (canToggle()) {
        _state = true;
        applyState();
        _lastChange = millis();
    }
}

inline void Relay::toggle() {
    if (canToggle()) {
        _state = !_state;
        applyState();
        _lastChange = millis();
    }
}

inline bool Relay::isClosed() {
    return _state;
}

inline bool Relay::isOpen() {
    return !_state;
}

inline void Relay::applyState() {
    if (_type == ACTIVE_HIGH) {
        digitalWrite(_pin, _state ? HIGH : LOW);
    } else { // ACTIVE_LOW
        digitalWrite(_pin, _state ? LOW : HIGH);
    }
}

inline RelayManager::RelayManager() : _relayCount(0) {
  for (int i = 0; i < MAX_RELAYS; i++) {
      _relays[i] = nullptr;
  }
}

inline bool RelayManager::addRelay(Relay* relay) {
  if (_relayCount >= MAX_RELAYS) {
      DEBUG_PRINTLN(DEBUG_RELAY, "RelayManager: Max relays reached");
      return false;
  }

  _relays[_relayCount] = relay;
  _relayCount++;
  return true;
}

inline int RelayManager::count() const {
  return _relayCount;
}

inline Relay* RelayManager::getRelay(int index) {
  if (index < 0 || index >= _relayCount) return nullptr;
  return _relays[index];
}

inline void RelayManager::beginAll() {
  for (int i = 0; i < _relayCount; i++)
    if(_relays[i]) _relays[i]->begin();
}

inline void RelayManager::openAll() {
  for (int i = 0; i < _relayCount; i++)
    if(_relays[i]) _relays[i]->open();
}

inline void RelayManager::closeAll() {
  for (int i = 0; i < _relayCount; i++)
    if(_relays[i]) _relays[i]->close();
}

inline void RelayManager::toggleAll() {
  for (int i = 0; i < _relayCount; i++)
    if(_relays[i]) _relays[i]->toggle();
}