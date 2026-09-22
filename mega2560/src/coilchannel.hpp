#pragma once
#include <Arduino.h>
#include "pwmdriver.hpp"
#include "debugconfig.hpp"

#define MAX_COIL_CHANNELS 4

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_COILCHANNEL = true;

// =====================================================
// Un canal = una bobina
// =====================================================
//
// Dueño de las dos señales que son 1:1 con una bobina: su PWM de intensidad
// y su habilitacion. La senoidal y el modo de experimento NO viven acá --
// son por generador y los maneja CoilExcitation.
//
// El enable es un pinMode+digitalWrite pelado, sin blanking: del otro lado
// hay una entrada digital a una etapa de estado solido, no un contacto
// mecanico. Por eso NO reutiliza la clase Relay, cuya proteccion de 200ms
// existe para un rele fisico y acá solo agregaria latencia sin motivo.
//
// FACTOR DE CALIBRACION
// ---------------------
// Hay un solo magnetometro en todo el sistema (por diseño, no por
// omision), asi que no existe realimentacion por bobina y no puede haber un
// lazo cerrado por canal. El control de intensidad "individual" es en
// realidad un unico lazo (FieldController) que calcula un duty comun, y
// cada canal lo escala por su propio factor:
//
//     duty_canal = duty_comun * factor
//
// El factor sale de la calibracion en banco: se mide cuanto duty necesita
// cada bobina para dar el mismo campo, y el cociente contra la bobina de
// referencia es ese numero. Ver docs/protocolo-calibracion-intensidad.md.
// Un factor de 1.0 (el default) significa "sin corregir", que es lo
// correcto mientras no haya calibracion real.
class CoilChannel {
  public:
    CoilChannel(PwmDriver& pwm, uint8_t enablePin, const char* name, uint8_t activeLevel = HIGH);

    void begin();
    void enable();
    void disable();
    bool isEnabled() const;

    // Duty COMUN que calcula el lazo (0..1). El canal le aplica su factor
    // antes de escribirlo al PWM.
    void setIntensity(float duty);

    // Rango 0.1-5.0 (docs/config-schema.md, seccion "coils"): fuera de eso,
    // o cero o negativo, se rechaza y devuelve false sin tocar el factor
    // actual.
    bool setCalibrationFactor(float factor);
    float calibrationFactor() const;
    // Ultimo duty REALMENTE escrito al PWM (0..1): el comun por el factor de
    // esta bobina, ya saturado. No es el que pidio FieldController -- esa es
    // justamente la diferencia que la pantalla En curso tiene que mostrar
    // cuando una bobina satura y queda corta.
    float appliedDuty() const;

    const char* getName() const;

  private:
    PwmDriver& _pwm;
    uint8_t _enablePin;
    const char* _name;
    uint8_t _activeLevel;
    float _factor;
    bool _enabled;
    float _appliedDuty;
};

inline CoilChannel::CoilChannel(PwmDriver& pwm, uint8_t enablePin, const char* name, uint8_t activeLevel)
  : _pwm(pwm), _enablePin(enablePin), _name(name), _activeLevel(activeLevel), _factor(1.0f), _enabled(false), _appliedDuty(0.0f) {
}

inline void CoilChannel::begin() {
  pinMode(_enablePin, OUTPUT);
  disable();
}

inline void CoilChannel::enable() {
  digitalWrite(_enablePin, _activeLevel);
  _pwm.enable();
  _enabled = true;
}

inline void CoilChannel::disable() {
  uint8_t inactiveLevel = (_activeLevel == HIGH) ? LOW : HIGH;
  digitalWrite(_enablePin, inactiveLevel);
  _pwm.disable();
  _enabled = false;
  // Con el PWM apagado el duty efectivo es 0. Dejar el ultimo valor haria
  // que En curso siguiera mostrando la intensidad de una bobina apagada.
  _appliedDuty = 0.0f;
}

inline bool CoilChannel::isEnabled() const {
  return _enabled;
}

// El clamp a 1.0 no es silencioso a proposito: que el factor empuje el duty
// fuera de rango significa que esta bobina NO tiene margen para alcanzar la
// consigna, y va a entregar menos campo que las demas sin que nada mas lo
// delate. Durante la calibracion es exactamente el sintoma que hay que ver
// (revisar hardware o bajar la consigna); en produccion, un aviso de que el
// experimento no esta corriendo en la condicion que se pidio.
inline void CoilChannel::setIntensity(float duty) {
  float scaled = duty * _factor;

  if (scaled > 1.0f) {
    DEBUG_PRINT(DEBUG_COILCHANNEL, F("[COIL] SATURA "));
    DEBUG_PRINT(DEBUG_COILCHANNEL, _name);
    DEBUG_PRINT(DEBUG_COILCHANNEL, F(": duty comun "));
    DEBUG_PRINT(DEBUG_COILCHANNEL, duty, 3);
    DEBUG_PRINT(DEBUG_COILCHANNEL, F(" x factor "));
    DEBUG_PRINT(DEBUG_COILCHANNEL, _factor, 3);
    DEBUG_PRINT(DEBUG_COILCHANNEL, F(" = "));
    DEBUG_PRINT(DEBUG_COILCHANNEL, scaled, 3);
    DEBUG_PRINTLN(DEBUG_COILCHANNEL, F(" -- se limita a 1.000, esta bobina queda corta"));
    scaled = 1.0f;
  }

  // PwmDriver ya clampea por abajo, pero un factor negativo mal cargado
  // seria un error de configuracion que conviene no propagar.
  if (scaled < 0.0f) scaled = 0.0f;

  _appliedDuty = scaled;
  _pwm.write(scaled);
}

