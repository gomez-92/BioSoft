#pragma once
// Bitacora remota: unos pocos eventos curados que la placa publica en
// biosoft/log y el monitor guarda y muestra. NO es el log USB espejado: el
// log USB (log.hpp) es la herramienta de banco, con todo el detalle; esto es
// lo que le sirve a alguien que mira la placa desde afuera.
//
//   EVENTO_E(MODULO, codigo, "fmt", ...)   error
//   EVENTO_W(MODULO, codigo, "fmt", ...)   advertencia
//   EVENTO_I(MODULO, codigo, "fmt", ...)   info
//
// Cada EVENTO escribe ademas la misma linea en el log USB (con el modulo),
// asi que no hace falta un LOG_* al lado. El evento se guarda aunque el
// nivel del log USB lo descarte.
//
// El CODIGO es estable y el texto no: el monitor filtra y cuenta por codigo
// y muestra el texto, asi que cambiar la redaccion no rompe la historia. La
// lista es cerrada (EventCodes, abajo): un codigo que no esta no compila.
// Si se agrega uno, va tambien en web/client/src/lib/bitacora.ts (nombre
// legible) -- mismo riesgo de deriva que el orden de bits de RLX.
//
// Mismas reglas que el log, y una mas estricta: el texto lo puede leer
// cualquier usuario del monitor, asi que ni contrasenas ni tokens ni
// contenido de la config, y TAMPOCO SSID ni host del broker (en el log USB
// esos van en D).
//
// Los eventos que pasan con el broker caido (el reinicio del Mega, la propia
// caida del broker) esperan en un buffer circular de 16 y salen al
// reconectar; si no, se perderian justo los que importan. Con el buffer
// lleno se pisa el mas viejo y el siguiente publicado lleva cuantos se
// perdieron ("d"). El buffer vive en RAM: lo que pasa justo antes de un
// reinicio de la ESP32 se pierde, y por eso la causa del reinicio sale en
// el evento "arranque" de la vuelta.
//
// Hilos: se registran eventos desde los dos nucleos (BrokerManager corre en
// communicationTask), asi que el buffer va bajo un spinlock. Publica
// MySystem::_publishEvents() desde el loop principal, de a uno y solo con la
// cola de publicacion vacia (no desplaza telemetria).
//
//   placa -> monitor  biosoft/log  {"n":7,"ts":1763820000123,"up":312,
//                                   "lv":"W","c":"broker_caido","m":"...","d":0}
//   n  = numero de evento desde el arranque (un hueco = eventos perdidos)
//   ts = hora real en ms; los eventos anteriores a la hora NTP se fechan al
//        publicar, restando su antiguedad (millis)
//   up = segundos desde el arranque de la ESP32 al ocurrir
//   d  = eventos pisados en el buffer justo antes de este (solo si > 0)
//   run = numero de la corrida en curso al ocurrir (solo si hay una): el
//        monitor asigna el evento a la corrida por este numero y no solo por
//        fecha -- el inicio de una corrida pasa ANTES de que el monitor la
//        abra (la abre al recibir targets, que sale despues).
// QoS 0 y sin retain: un evento retenido se le entregaria a cada monitor
// nuevo como si acabara de pasar.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdarg.h>
#include <sys/time.h>
#include "log.hpp"

namespace EventCodes {
  // Arranque de la ESP32: causa del reinicio y que configuracion corre.
  constexpr const char* arranque         = "arranque";
  // La config de la SD existe pero se ignora (ilegible, invalida, schema,
  // experimento incoherente): corren los valores de fabrica.
  constexpr const char* config_ignorada  = "config_ignorada";
  // Config remota grabada / revertida / red de respaldo conservada.
  constexpr const char* config_remota    = "config_remota";
  // Enlace serie con el Mega.
  constexpr const char* enlace_caido     = "enlace_caido";
  constexpr const char* enlace_ok        = "enlace_ok";
  // El Mega se reinicio sin que el enlace se cayera (uptime para atras).
  constexpr const char* mega_reinicio    = "mega_reinicio";
  // Peor margen heap/stack del Mega por debajo de 500 B: antesala del cuelgue.
  constexpr const char* mega_margen_bajo = "mega_margen_bajo";
  // Corrida cerrada como "lost": el Mega volvio a Ready sin result_data.
  constexpr const char* corrida_perdida  = "corrida_perdida";
  // El Mega rechazo un start (nomap / balance).
  constexpr const char* start_rechazado  = "start_rechazado";
  // Broker MQTT caido / de vuelta.
  constexpr const char* broker_caido     = "broker_caido";
  constexpr const char* broker_ok        = "broker_ok";
  // Mensajes de telemetria descartados por la cola de publicacion llena.
  constexpr const char* cola_llena       = "cola_llena";
  // Inicio y fin de cada corrida, para leer la bitacora como una sola linea
  // de tiempo. El fin de una corrida "lost" es corrida_perdida, no este.
  constexpr const char* corrida_inicio   = "corrida_inicio";
  constexpr const char* corrida_fin      = "corrida_fin";
}

