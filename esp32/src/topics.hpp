#pragma once
#include <Arduino.h>

// Configuracion de la telemetria remota del ESP32 (no es parte del protocolo
// serie con el Mega -- ver seriallink.hpp/commands.hpp para eso). Datacake se
// suscribe como cliente MQTT del mismo broker (EMQX) y decodea el JSON con un
// Decoder propio, configurado del lado de Datacake.
//
// Ya NO son constantes: las pisa la seccion `telemetry` del archivo de la SD
// (ver docs/config-schema.md seccion 9 y configloader.hpp). Los valores de
// aca son los DEFAULTS COMPILADOS, vigentes si no hay tarjeta o si la seccion
// falta.
//
// CUIDADO AL CAMBIARLOS: el Decoder de Datacake espera exactamente estas 6
// claves en este topic. Renombrar una desde el archivo obliga a actualizar
// tambien el Decoder, o ese valor deja de llegar al dashboard sin ningun
// error visible de este lado.
namespace Topics {
  constexpr size_t MaxTopicLength = 64;   // 63 caracteres + terminador
  constexpr size_t MaxFieldNameLength = 16;  // 15 caracteres + terminador

  inline char Telemetry[MaxTopicLength] = "biosoft/telemetry";

  // El set de campos es CERRADO: son los 6 que el firmware sabe producir y no
  // se pueden agregar nuevos desde el archivo. Lo configurable es cuales se
  // publican y con que nombre de clave salen.
  struct TelemetryField {
    bool enabled;
    char name[MaxFieldNameLength];
  };

  // Publicados en la tanda de mediciones (Intervals::PublishMeasures).
  inline TelemetryField MagneticField = { true, "CEM1" };
  inline TelemetryField Temperature   = { true, "TEMP1" };
  inline TelemetryField Current       = { true, "BOB1" };

  // Publicados en la tanda de estado (Intervals::PublishStatus).
  inline TelemetryField Health        = { true, "ESTADO" };
  inline TelemetryField Progress      = { true, "PROGRESS" };
  inline TelemetryField ElapsedTime   = { true, "ELAPSED_TIME" };
};
