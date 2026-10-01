#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "sdstorage.hpp"
#include "configloader.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_REMOTECONFIG = true;

// Configuracion remota (tarjeta 24): el monitor web puede LEER la
// configuracion vigente de la placa y MANDARLE una nueva, que se graba en la
// SD y se aplica en el proximo reinicio.
//
// Es el primer canal de entrada remoto: hasta aca la placa solo publicaba.
// Lo que lo hace aceptable es lo que NO hace:
//   - nunca aplica nada en caliente: graba y vale al reiniciar;
//   - rechaza el pedido si hay un experimento en curso;
//   - valida antes de tocar config.json y respalda el anterior en
//     config.prev.json;
//   - wifi y broker no viajan nunca (ni de ida ni de vuelta) y se conservan
//     del archivo vigente al grabar;
//   - no hay comandos remotos (start/stop/reset) ni reinicio remoto.
// No hay confirmacion en pantalla: requiere abrir SquareLine y quedo como
// mejora (tarjeta 26). Consecuencia: lo aceptado se aplica en el proximo
// reinicio, CUALQUIERA sea su causa.
//
// TRANSPORTE. La configuracion pesa ~7 KB y PubSubClient usa un solo buffer
// de 512 bytes para entrar y salir (agrandarlo choca con el heap de TLS,
// tarjetas 17 y 18). Asi que el texto del JSON viaja en BLOQUES de tamano
// fijo -- no por seccion: `detector` o `menus` solas ya pasan de 512 --,
// cada uno dentro de un mensaje {"id"/"r", "i", "n", "d"}. El tope del
// bloque se mide en caracteres YA ESCAPADOS (una comilla dentro de "d" ocupa
// dos), que es lo que de verdad ocupa el mensaje.
//
// Ida (placa -> monitor): biosoft/config/current/<i> y .../meta, RETENIDOS,
// para que el monitor los encuentre aunque se conecte despues. Vuelta
// (monitor -> placa): biosoft/config/set, sin retain -- uno retenido se
// reentregaria en cada reconexion --, de a un bloque por vez: el monitor
// manda el siguiente recien cuando la placa confirma el anterior en
// biosoft/config/status. Asi la cola de 4 mensajes del lado de la placa
// nunca se desborda.
namespace RemoteConfig {

  constexpr const char* TopicCurrentPrefix = "biosoft/config/current/";
  constexpr const char* TopicCurrentMeta   = "biosoft/config/current/meta";
  constexpr const char* TopicSet           = "biosoft/config/set";
  constexpr const char* TopicStatus        = "biosoft/config/status";

  constexpr const char* NewPath  = "/biosoft/config.new";
  constexpr const char* PrevPath = "/biosoft/config.prev.json";

  // Tope de un bloque, en caracteres escapados. Con el envoltorio del
  // mensaje y el topic queda holgado debajo de los 512 del buffer.
  constexpr size_t MaxChunkEscaped = 340;
  constexpr size_t MaxRequestIdLength = 24;
  // Sin un bloque nuevo en este tiempo, el pedido se da por perdido.
  constexpr unsigned long ReceiveTimeoutMs = 30000;

  // Lo que el modulo necesita de afuera: publicar y saber si hay un
  // experimento en curso. Lo implementa MySystem.
  class Output {
    public:
      virtual bool publishConfigMessage(const char* topic, const char* payload, bool retain) = 0;
      virtual bool experimentInProgress() = 0;
      virtual ~Output() {}
  };

  // Cuantos caracteres ocupa `c` dentro de un string JSON.
  inline size_t escapedLength(char c) {
    if (c == '"' || c == '\\') return 2;
    if ((unsigned char)c < 0x20) return 6;
    return 1;
  }

  // Largo (en caracteres crudos) del bloque que empieza en `from`.
  inline size_t chunkLength(const char* text, size_t length, size_t from) {
    size_t escaped = 0;
    size_t i = from;
    while (i < length) {
      size_t next = escapedLength(text[i]);
      if (escaped + next > MaxChunkEscaped) break;
      escaped += next;
      i++;
    }
    return i - from;
  }

  inline uint16_t chunkCount(const char* text, size_t length) {
    uint16_t count = 0;
    size_t pos = 0;
    while (pos < length) {
      pos += chunkLength(text, length, pos);
      count++;
    }
    return count;
  }

  // =====================================================================
  // Ida: la configuracion vigente, publicada de a un bloque por tick.
  // =====================================================================
  class CurrentPublisher {
    private:
      const char* _text = nullptr;
      size_t _length = 0;
      size_t _pos = 0;
      uint16_t _index = 0;
      uint16_t _total = 0;
      bool _active = false;
      bool _metaSent = false;

