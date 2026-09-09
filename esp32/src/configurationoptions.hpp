#pragma once
#include <Arduino.h>

// Opciones que ofrecen los dropdowns de la pantalla Configuracion.
//
// Ya NO son constexpr: son buffers estaticos de tope fijo con un contador de
// largo real, poblados desde el archivo de configuracion de la SD al arrancar
// (ver docs/config-schema.md y configloader.hpp). Los valores escritos acá son
// los DEFAULTS COMPILADOS: si no hay tarjeta, si el archivo no existe o si una
// seccion falta, el sistema arranca con estos y sigue funcionando.
//
// Cada menu tiene su propio contador. Ese contador es el largo real, no el
// tamaño del buffer, y es lo que hay que pasarle a buildDropdown() -- antes se
// le pasaba un literal escrito a mano en cada llamada, que ya se desincronizo
// una vez cuando la lista de tolerancia paso de 3 a 2 entradas.
//
// `label` es un buffer propio y no un `const char*` porque las cadenas del
// archivo viven en el JsonDocument, que se destruye apenas termina el parseo:
// hay que copiarlas, no apuntarlas.
namespace ConfigurationOptions {

  // Topes de compilacion: la ESP32 convive con LVGL y el framebuffer del TFT,
  // asi que no se reserva memoria en funcion de lo que traiga el archivo. Lo
  // que exceda estos topes se ignora y queda registrado en el log.
  constexpr uint8_t MaxOptions = 8;
  constexpr uint8_t MaxLabelLength = 16;   // 15 caracteres + terminador

  struct OptionFieldIntensity { char label[MaxLabelLength]; float intensity; };
  struct OptionFrequency { char label[MaxLabelLength]; int freq; };
  struct OptionDuration { char label[MaxLabelLength]; unsigned long duration; };
  struct OptionTolFieldIntensity { char label[MaxLabelLength]; int tol; };
  struct OptionRangeTemperature { char label[MaxLabelLength]; float tmin; float tmax; };
  // Bring-up temporal INT-001 (2026-08-31, ver tarjeta Trello "Prueba de
  // integracion -- Control de intensidad CEM"): quitar junto con el resto
  // del combo de testMode una vez terminadas las 6 pruebas de laboratorio.
  // NO se parametriza desde la SD a proposito: es temporal.
  struct OptionTestMode { char label[MaxLabelLength]; uint8_t value; };

  /* ============================================================
   *  OPCIONES (DEFAULTS COMPILADOS)
   * ============================================================ */

  inline OptionFieldIntensity optionsFieldIntensity[MaxOptions] = {
    {"1.0 mT", 1.0},
    {"2.0 mT", 2.0}
  };
  inline uint8_t countFieldIntensity = 2;

  inline OptionFrequency optionsFrequency[MaxOptions] = {
    {"10 Hz", 10},
    {"50 Hz", 50}
  };
  inline uint8_t countFrequency = 2;

  inline OptionDuration optionsDuration[MaxOptions] = {
    {"00h 01m", 60000},
    {"00h 05m", 300000},
    {"00h 10m", 600000},
    {"01h 00m", 3600000},
    {"01h 30m", 5400000},
    {"02h 00m", 7200000}
  };
  inline uint8_t countDuration = 6;

  inline OptionTolFieldIntensity optionsTolFieldIntensity[MaxOptions] = {
    {"5%", 5},
    {"10%", 10}
  };
  inline uint8_t countTolFieldIntensity = 2;

  inline OptionRangeTemperature optionsRangeNormalTemperature[MaxOptions] = {
    {"30~40 C", 30, 40},
    {"28~42 C", 28, 42},
    {"26~44 C", 26, 44}
  };
  inline uint8_t countRangeNormalTemperature = 3;

  inline OptionRangeTemperature optionsRangeCriticalTemperature[MaxOptions] = {
    {"25~45 C", 25, 45},
    {"23~47 C", 23, 47},
    {"20~50 C", 20, 50}
  };
  inline uint8_t countRangeCriticalTemperature = 3;

  // Sigue siendo fijo: es de bring-up y se elimina, no se configura.
  inline OptionTestMode optionsTestMode[MaxOptions] = {
    {"Test 1", 1},
    {"Test 2", 2},
    {"Test 3", 3},
    {"Test 4", 4},
    {"Test 5", 5},
    {"Test 6", 6}
  };
  inline uint8_t countTestMode = 6;

};
