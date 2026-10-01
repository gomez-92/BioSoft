#pragma once
#include <string.h>

// Que driver alimenta a una fuente del Detector: la clave `sensor` de
// detector.sources en la tarjeta SD (docs/config-schema.md §7), que llega en
// config_source y Engine aplica en el proximo start.
//
//   Real -- el sensor fisico (MLX90393 para CEM1, DS18B20 para TEMP1)
//   Sim  -- la entrada analogica simulada (A0 para CEM1, A1 para TEMP1)
//   Scenario -- lecturas sinteticas que describe la tarjeta SD
//           (scenario.hpp): recorren toda la logica sin sensores.
//   None -- ningun sensor registrado. Solo TEMP1: sin DS18B20 cableado, es
//           lo que evita los ~750 ms que requestTemperatures() bloquea en
//           cada medicion aunque no haya nadie en el bus.
//
// CEM1 NO acepta None: es la realimentacion del FieldController, y sin
// magnetometro el lazo empujaria el duty a fondo buscando un campo que no
// puede medir. Para "no mirar el campo" esta detector.sources[CEM1].enabled,
// y para "no excitar" esta control.enabled.
enum class SensorChoice : uint8_t { Real, Sim, Scenario, None };

namespace SensorChoices {

  // Escribe en `out` el driver que pide `value` para esa fuente y devuelve
  // true. Si el valor no vale para esa fuente devuelve false y NO toca
  // `out`: se rechaza entero y queda el que estaba, mismo contrato que las
  // reglas del Detector (rechazar, no adivinar). El generador de tools/ ya
  // no deja descargar un valor invalido.
  inline bool parse(const char* source, const char* value, SensorChoice& out) {
    if (source == nullptr || value == nullptr) return false;

    if (strcmp(source, "CEM1") == 0) {
      if (strcmp(value, "mlx90393") == 0) { out = SensorChoice::Real; return true; }
      if (strcmp(value, "sim") == 0)      { out = SensorChoice::Sim;  return true; }
      if (strcmp(value, "scenario") == 0) { out = SensorChoice::Scenario; return true; }
      return false;
    }

    if (strcmp(source, "TEMP1") == 0) {
      if (strcmp(value, "ds18b20") == 0) { out = SensorChoice::Real; return true; }
      if (strcmp(value, "sim") == 0)     { out = SensorChoice::Sim;  return true; }
      if (strcmp(value, "scenario") == 0) { out = SensorChoice::Scenario; return true; }
      if (strcmp(value, "none") == 0)    { out = SensorChoice::None; return true; }
      return false;
    }

    return false;
  }

  inline const char* name(SensorChoice choice) {
    switch (choice) {
      case SensorChoice::Real: return "real";
      case SensorChoice::Sim:  return "sim";
      case SensorChoice::Scenario: return "scenario";
      case SensorChoice::None: return "none";
    }
    return "?";
  }

};