    public:
      void begin(const char* text) {
        _text = text;
        _length = strlen(text);
        _pos = 0;
        _index = 0;
        _total = chunkCount(text, _length);
        _active = true;
        _metaSent = false;
      }

      bool active() const { return _active; }

      // Publica el siguiente mensaje. Si la cola de publicacion esta llena
      // no avanza: se reintenta en el proximo tick.
      void publishNext(Output& out, const char* configId) {
        if (!_active) return;
        char payload[512];

        if (_index < _total) {
          size_t length = chunkLength(_text, _length, _pos);
          char chunk[MaxChunkEscaped + 1];
          memcpy(chunk, _text + _pos, length);
          chunk[length] = '\0';

          JsonDocument doc;
          doc["id"] = configId;
          doc["i"] = _index;
          doc["n"] = _total;
          doc["d"] = chunk;
          serializeJson(doc, payload, sizeof(payload));

          char topic[48];
          snprintf(topic, sizeof(topic), "%s%u", TopicCurrentPrefix, _index);
          if (!out.publishConfigMessage(topic, payload, true)) return;
          _pos += length;
          _index++;
          return;
        }

        // Al final la meta: con "n" el monitor sabe cuantos bloques esperar.
        // n = 0 con "default": corren los defaults compilados, no hay
        // archivo que mostrar.
        JsonDocument doc;
        doc["id"] = configId;
        doc["n"] = _total;
        doc["len"] = _length;
        serializeJson(doc, payload, sizeof(payload));
        if (!out.publishConfigMessage(TopicCurrentMeta, payload, true)) return;
        _active = false;
        DEBUG_PRINT(DEBUG_REMOTECONFIG, F("[REMOTECONFIG] configuracion vigente publicada: "));
        DEBUG_PRINT(DEBUG_REMOTECONFIG, configId);
        DEBUG_PRINT(DEBUG_REMOTECONFIG, F(", "));
        DEBUG_PRINT(DEBUG_REMOTECONFIG, _total);
        DEBUG_PRINTLN(DEBUG_REMOTECONFIG, F(" bloques"));
      }
  };

  // =====================================================================
  // Vuelta: una configuracion nueva, recibida bloque a bloque en la SD.
  // =====================================================================
  class Receiver {
    private:
      SdStorage& _storage;
      char _requestId[MaxRequestIdLength + 1] = "";
      uint16_t _expected = 0;
      uint16_t _total = 0;
      bool _active = false;
      unsigned long _lastChunkMs = 0;

      void _status(Output& out, const char* request, const char* state,
                   int index = -1, const char* message = nullptr, const char* newId = nullptr) {
        JsonDocument doc;
        doc["r"] = request;
        doc["st"] = state;
        if (index >= 0) doc["i"] = index;
        if (message != nullptr) doc["msg"] = message;
        if (newId != nullptr) doc["id"] = newId;
        char payload[256];
        serializeJson(doc, payload, sizeof(payload));
        out.publishConfigMessage(TopicStatus, payload, false);

        DEBUG_PRINT(DEBUG_REMOTECONFIG, F("[REMOTECONFIG] "));
        DEBUG_PRINT(DEBUG_REMOTECONFIG, request);
        DEBUG_PRINT(DEBUG_REMOTECONFIG, F(" -> "));
        DEBUG_PRINT(DEBUG_REMOTECONFIG, state);
        if (message != nullptr) {
          DEBUG_PRINT(DEBUG_REMOTECONFIG, F(": "));
          DEBUG_PRINT(DEBUG_REMOTECONFIG, message);
        }
        DEBUG_PRINTLN(DEBUG_REMOTECONFIG, "");
      }

      void _abort() {
        _active = false;
        _requestId[0] = '\0';
        _storage.remove(NewPath);
      }

      // Con el archivo completo en config.new: valida, respalda, conserva
      // las credenciales y reemplaza config.json. Todo por streams de la SD:
      // ningun buffer de 8 KB, que saldria del mismo heap que necesita TLS.
      void _finish(Output& out) {
        File input = _storage.openRead(NewPath);
        if (!input) {
          _status(out, _requestId, "invalida", -1, "no se pudo leer el archivo recibido");
          _abort();
          return;
        }
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, input);
        input.close();
        if (error) {
          _status(out, _requestId, "invalida", -1, error.c_str());
          _abort();
          return;
        }
        if (!doc.is<JsonObject>() || (doc["schemaVersion"] | 0) != ConfigLoader::SupportedSchemaVersion) {
          _status(out, _requestId, "invalida", -1, "schemaVersion no soportada");
          _abort();
          return;
        }

        // El id se informa sobre lo que se va a cargar, sin credenciales:
        // es exactamente el configId que la placa va a publicar al reiniciar.
        char newId[ConfigLoader::ConfigIdLength];
        ConfigLoader::configIdOf(doc, newId, sizeof(newId));
        ConfigLoader::addCredentials(doc);

