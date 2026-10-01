#pragma once

namespace Commands {
  constexpr const char* Ping                  = "ping";
  constexpr const char* Pong                  = "pong";
  constexpr const char* Start                 = "start";
  constexpr const char* Stop                  = "stop";
  constexpr const char* Reset                 = "reset";
  constexpr const char* StateData             = "state_data";
  constexpr const char* ResultData            = "result_data";
  constexpr const char* TempData              = "temp_data";
  constexpr const char* CemData               = "cem_data";
  // Un frame por tanda con duty Y corriente de cada bobina ("d1".."d4",
  // "c1".."c4"). Reemplaza al viejo current_data, que mandaba solo las
  // corrientes indexadas por nombre de sensor: la pantalla En curso
  // necesita las dos magnitudes juntas, por fila de bobina, y saber
  // cuantas bobinas hay realmente registradas del otro lado.
  constexpr const char* CoilData              = "coil_data";
  constexpr const char* OneFlagsData          = "flag_data";
  constexpr const char* Ack                   = "ack";
  constexpr const char* ConfigIntervals       = "config_intervals";
  constexpr const char* ConfigControl         = "config_control";
  constexpr const char* ConfigCoil            = "config_coil";
  constexpr const char* ConfigSource          = "config_source";
  constexpr const char* ConfigRule            = "config_rule";
  constexpr const char* ConfigCurrent         = "config_current";
  constexpr const char* ConfigDetector        = "config_detector";
  constexpr const char* ConfigScenario        = "config_scenario";
};