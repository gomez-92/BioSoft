#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "sdstorage.hpp"
#include "configloader.hpp"
#include "log.hpp"

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
//   - las contrasenas (WiFi y broker) viajan SOLO de ida (2026-11-19): la
//     placa publica sus redes y su broker sin contrasenas; el monitor puede
//     mandar una lista de redes nueva y otro broker. Una red o un broker sin
//     contrasena conserva la que ya tenia la placa, y la red en uso no se
//     puede quitar (ConfigLoader::mergeRemoteWifi). Sin seccion wifi o
//     broker, se conservan las del archivo vigente;
//   - y como un broker mal escrito no tiene red de respaldo, lo recibido se
//     aplica A PRUEBA: si tras reiniciar la placa no llega al broker en
//     ConfigTrialMs, vuelve sola a config.prev.json (MySystem::_checkConfigTrial)
//     y al reconectar le avisa al monitor ("revertida");
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
  // Marca de "configuracion a prueba": la escribe el receptor al grabar
  // (contiene el id del pedido) y la borra la placa cuando, ya reiniciada,
  // llega al broker. Si en ConfigTrialMs no llego, se restaura PrevPath y se
  // deja RevertedPath (mismo id) para avisarle al monitor al reconectar.
  constexpr const char* TrialPath    = "/biosoft/config.trial";
  constexpr const char* RevertedPath = "/biosoft/config.reverted";
  constexpr unsigned long ConfigTrialMs = 5UL * 60UL * 1000UL;

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
      // true si la cola de publicacion esta vacia. BrokerManager::publish()
      // con la cola llena descarta el mensaje MAS VIEJO y devuelve true, asi
      // que un false de publishConfigMessage nunca frenaba al publicador: los
      // 12 bloques entraban de golpe en una cola de 4 y se perdian bloques
      // del medio (el monitor descarta la config entera por CRC).
      virtual bool publishQueueIdle() = 0;
      virtual bool experimentInProgress() = 0;
      // La configuracion nueva quedo grabada en la SD. MySystem programa el
      // reinicio que la aplica (ver MySystem::_rebootForConfigIfIdle).
      virtual void onConfigAccepted() {}
      // La red por la que la placa esta conectada (no se deja quitar). false
      // si no hay ninguna.
      virtual bool inUseNetwork(char* ssid, size_t ssidSize, char* password, size_t passwordSize) {
        return false;
      }
      // Las redes que la placa usa DE VERDAD (las de la tarjeta, o las de
      // secrets.h si la tarjeta no trae seccion wifi), con contrasena: son
      // las "conocidas" para mergeRemoteWifi. Sin ellas, una red compilada
      // no se podria conservar sin volver a escribir su contrasena.
      virtual uint8_t effectiveNetworks(ConfigLoader::WifiNetworkConfig* out, uint8_t max) { return 0; }
      // Agrega a la meta de la configuracion vigente las redes y el broker
      // EFECTIVOS, sin contrasenas: con la tarjeta sin esas secciones son
      // los compilados, y es la unica forma de que el monitor los muestre.
      virtual void describeConnection(JsonDocument& meta) {}
      virtual ~Output() {}
  };

  // La red en uso se quito, pero la placa solo pudo conectarse por ella:
  // la conserva (lo publica MySystem al confirmar la configuracion).
  inline size_t buildNetworkKept(const char* request, const char* ssid, char* buffer, size_t length) {
    JsonDocument doc;
    doc["r"] = request;
    doc["st"] = "red_conservada";
    char message[120];
    snprintf(message, sizeof(message), "las otras redes no conectaron: la placa conserva \"%s\"", ssid);
    doc["msg"] = message;
    if (measureJson(doc) >= length) return 0;
    return serializeJson(doc, buffer, length);
  }

  // Aviso de vuelta atras para el monitor (lo publica MySystem al reconectar).
  inline size_t buildReverted(const char* request, char* buffer, size_t length) {
    JsonDocument doc;
    doc["r"] = request;
    doc["st"] = "revertida";
    doc["msg"] = "con esa configuracion la placa no llego al broker en 5 minutos; volvio a la anterior";
    if (measureJson(doc) >= length) return 0;
    return serializeJson(doc, buffer, length);
  }

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

      // Publica el siguiente mensaje, solo con la cola de publicacion vacia:
      // asi hay a lo sumo un bloque esperando, y una rafaga de telemetria o
      // un pong que caiga encima entra sin desalojarlo. Si no, no avanza y
      // se reintenta en el proximo tick.
      void publishNext(Output& out, const char* configId) {
        if (!_active) return;
        if (!out.publishQueueIdle()) return;
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
        // archivo que mostrar; "src" dice por que (ConfigLoader::loadStatus).
        JsonDocument doc;
        doc["id"] = configId;
        doc["n"] = _total;
        doc["len"] = _length;
        doc["src"] = ConfigLoader::loadStatus();
        out.describeConnection(doc);
        // Si con lo efectivo no entra, sale sin: la meta es lo que importa.
        if (measureJson(doc) >= sizeof(payload)) { doc.remove("ew"); doc.remove("eb"); }
        serializeJson(doc, payload, sizeof(payload));
        if (!out.publishConfigMessage(TopicCurrentMeta, payload, true)) return;
        _active = false;
        LOG_I(REMOTA, "configuracion vigente publicada configId=%s bloques=%u", configId, (unsigned)_total);
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
      // El ultimo pedido aceptado. Si el monitor reenvia un bloque suyo
      // (no le llego a tiempo la confirmacion) se le repite "aceptada": sin
      // esto se le contestaba "incompleta" y el monitor mostraba como
      // fallido un archivo que ya estaba grabado.
      char _doneRequestId[MaxRequestIdLength + 1] = "";
      char _doneNewId[ConfigLoader::ConfigIdLength] = "";

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

        // "parcial" sale por cada bloque: solo en detalle. Los finales que
        // no son exito quedan como advertencia.
        if (strcmp(state, "parcial") == 0) {
          LOG_D(REMOTA, "pedido %s bloque %d recibido", request, index);
        } else if (strcmp(state, "aceptada") == 0 || strcmp(state, "revertida") == 0
                   || strcmp(state, "red_conservada") == 0) {
          LOG_I(REMOTA, "pedido %s -> %s%s%s", request, state, message ? ": " : "", message ? message : "");
        } else {
          LOG_W(REMOTA, "pedido %s -> %s%s%s", request, state, message ? ": " : "", message ? message : "");
        }
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
        // Un experimento con sensores simulados o sin Detector no se graba:
        // al arrancar se descartaria entero (ConfigLoader::load). Mejor
        // decirlo ahora, con el motivo, que dejar en la SD un archivo que la
        // placa no va a usar.
        if (const char* incoherence = ConfigLoader::experimentIncoherence(doc.as<JsonVariantConst>())) {
          _status(out, _requestId, "invalida", -1, incoherence);
          _abort();
          return;
        }

        // Broker: el que venga reemplaza clave por clave al del archivo (las
        // claves que no vengan quedan en el valor compilado, igual que en la
        // tarjeta); sin contrasena conserva la del archivo. Sin seccion, se
        // conserva el del archivo entero.
        char brokerPassword[ConfigLoader::MaxBrokerPassLength] = "";
        const ConfigLoader::BrokerConfig& fileBroker = ConfigLoader::brokerConfig();
        if (doc["broker"].is<JsonObject>()) {
          JsonObject broker = doc["broker"].as<JsonObject>();
          const char* tooLong = nullptr;
          if (strlen(broker["server"] | "") >= ConfigLoader::MaxBrokerHostLength) tooLong = "broker.server es demasiado largo";
          if (strlen(broker["user"] | "") >= ConfigLoader::MaxBrokerUserLength) tooLong = "broker.user es demasiado largo";
          if (strlen(broker["clientId"] | "") >= ConfigLoader::MaxBrokerClientIdLength) tooLong = "broker.clientId es demasiado largo";
          if (strlen(broker["password"] | "") >= ConfigLoader::MaxBrokerPassLength) tooLong = "broker.password es demasiado larga";
          if (!broker["port"].isNull() && !(broker["port"].is<uint16_t>() && broker["port"].as<uint16_t>() > 0))
            tooLong = "broker.port tiene que ser un entero de 1 a 65535";
          if (tooLong != nullptr) {
            _status(out, _requestId, "invalida", -1, tooLong);
            _abort();
            return;
          }
          const char* password = broker["password"].is<const char*>() ? broker["password"].as<const char*>()
                                                                      : fileBroker.password;
          strncpy(brokerPassword, password, sizeof(brokerPassword) - 1);
          broker.remove("password");
        } else if (!doc["broker"].isNull()) {
          _status(out, _requestId, "invalida", -1, "broker tiene que ser un objeto");
          _abort();
          return;
        } else {
          ConfigLoader::addBroker(doc);
          strncpy(brokerPassword, fileBroker.password, sizeof(brokerPassword) - 1);
          if (doc["broker"].is<JsonObject>()) doc["broker"].remove("password");
        }

        // Redes WiFi: la lista final (con contrasenas) va al documento.
        ConfigLoader::WifiNetworkConfig networks[ConfigLoader::MaxWifiNetworks];
        uint8_t networkCount = 0;
        bool hasWifi = doc["wifi"].is<JsonArray>();
        if (!hasWifi && !doc["wifi"].isNull()) {
          _status(out, _requestId, "invalida", -1, "wifi tiene que ser una lista de redes");
          _abort();
          return;
        }
        if (hasWifi) {
          char inUseSsid[ConfigLoader::MaxSsidLength] = "";
          char inUsePassword[ConfigLoader::MaxWifiPassLength] = "";
          out.inUseNetwork(inUseSsid, sizeof(inUseSsid), inUsePassword, sizeof(inUsePassword));
          // Conocidas = las que la placa usa de verdad (tarjeta o secrets.h).
          ConfigLoader::WifiNetworkConfig known[ConfigLoader::MaxWifiNetworks];
          uint8_t knownCount = out.effectiveNetworks(known, ConfigLoader::MaxWifiNetworks);
          if (knownCount == 0) {
            knownCount = ConfigLoader::fileWifiNetworkCount();
            memcpy(known, ConfigLoader::fileWifiNetworks(), sizeof(known[0]) * knownCount);
          }
          bool droppedInUse = false;
          const char* reason = ConfigLoader::mergeRemoteWifi(
            doc["wifi"].as<JsonArrayConst>(), known, knownCount,
            inUseSsid, inUsePassword, networks, networkCount, droppedInUse);
          if (reason != nullptr) {
            _status(out, _requestId, "invalida", -1, reason);
            _abort();
            return;
          }
          // La red en uso se quito: queda de respaldo hasta confirmar otra.
          // Si no se quito, un respaldo viejo sobra (o esta en la lista, o se
          // quito una red que no era la de uso).
          if (droppedInUse) {
            if (!ConfigLoader::writeWifiFallback(_storage, inUseSsid, inUsePassword)) {
              _status(out, _requestId, "error", -1, "no se pudo guardar la red en uso como respaldo; no se toco nada");
              _abort();
              return;
            }
          } else {
            _storage.remove(ConfigLoader::FallbackPath);
          }
        } else {
          networkCount = ConfigLoader::fileWifiNetworkCount();
          memcpy(networks, ConfigLoader::fileWifiNetworks(), sizeof(networks[0]) * networkCount);
        }

        // El id se informa sobre lo que se va a cargar (sin contrasenas ni
        // broker): es exactamente el configId que la placa va a publicar al
        // reiniciar. Por eso se calcula con la lista final de redes y antes de
        // ponerles la contrasena.
        if (networkCount > 0) {
          JsonArray wifi = doc["wifi"].to<JsonArray>();
          for (uint8_t i = 0; i < networkCount; i++) wifi.add<JsonObject>()["ssid"] = networks[i].ssid;
        } else {
          doc.remove("wifi");
        }
        char newId[ConfigLoader::ConfigIdLength];
        ConfigLoader::configIdOf(doc, newId, sizeof(newId));
        if (networkCount > 0) ConfigLoader::writeWifi(doc, networks, networkCount);
        if (brokerPassword[0] != '\0' && doc["broker"].is<JsonObject>()) doc["broker"]["password"] = brokerPassword;

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

        // Respaldo: el config.json vigente pasa a config.prev.json. Sin
        // config.json, un prev de antes quedaria como "lo anterior" y la
        // vuelta atras restauraria un archivo viejo: se borra.
        if (_storage.exists(ConfigLoader::ConfigPath)) {
          if (!_storage.copyFile(ConfigLoader::ConfigPath, PrevPath)) {
            _status(out, _requestId, "error", -1, "no se pudo respaldar config.json; no se toco nada");
            _abort();
            return;
          }
        } else {
          _storage.remove(PrevPath);
        }

        File output = _storage.openWrite(ConfigLoader::ConfigPath);
        size_t written = output ? serializeJson(doc, output) : 0;
        if (output) output.close();
        if (written != size) {
          _status(out, _requestId, "error", -1, "no se pudo escribir config.json (queda el respaldo en config.prev.json)");
          _abort();
          return;
        }

        // A prueba: si tras reiniciar no llega al broker, vuelve sola.
        _storage.writeFile(TrialPath, _requestId);
        _storage.remove(RevertedPath);
        _status(out, _requestId, "aceptada", -1, "la placa se reinicia sola en la pantalla principal", newId);
        out.onConfigAccepted();
        strncpy(_doneRequestId, _requestId, MaxRequestIdLength);
        _doneRequestId[MaxRequestIdLength] = '\0';
        strncpy(_doneNewId, newId, sizeof(_doneNewId) - 1);
        _doneNewId[sizeof(_doneNewId) - 1] = '\0';
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
          LOG_W(REMOTA, "mensaje ilegible en config/set: se ignora");
          return;
        }
        const char* request = doc["r"] | "";
        if (request[0] == '\0' || strlen(request) > MaxRequestIdLength
            || !doc["i"].is<uint16_t>() || !doc["n"].is<uint16_t>() || !doc["d"].is<const char*>()) {
          LOG_W(REMOTA, "mensaje incompleto en config/set: se ignora");
          return;
        }
        uint16_t index = doc["i"];
        uint16_t total = doc["n"];
        const char* data = doc["d"];

        if (_doneRequestId[0] != '\0' && strcmp(request, _doneRequestId) == 0) {
          _status(out, request, "aceptada", -1, "la placa se reinicia sola en la pantalla principal", _doneNewId);
          return;
        }

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
