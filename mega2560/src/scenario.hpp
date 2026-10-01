#pragma once
#include <Arduino.h>
#include <math.h>

// Lecturas SINTETICAS para validar la logica sin sensores (tarjeta 22).
//
// Una fuente con `sensor: "scenario"` no lee hardware: calcula su valor como
// una funcion del tiempo desde el start, con la forma que describe la
// tarjeta SD (detector.sources[].scenario). El valor entra al sistema por el
// mismo camino que el de un sensor real -- IMagnetometer / IThermometer --,
// asi que el Detector, sus reglas, los flags, el corte por _finish() y lo
// que se ve en pantalla y en el monitor remoto son la logica real, no una
// imitacion.
//
// Por que en el Mega y no fabricado en el ESP32: ahi solo se probaria el
// envio, y las alertas serian de mentira. Por que una formula y no muestras
// mandadas por serie: el enlace es de 9600 baudios y compite con todo lo
// demas, y un sensor que depende del enlace se "congela" cuando el enlace se
// cae -- que se veria como una falla del sensor.
//
// ---------------------------------------------------------------------------
// La senal NO tiene valores absolutos: se mide en NIVELES relativos a los
// rangos de la corrida, los que el operador eligio en pantalla (TEMP1) o los
// que el Mega deriva del objetivo y la tolerancia (CEM1, ver
// DetectorConfigBuilder::applyCemRanges):
//
//    nivel  0  = centro del rango normal
//    nivel +1  = borde superior del normal      (-1 = inferior)
//    nivel +2  = borde superior del critico     (-2 = inferior)
//    nivel > 2 = adentro de la zona critica, con el mismo paso que de 1 a 2
//
// Entre esos puntos, lineal. Asi un escenario significa lo mismo con
// cualquier rango: "temperatura alta" es un salto a +2.5 y corta igual con
// 30~40/25~45 que con 26~44/20~50, y "campo inestable" con amplitud 1.5 se
// sale del normal con tolerancia 5% o 10%. Con valores fijos, cambiar un
// combo en pantalla dejaria el escenario adentro del rango normal sin que
// nadie lo note.
//
//   nivel(t) = base + ramp*t/60 + (t >= stepAt ? step : 0)
//            + oscAmp*sin(2*pi*t/oscPeriod) + ruido uniforme en [-noise, noise]
//
// y a partir de dropAt (si es > 0) el sensor deja de dar lecturas validas:
// el "sensor que se cae", que el Detector ve como silencio.
//
// CEM1 ademas tiene planta: `setpointDuty` es el duty comun con el que las
// bobinas alcanzan exactamente el objetivo, y el campo es
//
//   objetivo * duty/setpointDuty + (valor del nivel - objetivo)
//
// es decir, lo que producen las bobinas mas una perturbacion. Asi el
// FieldController regula de verdad contra el escenario -- y puede compensar
// una perturbacion lenta, que es lo que haria con bobinas reales. Con
// setpointDuty = 0 no hay planta: el campo es el nivel tal cual, sin
// importar el lazo (sirve para forzar un campo fuera de rango). Las bobinas
// NO se energizan mientras CEM1 no sea el sensor real
// (CoilChannels::setOutputsInhibited): el duty se calcula y se reporta, pero
// no sale.
// ---------------------------------------------------------------------------
struct ScenarioSignal {
  float base = 0.0f;          // nivel
  float noise = 0.0f;         // amplitud del ruido, en niveles, >= 0
  float ramp = 0.0f;          // niveles por minuto
  float stepAt = 0.0f;        // s desde el start; 0 = sin salto
  float step = 0.0f;          // niveles que se suman a partir de stepAt
  float oscAmp = 0.0f;        // niveles
  float oscPeriod = 0.0f;     // s; con 0 no hay oscilacion
  float dropAt = 0.0f;        // s desde el start; 0 = nunca se cae
  float setpointDuty = 0.0f;  // solo CEM1: duty que da el objetivo; 0 = sin planta
};

// Los cuatro limites de la corrida, tal como los usa el Detector.
struct ScenarioRanges {
  float normalMin = 0.0f;
  float normalMax = 0.0f;
  float criticalMin = 0.0f;
  float criticalMax = 0.0f;
};

namespace Scenario {

  // Reloj comun de los escenarios: segundos desde el ultimo start. Lo
  // reinicia Engine::_start().
  inline unsigned long startMs = 0;

  inline void restart(unsigned long nowMs) { startMs = nowMs; }

  inline float secondsSinceStart(unsigned long nowMs) {
    return (nowMs - startMs) / 1000.0f;
  }

  // Generador pseudoaleatorio propio (xorshift32): determinista y sin
  // depender de random() del core, que en native no esta. Devuelve un valor
  // uniforme en [-1, 1].
  inline float nextUnitNoise(uint32_t& state) {
    if (state == 0) state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state / 4294967295.0f) * 2.0f - 1.0f;
  }

  // `unitNoise` en [-1, 1] se pasa de afuera para poder fijarlo en los tests.
  inline float levelAt(const ScenarioSignal& s, float t, float unitNoise) {
    float level = s.base + s.ramp * t / 60.0f;
    if (s.stepAt > 0.0f && t >= s.stepAt) level += s.step;
    if (s.oscPeriod > 0.0f) level += s.oscAmp * sinf(2.0f * (float)M_PI * t / s.oscPeriod);
    level += s.noise * unitNoise;
    return level;
  }

  inline float center(const ScenarioRanges& r) {
    return (r.normalMin + r.normalMax) / 2.0f;
  }

  // Nivel -> valor fisico, lineal por tramos. Si un rango critico coincide
  // con el normal (sin franja de advertencia), el paso de 1 a 2 se toma
  // igual a medio rango normal: sin eso, todos los niveles por encima de 1
  // caerian en el mismo valor y un escenario de "alta" no podria pasar el
  // borde.
  inline float levelToValue(const ScenarioRanges& r, float level) {
    float c = center(r);
    float halfNormal = (r.normalMax - r.normalMin) / 2.0f;
    if (level >= 0.0f) {
      if (level <= 1.0f) return c + level * halfNormal;
      float gap = r.criticalMax - r.normalMax;
      if (gap <= 0.0f) gap = halfNormal;
      return r.normalMax + (level - 1.0f) * gap;
    }
    if (level >= -1.0f) return c + level * halfNormal;
    float gap = r.normalMin - r.criticalMin;
    if (gap <= 0.0f) gap = halfNormal;
    return r.normalMin + (level + 1.0f) * gap;
  }

  // El campo de CEM1: planta + perturbacion (ver arriba).
  inline float fieldValue(const ScenarioSignal& s, const ScenarioRanges& r, float level, float duty) {
    float value = levelToValue(r, level);
    if (s.setpointDuty <= 0.0f) return value;
    float target = center(r);
    return target * (duty / s.setpointDuty) + (value - target);
  }

  inline bool validAt(const ScenarioSignal& s, float t) {
    return !(s.dropAt > 0.0f && t >= s.dropAt);
  }

};
