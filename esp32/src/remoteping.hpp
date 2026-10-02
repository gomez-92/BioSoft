#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Ping del monitor web: la forma de saber si la placa esta viva AHORA, y no
// solo si publico algo alguna vez. Fuera de un experimento la placa no
// publica telemetria, asi que sin esto el monitor no puede distinguir "placa
// encendida y quieta" de "placa apagada".
//
// Es solo lectura, como el resto del canal remoto: el ping no cambia nada en
// la placa, solo pide que conteste. No es un comando remoto (no hay start,
// stop ni reinicio por MQTT, por diseno).
//
//   monitor -> placa  biosoft/ping  {"r":"<id>"}
//   placa -> monitor  biosoft/pong  {"r":"<id>","st":"ready","cfg":"<configId>",
//                                    "cfgst":"ok","mega":true,"up":1234}
//   (cfgst = ConfigLoader::loadStatus(): por que corre esa configuracion)
//
// Ninguno de los dos va retenido: un pong retenido le diria a un monitor
// recien conectado que la placa contesto, cuando puede estar apagada desde
// hace horas.
namespace RemotePing {

  constexpr const char* TopicPing = "biosoft/ping";
  constexpr const char* TopicPong = "biosoft/pong";
  constexpr size_t MaxRequestIdLength = 24;

  struct Request {
    char id[MaxRequestIdLength + 1];
  };

  // Extrae el id del ping. false si el mensaje no lo trae o no entra.
  inline bool parse(const char* payload, Request& out) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;
    const char* id = doc["r"] | "";
    size_t length = strlen(id);
    if (length == 0 || length > MaxRequestIdLength) return false;
    memcpy(out.id, id, length + 1);
    return true;
  }

  // Arma la respuesta en `buffer`. Devuelve los bytes escritos (0 si no entra).
  inline size_t buildPong(const Request& request, const char* state, const char* configId,
                          const char* configStatus, bool megaConnected, unsigned long uptimeSeconds,
                          char* buffer, size_t length) {
    JsonDocument doc;
    doc["r"] = request.id;
    doc["st"] = state;
    doc["cfg"] = configId;
    doc["cfgst"] = configStatus;
    doc["mega"] = megaConnected;
    doc["up"] = uptimeSeconds;
    if (measureJson(doc) >= length) return 0;
    return serializeJson(doc, buffer, length);
  }
}
