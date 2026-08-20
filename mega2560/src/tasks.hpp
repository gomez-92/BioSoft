#pragma once

namespace Tasks {
  constexpr const char* Ping                  = "PING";
  constexpr const char* SendState             = "SEND_STATE";
  constexpr const char* MeasureTemperature    = "MEASURE_TEMPERATURE";
  constexpr const char* MeasureCurrent        = "MEASURE_CURRENT";
  constexpr const char* MeasureMagneticField  = "MEASURE_MAGNETIC_FIELD";
  constexpr const char* UpdateProgress        = "UPDATE_PROGRESS";
  constexpr const char* SendFlags             = "SEND_FLAGS";
  constexpr const char* SendResult            = "SEND_RESULT";
  constexpr const char* SettlingTime          = "SETTLING_TIME";
  constexpr const char* Finish                = "FINISH";
};