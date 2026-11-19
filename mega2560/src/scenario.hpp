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
//   nivel(t) = puntos(t) + oscAmp*sin(2*pi*t/oscPeriod)
//            + ruido uniforme en [-noise, noise]
//
// donde puntos(t) es la curva que une hasta MaxPoints puntos de control
// (segundo, nivel) con rectas: antes del primero vale el primero, despues
// del ultimo vale el ultimo, sin puntos vale 0. Dos puntos con el MISMO
// segundo son un salto vertical. Rampa y salto (las claves de la tarjeta 22)
// son casos particulares: una rampa son dos puntos, un salto dos puntos en
// el mismo segundo. Se cambiaron por puntos porque el operador dibuja la
// senal en el generador (tools/generador-config.html) arrastrandolos, y
// cualquier forma -- subir, mantener, bajar, recuperarse -- entra en 32
// bytes. Un arreglo de muestras no: una hora de TEMP1 cada 5 s son 720
// floats, mas de un tercio de la RAM del Mega.
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
// Un punto de control. Entero a proposito: 4 bytes en vez de 8, y el
// generador no ofrece mas resolucion que el segundo y la centesima de nivel.
struct ScenarioPoint {
  uint16_t t = 0;        // s desde el start
  int16_t level100 = 0;  // nivel x 100
};

namespace Scenario {
  constexpr uint8_t MaxPoints = 8;
  // Tope de |nivel| de un punto. Nivel 10 ya esta 8 franjas criticas por
  // fuera del borde critico: mas alla no agrega nada que probar, y acota el
  // int16.
  constexpr float MaxLevel = 10.0f;
};

struct ScenarioSignal {
  ScenarioPoint points[Scenario::MaxPoints];
  uint8_t pointCount = 0;     // 0 = nivel 0 todo el tiempo
  float noise = 0.0f;         // amplitud del ruido, en niveles, >= 0
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

  // Nivel de la curva de puntos en t (sin oscilacion ni ruido).
  inline float pointsLevelAt(const ScenarioSignal& s, float t) {
    if (s.pointCount == 0) return 0.0f;
    const ScenarioPoint* p = s.points;
    if (t < p[0].t) return p[0].level100 / 100.0f;
    // El ULTIMO punto con t_i <= t: con dos puntos en el mismo segundo gana
    // el segundo, que es lo que hace del par un salto.
    uint8_t i = 0;
    while (i + 1 < s.pointCount && p[i + 1].t <= t) i++;
    if (i + 1 >= s.pointCount) return p[i].level100 / 100.0f;
    float t0 = p[i].t, t1 = p[i + 1].t;
    float l0 = p[i].level100 / 100.0f, l1 = p[i + 1].level100 / 100.0f;
    return l0 + (l1 - l0) * (t - t0) / (t1 - t0);
  }

  // `unitNoise` en [-1, 1] se pasa de afuera para poder fijarlo en los tests.
  inline float levelAt(const ScenarioSignal& s, float t, float unitNoise) {
    float level = pointsLevelAt(s, t);
    if (s.oscPeriod > 0.0f) level += s.oscAmp * sinf(2.0f * (float)M_PI * t / s.oscPeriod);
    level += s.noise * unitNoise;
    return level;
  }

  // Por que se rechazo una lista de puntos. Un codigo y no un texto: en el
  // AVR cada literal que no va con F() vive en RAM, y los cinco motivos
  // costaban ~170 bytes. El texto lo pone Engine con F() al loguear.
  enum class PointsRefusal : uint8_t {
    None = 0,
    OddLength,     // cantidad impar de valores
    TooMany,       // mas de MaxPoints puntos
    BadTime,       // segundo no entero o fuera de 0..65535
    BadLevel,      // nivel fuera de -MaxLevel..MaxLevel
    Unordered,     // segundos que bajan
    NotNumeric,    // (solo el handler) un valor que no es numero
    Missing        // (solo el handler) falta la lista
  };

  // Valida una lista plana [t0, n0, t1, n1, ...] tal como viaja en
  // config_scenario_pts y, si TODA es valida, la copia a `signal`. Si algo
  // falla no toca nada: medio escenario corre una prueba que nadie dibujo
  // (mismo contrato que las reglas del Detector, "rechazar entero").
  inline PointsRefusal applyPoints(ScenarioSignal& signal, const float* flat, uint8_t length) {
    if (length % 2 != 0) return PointsRefusal::OddLength;
    if (length / 2 > MaxPoints) return PointsRefusal::TooMany;
    ScenarioPoint parsed[MaxPoints];
    for (uint8_t i = 0; i < length / 2; i++) {
      float t = flat[2 * i], level = flat[2 * i + 1];
      if (!(t >= 0.0f && t <= 65535.0f) || t != (float)(long)t) return PointsRefusal::BadTime;
      if (!(level >= -MaxLevel && level <= MaxLevel)) return PointsRefusal::BadLevel;
      if (i > 0 && t < parsed[i - 1].t) return PointsRefusal::Unordered;
      parsed[i].t = (uint16_t)t;
      parsed[i].level100 = (int16_t)lroundf(level * 100.0f);
    }
    for (uint8_t i = 0; i < length / 2; i++) signal.points[i] = parsed[i];
    signal.pointCount = length / 2;
    return PointsRefusal::None;
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