        // ConfigLoader lee con un buffer de 8 KB: un archivo mas grande se
        // grabaria bien y despues se descartaria entero en el arranque. Por
        // eso se graba MINIFICADO: indentado, una configuracion que entra al
        // enviarla puede pasarse con las credenciales agregadas.
        size_t size = measureJson(doc);
        if (size >= ConfigLoader::MaxFileSize - 1) {
          _status(out, _requestId, "invalida", -1, "el archivo no entra en 8 KB");
          _abort();
          return;
        }

        // Respaldo: el config.json vigente pasa a config.prev.json.
        if (_storage.exists(ConfigLoader::ConfigPath)
            && !_storage.copyFile(ConfigLoader::ConfigPath, PrevPath)) {
          _status(out, _requestId, "error", -1, "no se pudo respaldar config.json; no se toco nada");
          _abort();
          return;
        }

        File output = _storage.openWrite(ConfigLoader::ConfigPath);
        size_t written = output ? serializeJson(doc, output) : 0;
        if (output) output.close();
        if (written != size) {
          _status(out, _requestId, "error", -1, "no se pudo escribir config.json (queda el respaldo en config.prev.json)");
          _abort();
          return;
        }

        _status(out, _requestId, "aceptada", -1, "se aplica en el proximo reinicio", newId);
        _abort();   // limpia config.new y el estado
      }

    public:
      explicit Receiver(SdStorage& storage) : _storage(storage) {}

      // Un mensaje de biosoft/config/set. Se llama desde el loop principal,
      // nunca desde el callback MQTT (que corre en el otro nucleo): la SD
      // tambien la usa el loop, y no se puede tocar desde los dos.
      void handle(const char* payload, Output& out) {
        JsonDocument doc;
        if (deserializeJson(doc, payload)) {
          DEBUG_PRINTLN(DEBUG_REMOTECONFIG, F("[REMOTECONFIG] mensaje ilegible en config/set, se ignora"));
          return;
        }
        const char* request = doc["r"] | "";
        if (request[0] == '\0' || strlen(request) > MaxRequestIdLength
            || !doc["i"].is<uint16_t>() || !doc["n"].is<uint16_t>() || !doc["d"].is<const char*>()) {
          DEBUG_PRINTLN(DEBUG_REMOTECONFIG, F("[REMOTECONFIG] mensaje incompleto en config/set, se ignora"));
          return;
        }
        uint16_t index = doc["i"];
        uint16_t total = doc["n"];
        const char* data = doc["d"];

        if (!_storage.isReady()) {
          _status(out, request, "error", -1, "la placa no tiene tarjeta SD");
          return;
        }
        if (out.experimentInProgress()) {
          _status(out, request, "ocupada", -1, "hay un experimento en curso");
          if (_active && strcmp(request, _requestId) == 0) _abort();
          return;
        }

        if (index == 0) {
          if (total == 0) {
            _status(out, request, "invalida", -1, "pedido sin bloques");
            return;
          }
          _abort();
          strncpy(_requestId, request, MaxRequestIdLength);
          _requestId[MaxRequestIdLength] = '\0';
          _total = total;
          _expected = 0;
          _active = true;
          if (!_storage.ensureDir("/biosoft")) {
            _status(out, request, "error", -1, "no se pudo crear /biosoft en la SD");
            _abort();
            return;
          }
        }

        if (!_active || strcmp(request, _requestId) != 0) {
          _status(out, request, "incompleta", -1, "bloque de un pedido que no esta en curso");
          return;
        }
        // Un reenvio del bloque anterior (se perdio su confirmacion): se
        // vuelve a confirmar sin escribir dos veces.
        if (index + 1 == _expected) {
          _status(out, request, "parcial", index);
          return;
        }
        if (index != _expected || total != _total) {
          _status(out, request, "incompleta", -1, "bloque fuera de orden");
          _abort();
          return;
        }

        bool ok = (index == 0) ? _storage.writeFile(NewPath, data) : _storage.appendFile(NewPath, data);
        if (!ok) {
          _status(out, request, "error", -1, "no se pudo escribir en la SD");
          _abort();
          return;
        }
        _expected++;
        _lastChunkMs = millis();

        if (_expected < _total) {
          _status(out, request, "parcial", index);
          return;
        }
        _finish(out);
      }

      // Un pedido que dejo de llegar a mitad de camino.
      void checkTimeout(Output& out) {
        if (!_active) return;
        if (millis() - _lastChunkMs < ReceiveTimeoutMs) return;
        _status(out, _requestId, "incompleta", -1, "se dejaron de recibir bloques");
        _abort();
      }
  };

};