#define EVENTO(nivel, letra, mod, codigo, fmt, ...)                              \
  do {                                                                           \
    LOG_AT(nivel, letra, mod, fmt, ##__VA_ARGS__);                               \
    EventLog::record(letra, EventCodes::codigo, fmt, ##__VA_ARGS__);             \
  } while (0)

#define EVENTO_E(mod, codigo, fmt, ...) EVENTO(LOG_NIVEL_E, 'E', mod, codigo, fmt, ##__VA_ARGS__)
#define EVENTO_W(mod, codigo, fmt, ...) EVENTO(LOG_NIVEL_W, 'W', mod, codigo, fmt, ##__VA_ARGS__)
#define EVENTO_I(mod, codigo, fmt, ...) EVENTO(LOG_NIVEL_I, 'I', mod, codigo, fmt, ##__VA_ARGS__)

namespace EventLog {

  constexpr const char* Topic = "biosoft/log";
  constexpr uint8_t Capacity = 16;
  constexpr size_t MaxCodeLength = 20;
  // El texto se corta aca. 16 x ~140 B = ~2.2 KB de RAM fija: sale del mismo
  // heap que necesita el handshake TLS, asi que no agrandar sin medir.
  constexpr size_t MaxMessageLength = 112;

  struct Entry {
    uint32_t seq;
    uint32_t atMs;
    uint32_t run;
    char level;
    char code[MaxCodeLength + 1];
    char message[MaxMessageLength + 1];
  };

  inline Entry _ring[Capacity];
  inline uint8_t _head = 0;      // el mas viejo
  inline uint8_t _count = 0;
  inline uint32_t _nextSeq = 1;
  inline uint16_t _overwritten = 0;   // pisados desde el ultimo publicado
  // Corrida en curso (0 = ninguna). Lo fija MySystem al entrar en Running y
  // lo limpia al cerrar la corrida; cada evento se lleva el valor del
  // momento. volatile: lo leen los dos nucleos.
  inline volatile uint32_t activeRun = 0;
  inline portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;

  inline void record(char level, const char* code, const char* fmt, ...)
      __attribute__((format(printf, 3, 4)));

  inline void record(char level, const char* code, const char* fmt, ...) {
    // Se formatea FUERA del spinlock: vsnprintf no es algo para hacer con
    // las interrupciones de este nucleo apagadas.
    char message[MaxMessageLength + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    uint32_t now = millis();

    portENTER_CRITICAL(&_lock);
    uint8_t slot;
    if (_count == Capacity) {
      slot = _head;                       // se pisa el mas viejo
      _head = (_head + 1) % Capacity;
      if (_overwritten < 0xFFFF) _overwritten++;
    } else {
      slot = (_head + _count) % Capacity;
      _count++;
    }
    Entry& entry = _ring[slot];
    entry.seq = _nextSeq++;
    entry.atMs = now;
    entry.run = activeRun;
    entry.level = level;
    strncpy(entry.code, code, MaxCodeLength);
    entry.code[MaxCodeLength] = '\0';
    memcpy(entry.message, message, sizeof(entry.message));
    portEXIT_CRITICAL(&_lock);
  }

  // Copia el mas viejo sin sacarlo. false si no hay ninguno.
  inline bool peek(Entry& out, uint16_t& overwritten) {
    bool found = false;
    portENTER_CRITICAL(&_lock);
    if (_count > 0) {
      out = _ring[_head];
      overwritten = _overwritten;
      found = true;
    }
    portEXIT_CRITICAL(&_lock);
    return found;
  }

  // Saca el mas viejo si sigue siendo `seq` (entre peek y pop pudo haberlo
  // pisado un evento nuevo con el buffer lleno).
  inline void pop(uint32_t seq) {
    portENTER_CRITICAL(&_lock);
    if (_count > 0 && _ring[_head].seq == seq) {
      _head = (_head + 1) % Capacity;
      _count--;
      _overwritten = 0;
    }
    portEXIT_CRITICAL(&_lock);
  }

  inline uint8_t pending() {
    portENTER_CRITICAL(&_lock);
    uint8_t count = _count;
    portEXIT_CRITICAL(&_lock);
    return count;
  }

  // Arma el JSON del evento. Devuelve los bytes escritos (0 si no entra).
  // Con hora valida, ts = ahora menos la antiguedad del evento; sin hora no
  // va ts y el monitor usa la de recepcion.
  inline size_t buildPayload(const Entry& entry, uint16_t overwritten, bool timeValid, char* buffer,
                             size_t length) {
    JsonDocument doc;
    doc["n"] = entry.seq;
    if (timeValid) {
      struct timeval now;
      gettimeofday(&now, nullptr);
      int64_t nowMs = (int64_t)now.tv_sec * 1000LL + now.tv_usec / 1000;
      doc["ts"] = nowMs - (int64_t)(uint32_t)(millis() - entry.atMs);
    }
    doc["up"] = entry.atMs / 1000UL;
    char level[2] = {entry.level, '\0'};
    doc["lv"] = level;
    doc["c"] = entry.code;
    doc["m"] = entry.message;
    if (overwritten > 0) doc["d"] = overwritten;
    if (entry.run > 0) doc["run"] = entry.run;
    if (measureJson(doc) >= length) return 0;
    return serializeJson(doc, buffer, length);
  }
}
