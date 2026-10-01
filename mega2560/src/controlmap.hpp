#pragma once
#include <Arduino.h>
#include "coilexcitation.hpp"

// =====================================================
// Mapa de calibracion: intensidad x frecuencia -> duty y balance
// =====================================================
//
// Resultado de las corridas de banco (docs/protocolo-calibracion-intensidad.md):
// para cada par (intensidad de campo, frecuencia) que el operador puede elegir,
// el duty COMUN con que las bobinas dan ese campo y, para el campo nulo, el
// balance entre grupos de fase que deja el residuo en cero.
//
// Sirve a dos cosas:
//   - Campo X: el lazo arranca desde `duty` en vez de desde 0. Solo recorta lo
//     que falte, en vez de subir desde cero cada experimento.
//   - Campo nulo: las bobinas se excitan con ESE duty -- el mismo estres que en
//     campo X -- y el lazo de campo NO corre (el objetivo es 0, no hay nada que
//     regular hacia arriba). Sin un punto mapeado el campo nulo se rechaza: no
//     hay duty que aplicar, y un control "a ojo" seria un control de mentira.
//
// El balance NO es unico: depende de la intensidad y de la frecuencia (la
// diferencia entre bobinas cambia con la corriente y con la reactancia), por
// eso vive junto al duty en cada punto y no como un numero global.
//
// Llega por config_map desde la SD, un frame por punto. El mapa es DISPERSO y
// se indexa por VALOR (intensidad, frecuencia), nunca por posicion en los
// menus: las opciones de intensidad y de frecuencia son configurables desde la
// SD (hasta 3 cada una, tope de ConfigurationOptions en la ESP32), asi que las
// combinaciones posibles cambian de una tarjeta a otra, con un maximo de 9. Un punto sin combinacion en los menus es inofensivo; una
// combinacion sin punto arranca en campo X desde duty 0 (como antes del mapa)
// y se RECHAZA en campo nulo. 9 puntos x 16 bytes = 144 B: cubre las 3 x 3
// combinaciones y deja el margen de RAM del Mega (~76% usado).
struct ControlMapPoint {
  float intensity = 0.0f;   // mT; 0 = punto vacio
  float frequency = 0.0f;   // Hz
  float duty = 0.0f;        // duty comun (0..1)
  float balance = 0.0f;     // delta del campo nulo (+-)
  bool isValid() const { return intensity > 0.0f; }
};

namespace ControlMapLimits {
  constexpr float MaxIntensity = 100.0f;   // mT
  constexpr float MaxFrequency = 1000.0f;  // Hz
  constexpr float MaxBalance   = 0.5f;     // = CoilChannels::MaxBalance
  constexpr float IntensityMatch = 0.001f; // mT, para comparar floats de la SD con el start
  constexpr float FrequencyMatch = 0.5f;   // Hz
}

class ControlMap {
  public:
    static constexpr uint8_t MaxPoints = 9;   // = 3 intensidades x 3 frecuencias, ver ConfigurationOptions

    // Valida y guarda el punto `index`. Rechaza ENTERO (no recorta) lo fuera
    // de rango y deja ese lugar vacio: dejar el valor viejo daria un punto
    // que la tarjeta actual ya no dice.
    bool setPoint(uint8_t index, float intensity, float frequency, float duty, float balance) {
      if (index >= MaxPoints) return false;
      _points[index] = ControlMapPoint();
      if (!(intensity > 0.0f && intensity <= ControlMapLimits::MaxIntensity)) return false;
      if (!(frequency > 0.0f && frequency <= ControlMapLimits::MaxFrequency)) return false;
      if (!(duty > 0.0f && duty <= 1.0f)) return false;
      if (balance > ControlMapLimits::MaxBalance || balance < -ControlMapLimits::MaxBalance) return false;
      _points[index].intensity = intensity;
      _points[index].frequency = frequency;
      _points[index].duty = duty;
      _points[index].balance = balance;
      return true;
    }

    // La tarjeta dice cuantos puntos hay: los lugares de `count` en adelante
    // quedan vacios, para que un punto que la tarjeta nueva ya no trae no
    // sobreviva de un arranque anterior.
    void setCount(uint8_t count) {
      for (uint8_t i = count; i < MaxPoints; i++) _points[i] = ControlMapPoint();
    }

    void clear() { setCount(0); }

    // Coincidencia exacta (con tolerancia de float) en intensidad y
    // frecuencia. Sin interpolar: entre dos puntos medidos no hay nada medido,
    // y un duty "interpolado" para el campo nulo seria un control no verificado.
    const ControlMapPoint* find(float intensity, float frequency) const {
      for (uint8_t i = 0; i < MaxPoints; i++) {
        if (!_points[i].isValid()) continue;
        if (fabs(_points[i].intensity - intensity) <= ControlMapLimits::IntensityMatch &&
            fabs(_points[i].frequency - frequency) <= ControlMapLimits::FrequencyMatch) {
          return &_points[i];
        }
      }
      return nullptr;
    }

    uint8_t validCount() const {
      uint8_t n = 0;
      for (uint8_t i = 0; i < MaxPoints; i++) if (_points[i].isValid()) n++;
      return n;
    }

  private:
    ControlMapPoint _points[MaxPoints];
};

// =====================================================
// Plan de arranque
// =====================================================
//
// Funcion pura (sin hardware) para poder fijar con tests la regla que decide
// COMO excitar: Engine solo la ejecuta.
enum class StartRefusal : uint8_t { None, NoMappedPoint, BalanceOutOfRange };

struct StartPlan {
  StartRefusal refusal = StartRefusal::None;
  // true = el lazo de campo no corre: se aplica `initialDuty` fijo (campo nulo).
  bool openLoop = false;
  float initialDuty = 0.0f;
  float balance = 0.0f;
};

// balanceMax: tope operativo del |delta| (seccion control de la SD). Un punto
// mapeado con mas balance que eso se rechaza en vez de recortarse.
inline StartPlan planStart(const ControlMap& map, FieldMode mode, float intensity, float frequency, float balanceMax) {
  StartPlan plan;
  const ControlMapPoint* point = map.find(intensity, frequency);

  if (mode == FieldMode::X) {
    // Sin punto no hay error: simplemente no hay de donde partir (duty 0),
    // que es lo que hacia el equipo antes del mapa. El balance no interviene.
    if (point != nullptr) plan.initialDuty = point->duty;
    return plan;
  }

  if (point == nullptr) {
    plan.refusal = StartRefusal::NoMappedPoint;
    return plan;
  }
  if (point->balance > balanceMax || point->balance < -balanceMax) {
    plan.refusal = StartRefusal::BalanceOutOfRange;
    return plan;
  }
  plan.openLoop = true;
  plan.initialDuty = point->duty;
  plan.balance = point->balance;
  return plan;
}
