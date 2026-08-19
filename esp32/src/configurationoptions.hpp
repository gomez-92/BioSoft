#pragma once

namespace ConfigurationOptions {
  
  struct OptionFieldIntensity { const char* label; float intensity; };
  struct OptionFrequency { const char* label; int freq; };
  struct OptionDuration { const char* label; unsigned long duration; };
  struct OptionTolFieldIntensity { const char* label; int tol; };
  struct OptionRangeTemperature { const char* label; float tmin; float tmax; };

  /* ============================================================
   *  OPCIONES (DEFINICIÓN)
   * ============================================================ */

  inline constexpr OptionFieldIntensity optionsFieldIntensity[] = {
    {"1.0 mT", 1.0},
    {"2.0 mT", 2.0}
  };
  
  inline constexpr OptionFrequency optionsFrequency[] = {
    {"10 Hz", 10},
    {"50 Hz", 50}
  };
  
  inline constexpr OptionDuration optionsDuration[] = {
    {"00h 01m", 60000},
    {"00h 05m", 300000},
    {"00h 10m", 600000},
    {"01h 00m", 3600000},
    {"01h 30m", 5400000},
    {"02h 00m", 7200000}
  };
  
  inline constexpr OptionTolFieldIntensity optionsTolFieldIntensity[] = {
    {"1%", 1},
    {"5%", 5},
    {"10%", 10}
  };
  
  inline constexpr OptionRangeTemperature optionsRangeNormalTemperature[] = {
    {"30~40 C", 30, 40},
    {"28~42 C", 28, 42},
    {"26~44 C", 26, 44}
  };
  
  inline constexpr OptionRangeTemperature optionsRangeCriticalTemperature[] = {
    {"25~45 C", 25, 45},
    {"23~47 C", 23, 47},
    {"20~50 C", 20, 50}
  };

};