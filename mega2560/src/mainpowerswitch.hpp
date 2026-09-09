#pragma once
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

// Interruptor general de la etapa de potencia: habilita o corta TODO lo que
// llega a las bobinas. Es el corte de seguridad remoto (conector de 3 pines
// GND/VDD/enable), distinto de la habilitacion individual por bobina que
// vive en CoilChannel.
//
// Envuelve un unico Relay en vez de ser un contenedor de N. Antes esto era
// RelayManager (addRelay/beginAll/openAll/closeAll sobre un array de hasta
// 10), pero por diseno hay UN solo rele: un "manager de N" para N=1 fijo es
// indireccion sin concepto -- el mismo criterio por el que CoilExcitation no
// se llama Manager, y por el que SafetyMargins no es una clase.
//
// Lo que si aporta es vocabulario: Engine pide enable()/disable() sobre el
// interruptor general y deja de razonar en contactos abiertos y cerrados,
// que es un detalle del rele y no del experimento.
//
// El blanking de Relay sigue vigente por debajo: un enable() o disable()
// pedido dentro de los 200ms del cambio anterior es un no-op SILENCIOSO. Si
// hace falta confirmar que se aplico, consultar isEnabled() despues.
class MainPowerSwitch {
  private:
    Relay& _relay;

  public:
    explicit MainPowerSwitch(Relay& relay);
    void begin();
    void enable();          // Habilita la etapa de potencia (rele cerrado)
    void disable();         // Corta la etapa de potencia (rele abierto)
    bool isEnabled();
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

inline MainPowerSwitch::MainPowerSwitch(Relay& relay) : _relay(relay) {}

inline void MainPowerSwitch::begin() {
  _relay.begin();
}

inline void MainPowerSwitch::enable() {
  _relay.close();
}

inline void MainPowerSwitch::disable() {
  _relay.open();
}

inline bool MainPowerSwitch::isEnabled() {
  return _relay.isClosed();
}
