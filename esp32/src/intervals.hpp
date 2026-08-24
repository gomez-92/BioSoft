#pragma once

namespace Intervals {
  constexpr unsigned long UpdateScreens         = 50;
  constexpr unsigned long ReSendStart           = 4000;
  constexpr unsigned long ReSendStop            = 4000;
  constexpr unsigned long ReSendReset           = 4000;
  constexpr unsigned long UpdateProgress        = 5000;

  // Cadencia de publicacion a Datacake (via MQTT/EMQX), separada por tipo
  // de variable como pidio el usuario -- mediciones en vivo vs estado
  // general -- para poder ajustarlas independiente una de la otra sin
  // tocar codigo. Valores de arranque conservadores; ajustar segun el
  // limite real del plan de Datacake que se este usando.
  constexpr unsigned long PublishMeasures       = 30000;
  constexpr unsigned long PublishStatus         = 30000;
};