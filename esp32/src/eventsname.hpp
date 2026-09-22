#pragma once

namespace EventName {
  constexpr const char* Start = "start";
  constexpr const char* Stop = "stop";
  constexpr const char* Back = "back";
  constexpr const char* GoToConfig = "gotoconfig";
  constexpr const char* Save = "save";
  constexpr const char* Timeout = "timeout";
  // Repetir el experimento que acaba de terminar, con la misma
  // configuracion. Lo emite el boton REPETIR de Resultado y MySystem lo
  // trata igual que el Start de Principal.
  constexpr const char* Repeat = "repeat";
};