inline float CoilChannel::appliedDuty() const {
  return _appliedDuty;
}

inline bool CoilChannel::setCalibrationFactor(float factor) {
  if (factor < 0.1f || factor > 5.0f) {
    DEBUG_PRINT(DEBUG_COILCHANNEL, F("[COIL] Factor invalido para "));
    DEBUG_PRINT(DEBUG_COILCHANNEL, _name);
    DEBUG_PRINTLN(DEBUG_COILCHANNEL, F(" -- se ignora"));
    return false;
  }
  _factor = factor;
  return true;
}

inline float CoilChannel::calibrationFactor() const {
  return _factor;
}

inline const char* CoilChannel::getName() const {
  return _name;
}

// =====================================================
// Conjunto de canales
// =====================================================
//
// Todas las operaciones del experimento son colectivas: se habilitan o
// cortan todas las bobinas a la vez, y todas reciben el mismo duty comun
// (cada una lo escala por su factor). Engine mantiene una sola referencia
// en vez de cuatro.
//
// Acá SI corresponde un contenedor de N, a diferencia de RelayManager (que
// se colapso en MainPowerSwitch): ahi N era 1 fijo por diseño, acá son 4
// bobinas reales y las operaciones son sobre el conjunto. Mismo patron que
// MagnetometerManager o ThermometerManager.
class CoilChannels {
  public:
    CoilChannels();

    bool addChannel(CoilChannel* channel);
    uint8_t count() const;
    CoilChannel* getChannel(uint8_t index);
    // Busca por nombre exacto (strcmp) -- lo usa Engine para aplicar
    // config_coil, que identifica el canal por `name`, no por indice.
    // nullptr si ninguno matchea.
    CoilChannel* findByName(const char* name);
    void clearChannels();

    void beginAll();
    void enableAll();
    void disableAll();

    // Reparte el duty comun del lazo a todos los canales habilitados.
    void writeAll(float duty);

  private:
    CoilChannel* _channels[MAX_COIL_CHANNELS];
    uint8_t _count;
};

inline CoilChannels::CoilChannels() : _count(0) {
  for (uint8_t i = 0; i < MAX_COIL_CHANNELS; i++) {
    _channels[i] = nullptr;
  }
}

inline bool CoilChannels::addChannel(CoilChannel* channel) {
  if (_count >= MAX_COIL_CHANNELS) {
    DEBUG_PRINTLN(DEBUG_COILCHANNEL, F("[COIL] ERROR: maximo de canales alcanzado"));
    return false;
  }
  if (channel == nullptr) return false;

  _channels[_count] = channel;
  _count++;
  return true;
}

inline uint8_t CoilChannels::count() const {
  return _count;
}

inline CoilChannel* CoilChannels::getChannel(uint8_t index) {
  if (index >= _count) return nullptr;
  return _channels[index];
}

inline CoilChannel* CoilChannels::findByName(const char* name) {
  if (name == nullptr) return nullptr;
  for (uint8_t i = 0; i < _count; i++) {
    if (_channels[i] && strcmp(_channels[i]->getName(), name) == 0) {
      return _channels[i];
    }
  }
  return nullptr;
}

inline void CoilChannels::clearChannels() {
  for (uint8_t i = 0; i < MAX_COIL_CHANNELS; i++) {
    _channels[i] = nullptr;
  }
  _count = 0;
}

inline void CoilChannels::beginAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_channels[i]) _channels[i]->begin();
  }
}

inline void CoilChannels::enableAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_channels[i]) _channels[i]->enable();
  }
}

inline void CoilChannels::disableAll() {
  for (uint8_t i = 0; i < _count; i++) {
    if (_channels[i]) _channels[i]->disable();
  }
}

// Un canal deshabilitado igual recibe el valor: PwmDriver::write() fuerza 0
// mientras esta disabled, asi que no hay riesgo de que salga señal. Se le
// pasa igual para que al habilitarlo no arranque con un duty viejo.
inline void CoilChannels::writeAll(float duty) {
  for (uint8_t i = 0; i < _count; i++) {
    if (_channels[i]) _channels[i]->setIntensity(duty);
  }
}
