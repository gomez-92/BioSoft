#pragma once

// Topics MQTT propios del ESP32 (no son parte del protocolo serie con el
// Mega -- ver seriallink.hpp/commands.hpp para eso). Datacake se suscribe
// a esto como cliente MQTT del mismo broker (EMQX) y decodea el JSON con
// un Decoder propio configurado del lado de Datacake.
namespace Topics {
  constexpr const char* Telemetry = "biosoft/telemetry";
};
