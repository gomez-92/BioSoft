#pragma once

// Ya NO son constexpr: se pueblan desde el archivo de configuracion de la SD
// al arrancar (ver docs/config-schema.md y configloader.hpp). Los valores de
// acá son los DEFAULTS COMPILADOS, que quedan vigentes si no hay tarjeta, si
// el archivo no existe, o si la seccion falta.
//
// Se leen en cada addTask(), no se cachean en ningun lado, asi que alcanza
// con pisarlos antes de que MySystem registre sus tareas.
namespace Intervals {
  inline unsigned long UpdateScreens         = 50;
  inline unsigned long ReSendStart           = 4000;
  inline unsigned long ReSendStop            = 4000;
  inline unsigned long ReSendReset           = 4000;
  inline unsigned long UpdateProgress        = 5000;

  // Cadencia de publicacion a Datacake (via MQTT/EMQX), separada por tipo
  // de variable como pidio el usuario -- mediciones en vivo vs estado
  // general -- para poder ajustarlas independiente una de la otra sin
  // tocar codigo. Valores de arranque conservadores; ajustar segun el
  // limite real del plan de Datacake que se este usando.
  inline unsigned long PublishMeasures       = 30000;
  inline unsigned long PublishStatus         = 30000;

  // Reintento del envio fragmentado de config_intervals/config_control/
  // config_coil al Mega (ver mysystem.hpp::_sendMegaConfig()).
  inline unsigned long ReSendConfig          = 4000;

  // Timeouts de pantalla, no periodos de tarea: cuanto se queda el splash
  // antes de emitir su Timeout, y cuanto espera Busy la confirmacion del
  // Mega antes de rendirse y volver a Principal. Configurables por SD como
  // el resto, pero con piso de 1000 ms (ConfigLoader::applyScreenTimeout).
  // busyTimeout ademas tiene que superar a intervals.mega.sendState: es la
  // cadencia con la que llega el state_data que esta pantalla espera.
  inline unsigned long SplashTimeout         = 4000;
  inline unsigned long BusyTimeout           = 6000;
};