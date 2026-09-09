#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "sdstorage.hpp"
#include "configurationoptions.hpp"
#include "intervals.hpp"
#include "topics.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_CONFIGLOADER = true;

// Lee /biosoft/config.json de la SD y puebla con el los valores que hasta
// ahora eran constantes de compilacion. Contrato completo en
// docs/config-schema.md.
//
// PRINCIPIO CENTRAL: los defaults compilados nunca desaparecen. Este modulo
// solo PISA lo que el archivo trae. Sin tarjeta, sin archivo, con JSON
// corrupto o con una seccion ausente, el sistema arranca igual con los
// valores del firmware -- nunca se bloquea el arranque por configuracion.
// Por eso load() no devuelve "fallo": devuelve si llego a aplicar algo, y
// el llamador no tiene que hacer nada distinto en cada caso.
//
// Un JSON que no parsea se descarta ENTERO, no a medias: aplicar la mitad de
// un archivo corrupto dejaria el equipo en una combinacion de parametros que
// nadie eligio ni audito.
//
// ALCANCE ACTUAL: `menus` e `intervals.esp32` se aplican localmente (fase 1).
// `intervals.mega`, `control`, `coils` y `detector.sources` se parsean y
// quedan guardados acá, pero esta clase NO los aplica -- son del Mega, y
// viajan por el protocolo serie fragmentado que dispara
// MySystem::onSerialConnected() (ver mysystem.hpp::_sendMegaConfig() y
// docs/config-schema.md seccion 10). `currentSensors` y `telemetry` siguen
// sin implementar.
namespace ConfigLoader {

  constexpr const char* ConfigPath = "/biosoft/config.json";
  constexpr uint8_t MaxCoils = 4;

  // Cada valor de una seccion que viaja al Mega se guarda con su propio flag
  // `has`: distingue "el archivo no traia esta clave" (el Mega se queda con
  // SU default compilado) de "el archivo la traia en 0/invalida" (ya
  // rechazado acá, tampoco se manda). Solo se envian al Mega las claves con
  // has=true.
  struct MegaIntervalValue {
    bool has = false;
    unsigned long value = 0;
  };

  struct MegaIntervalsConfig {
    MegaIntervalValue ping;
    MegaIntervalValue sendState;
    MegaIntervalValue measureTemperature;
    MegaIntervalValue measureCurrent;
    MegaIntervalValue measureMagneticField;
    MegaIntervalValue updateProgress;
    MegaIntervalValue sendFlags;
    MegaIntervalValue sendResult;
    MegaIntervalValue settlingTime;
  };

  struct ControlConfig {
    bool hasKp = false;
    float kp = 0.0f;
    bool hasMaxStep = false;
    float maxStep = 0.0f;
    bool hasDeadBand = false;
    float deadBand = 0.0f;
    // false = el lazo mide pero no actua sobre el PWM ("solo sensado").
    bool hasEnabled = false;
    bool enabled = true;
  };

  constexpr uint8_t MaxDetectorSources = 2;   // CEM1 y TEMP1, fijas por diseño
  constexpr uint8_t MaxSensorNameLength = 16;

  struct RuleConfig {
    bool has = false;
    uint16_t threshold = 0;
    uint16_t cooldown = 0;
    uint16_t maxEvents = 0;
  };

  // Los rangos normal/critico NO estan acá: no salen del archivo sino de lo
  // que elige el operador en la pantalla, y viajan en el comando start.
  // El archivo solo configura el entorno de la fuente (ver seccion 1.2 del
  // esquema).
  struct SourceConfigEntry {
    char name[ConfigurationOptions::MaxLabelLength] = "";
    bool hasEnabled = false;
    bool enabled = true;
    bool hasSensor = false;
    char sensor[MaxSensorNameLength] = "";
    bool hasBufferSize = false;
    unsigned long bufferSize = 0;
    bool hasCriticalMultiplier = false;
    float criticalMultiplier = 0.0f;
    RuleConfig critical;
    RuleConfig streak;
    RuleConfig frequency;
  };

  struct CoilConfig {
    char name[ConfigurationOptions::MaxLabelLength] = "";
    bool hasEnabled = false;
    bool enabled = true;
    bool hasCalibrationFactor = false;
    float calibrationFactor = 1.0f;
  };

  // La version del esquema que este firmware entiende. Un archivo con otra
  // version se descarta entero, igual que uno corrupto: es preferible
  // arrancar con defaults conocidos antes que interpretar claves que pueden
  // haber cambiado de significado entre versiones.
  constexpr int SupportedSchemaVersion = 1;

  // El archivo de ejemplo pesa ~4.8 KB; 8 KB deja margen para crecer sin
  // acercarse a nada critico en la ESP32. Si el archivo no entra,
  // SdStorage::readFile devuelve false y lo dice en el log -- por eso el
  // truncado no puede confundirse con un JSON invalido.
  constexpr size_t MaxFileSize = 8192;

  inline MegaIntervalsConfig _megaIntervalsConfig;
  inline ControlConfig _controlConfig;
  inline CoilConfig _coilConfigs[MaxCoils];
  inline uint8_t _coilConfigCount = 0;
  inline SourceConfigEntry _sourceConfigs[MaxDetectorSources];
  inline uint8_t _sourceConfigCount = 0;

  // MySystem los lee para armar los frames config_intervals/config_control/
  // config_coil al reconectar (ver mysystem.hpp::_sendMegaConfig()).
  inline const MegaIntervalsConfig& megaIntervalsConfig() { return _megaIntervalsConfig; }
  inline const ControlConfig& controlConfig() { return _controlConfig; }
  inline const CoilConfig* coilConfigs() { return _coilConfigs; }
  inline uint8_t coilConfigCount() { return _coilConfigCount; }
  inline const SourceConfigEntry* sourceConfigs() { return _sourceConfigs; }
  inline uint8_t sourceConfigCount() { return _sourceConfigCount; }

  namespace {

    // Copia una cadena del JSON al buffer del label, truncando si hace
    // falta. Las cadenas del documento mueren con el JsonDocument, asi que
    // apuntarlas en vez de copiarlas dejaria punteros colgados.
    inline void copyLabel(char* dest, const char* source) {
      strncpy(dest, source, ConfigurationOptions::MaxLabelLength - 1);
      dest[ConfigurationOptions::MaxLabelLength - 1] = '\0';
    }

    inline void logMenuLoaded(const char* name, uint8_t loaded, size_t available) {
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] menu "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, name);
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F(": "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, loaded);
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F(" opciones"));
      if (available > loaded) {
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F(" (el archivo traia "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, available);
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F(", se ignora el resto por el tope de "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, ConfigurationOptions::MaxOptions);
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F(")"));
      }
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, );
    }

    // Un menu presente pero vacio se trata como ausente: dejar un dropdown
    // sin opciones haria imposible configurar el experimento desde la
    // pantalla, que es peor que ignorar la seccion.
    inline bool menuIsUsable(JsonArrayConst array, const char* name) {
      if (array.isNull()) return false;
      if (array.size() == 0) {
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] menu "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, name);
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" vacio: se conservan los defaults"));
        return false;
      }
      return true;
    }

    // Un intervalo de 0 o negativo no es una configuracion agresiva sino una
    // tarea que nunca corre (o que corre en cada tick): se rechaza y queda
    // el default.
    inline void applyInterval(JsonVariantConst value, unsigned long& target, const char* name) {
      if (!value.is<unsigned long>()) return;

      unsigned long parsed = value.as<unsigned long>();
      if (parsed == 0) {
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] intervalo "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, name);
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" en 0: se ignora"));
        return;
      }
      target = parsed;
    }

    inline void loadMenus(JsonObjectConst menus) {
      if (menus.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'menus': se conservan los defaults"));
        return;
      }

      JsonArrayConst intensity = menus["fieldIntensity"];
      if (menuIsUsable(intensity, "fieldIntensity")) {
        uint8_t i = 0;
        for (JsonObjectConst option : intensity) {
          if (i >= ConfigurationOptions::MaxOptions) break;
          copyLabel(ConfigurationOptions::optionsFieldIntensity[i].label, option["label"] | "");
          ConfigurationOptions::optionsFieldIntensity[i].intensity = option["value"] | 0.0f;
          i++;
        }
        ConfigurationOptions::countFieldIntensity = i;
        logMenuLoaded("fieldIntensity", i, intensity.size());
      }

      JsonArrayConst frequency = menus["frequency"];
      if (menuIsUsable(frequency, "frequency")) {
        uint8_t i = 0;
        for (JsonObjectConst option : frequency) {
          if (i >= ConfigurationOptions::MaxOptions) break;
          copyLabel(ConfigurationOptions::optionsFrequency[i].label, option["label"] | "");
          ConfigurationOptions::optionsFrequency[i].freq = option["value"] | 0;
          i++;
        }
        ConfigurationOptions::countFrequency = i;
        logMenuLoaded("frequency", i, frequency.size());
      }

      JsonArrayConst duration = menus["duration"];
      if (menuIsUsable(duration, "duration")) {
        uint8_t i = 0;
        for (JsonObjectConst option : duration) {
          if (i >= ConfigurationOptions::MaxOptions) break;
          copyLabel(ConfigurationOptions::optionsDuration[i].label, option["label"] | "");
          ConfigurationOptions::optionsDuration[i].duration = option["value"] | 0UL;
          i++;
        }
        ConfigurationOptions::countDuration = i;
        logMenuLoaded("duration", i, duration.size());
      }

      JsonArrayConst tolerance = menus["fieldTolerance"];
      if (menuIsUsable(tolerance, "fieldTolerance")) {
        uint8_t i = 0;
        for (JsonObjectConst option : tolerance) {
          if (i >= ConfigurationOptions::MaxOptions) break;
          copyLabel(ConfigurationOptions::optionsTolFieldIntensity[i].label, option["label"] | "");
          ConfigurationOptions::optionsTolFieldIntensity[i].tol = option["value"] | 0;
          i++;
        }
        ConfigurationOptions::countTolFieldIntensity = i;
        logMenuLoaded("fieldTolerance", i, tolerance.size());
      }

      JsonArrayConst normalTemp = menus["temperatureNormal"];
      if (menuIsUsable(normalTemp, "temperatureNormal")) {
        uint8_t i = 0;
        for (JsonObjectConst option : normalTemp) {
          if (i >= ConfigurationOptions::MaxOptions) break;
          copyLabel(ConfigurationOptions::optionsRangeNormalTemperature[i].label, option["label"] | "");
          ConfigurationOptions::optionsRangeNormalTemperature[i].tmin = option["min"] | 0.0f;
          ConfigurationOptions::optionsRangeNormalTemperature[i].tmax = option["max"] | 0.0f;
          i++;
        }
        ConfigurationOptions::countRangeNormalTemperature = i;
        logMenuLoaded("temperatureNormal", i, normalTemp.size());
      }

      JsonArrayConst criticalTemp = menus["temperatureCritical"];
      if (menuIsUsable(criticalTemp, "temperatureCritical")) {
        uint8_t i = 0;
        for (JsonObjectConst option : criticalTemp) {
          if (i >= ConfigurationOptions::MaxOptions) break;
          copyLabel(ConfigurationOptions::optionsRangeCriticalTemperature[i].label, option["label"] | "");
          ConfigurationOptions::optionsRangeCriticalTemperature[i].tmin = option["min"] | 0.0f;
          ConfigurationOptions::optionsRangeCriticalTemperature[i].tmax = option["max"] | 0.0f;
          i++;
        }
        ConfigurationOptions::countRangeCriticalTemperature = i;
        logMenuLoaded("temperatureCritical", i, criticalTemp.size());
      }
    }

    inline void loadIntervals(JsonObjectConst intervals) {
      if (intervals.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'intervals': se conservan los defaults"));
        return;
      }

      // Solo la porcion de la ESP32 -- `intervals.mega` la lee
      // loadMegaIntervals(), mas abajo, porque no se aplica local sino que
      // se reenvia.
      JsonObjectConst esp32 = intervals["esp32"];
      if (esp32.isNull()) return;

      applyInterval(esp32["updateScreens"], Intervals::UpdateScreens, "updateScreens");
      applyInterval(esp32["reSendStart"], Intervals::ReSendStart, "reSendStart");
      applyInterval(esp32["reSendStop"], Intervals::ReSendStop, "reSendStop");
      applyInterval(esp32["reSendReset"], Intervals::ReSendReset, "reSendReset");
      applyInterval(esp32["updateProgress"], Intervals::UpdateProgress, "updateProgress");

      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] intervalos de la ESP32 aplicados"));
    }

    // Mismo criterio de rechazo que applyInterval (0/negativo se ignora),
    // pero acá NO hay valor local que pisar: solo se marca `has=true` para
    // que _sendMegaConfig() sepa que clave incluir en el frame
    // config_intervals. La clave que no vino, o vino invalida, simplemente
    // no se manda -- el Mega se queda con su propio default compilado.
    inline void applyMegaInterval(JsonVariantConst value, MegaIntervalValue& target, const char* name) {
      target = MegaIntervalValue();
      if (!value.is<unsigned long>()) return;

      unsigned long parsed = value.as<unsigned long>();
      if (parsed == 0) {
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] intervalo (mega) "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, name);
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" en 0: se ignora"));
        return;
      }
      target.has = true;
      target.value = parsed;
    }

    inline void loadMegaIntervals(JsonObjectConst intervals) {
      _megaIntervalsConfig = MegaIntervalsConfig();
      if (intervals.isNull()) return;

      JsonObjectConst mega = intervals["mega"];
      if (mega.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'intervals.mega': el Mega arranca con sus defaults"));
        return;
      }

      applyMegaInterval(mega["ping"], _megaIntervalsConfig.ping, "ping");
      applyMegaInterval(mega["sendState"], _megaIntervalsConfig.sendState, "sendState");
      applyMegaInterval(mega["measureTemperature"], _megaIntervalsConfig.measureTemperature, "measureTemperature");
      applyMegaInterval(mega["measureCurrent"], _megaIntervalsConfig.measureCurrent, "measureCurrent");
      applyMegaInterval(mega["measureMagneticField"], _megaIntervalsConfig.measureMagneticField, "measureMagneticField");
      applyMegaInterval(mega["updateProgress"], _megaIntervalsConfig.updateProgress, "updateProgress");
      applyMegaInterval(mega["sendFlags"], _megaIntervalsConfig.sendFlags, "sendFlags");
      applyMegaInterval(mega["sendResult"], _megaIntervalsConfig.sendResult, "sendResult");
      applyMegaInterval(mega["settlingTime"], _megaIntervalsConfig.settlingTime, "settlingTime");

      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] intervalos del Mega leidos, se reenvian al conectar"));
    }

    inline void loadControl(JsonObjectConst control) {
      _controlConfig = ControlConfig();
      if (control.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'control': el Mega arranca con sus defaults"));
        return;
      }

      if (control["kp"].is<float>()) {
        _controlConfig.hasKp = true;
        _controlConfig.kp = control["kp"].as<float>();
      }
      if (control["maxStep"].is<float>()) {
        _controlConfig.hasMaxStep = true;
        _controlConfig.maxStep = control["maxStep"].as<float>();
      }
      if (control["deadBand"].is<float>()) {
        _controlConfig.hasDeadBand = true;
        _controlConfig.deadBand = control["deadBand"].as<float>();
      }
      if (control["enabled"].is<bool>()) {
        _controlConfig.hasEnabled = true;
        _controlConfig.enabled = control["enabled"].as<bool>();
      }

      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] seccion 'control' leida, se reenvia al conectar"));
    }

    inline void loadRule(JsonObjectConst rules, const char* name, RuleConfig& target) {
      JsonObjectConst rule = rules[name];
      if (rule.isNull()) return;

      // Las tres claves van juntas o no va ninguna: el Mega valida y
      // descarta la regla ENTERA si algo cae fuera de rango, asi que
      // mandar media regla solo puede terminar en un rechazo silencioso.
      if (!rule["threshold"].is<uint16_t>()) return;
      if (!rule["cooldown"].is<uint16_t>()) return;
      if (!rule["maxEvents"].is<uint16_t>()) return;

      target.has = true;
      target.threshold = rule["threshold"].as<uint16_t>();
      target.cooldown = rule["cooldown"].as<uint16_t>();
      target.maxEvents = rule["maxEvents"].as<uint16_t>();
    }

    // Los rangos de validacion (bufferSize 8-32, threshold 1-bufferSize,
    // etc.) NO se chequean acá: los aplica el Mega, que es quien manda
    // sobre su propio Detector. Acá solo se extrae lo que el JSON trae con
    // el tipo correcto.
    inline void loadDetectorSources(JsonObjectConst detector) {
      _sourceConfigCount = 0;
      if (detector.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'detector': el Mega arranca con sus defaults"));
        return;
      }

      JsonArrayConst sources = detector["sources"];
      if (sources.isNull()) return;

      for (JsonObjectConst source : sources) {
        if (_sourceConfigCount >= MaxDetectorSources) {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] 'detector.sources' excede el tope de 2: se ignora el resto"));
          break;
        }

        const char* name = source["name"] | "";
        if (name[0] == '\0') {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] fuente sin 'name': se ignora"));
          continue;
        }

        SourceConfigEntry& entry = _sourceConfigs[_sourceConfigCount];
        entry = SourceConfigEntry();
        copyLabel(entry.name, name);

        if (source["enabled"].is<bool>()) {
          entry.hasEnabled = true;
          entry.enabled = source["enabled"].as<bool>();
        }
        if (source["sensor"].is<const char*>()) {
          entry.hasSensor = true;
          strncpy(entry.sensor, source["sensor"].as<const char*>(), MaxSensorNameLength - 1);
          entry.sensor[MaxSensorNameLength - 1] = '\0';
        }
        if (source["bufferSize"].is<unsigned long>()) {
          entry.hasBufferSize = true;
          entry.bufferSize = source["bufferSize"].as<unsigned long>();
        }

        // `range.mode` no se reenvia: que fuente deriva sus rangos del
        // target y cual los toma tal cual del operador es fijo por diseño
        // en el Mega (CEM1 derivada, TEMP1 no). Del bloque solo viaja el
        // multiplicador.
        JsonObjectConst range = source["range"];
        if (!range.isNull() && range["criticalMultiplier"].is<float>()) {
          entry.hasCriticalMultiplier = true;
          entry.criticalMultiplier = range["criticalMultiplier"].as<float>();
        }

        JsonObjectConst rules = source["rules"];
        if (!rules.isNull()) {
          loadRule(rules, "critical", entry.critical);
          loadRule(rules, "streak", entry.streak);
          loadRule(rules, "frequency", entry.frequency);
        }

        _sourceConfigCount++;
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] 'detector.sources': "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, _sourceConfigCount);
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" fuentes leidas, se reenvian al conectar"));
    }

    // Un campo ausente conserva su default compilado (nombre Y enabled): es
    // el mismo criterio del resto del esquema, y acá importa especialmente
    // porque el nombre es la clave que espera el Decoder de Datacake.
    inline void applyTelemetryField(JsonObjectConst fields, const char* key, Topics::TelemetryField& target) {
      JsonObjectConst field = fields[key];
      if (field.isNull()) return;

      if (field["enabled"].is<bool>()) {
        target.enabled = field["enabled"].as<bool>();
      }
      if (field["name"].is<const char*>()) {
        strncpy(target.name, field["name"].as<const char*>(), Topics::MaxFieldNameLength - 1);
        target.name[Topics::MaxFieldNameLength - 1] = '\0';
      }
    }

    // A diferencia de control/coils/detector, esta seccion es del ESP32 y se
    // aplica acá mismo: no viaja al Mega.
    inline void loadTelemetry(JsonObjectConst telemetry) {
      if (telemetry.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'telemetry': se conservan los defaults"));
        return;
      }

      if (telemetry["topic"].is<const char*>()) {
        strncpy(Topics::Telemetry, telemetry["topic"].as<const char*>(), Topics::MaxTopicLength - 1);
        Topics::Telemetry[Topics::MaxTopicLength - 1] = '\0';
      }

      JsonObjectConst intervals = telemetry["intervals"];
      if (!intervals.isNull()) {
        applyInterval(intervals["measures"], Intervals::PublishMeasures, "telemetry.measures");
        applyInterval(intervals["status"], Intervals::PublishStatus, "telemetry.status");
      }

      JsonObjectConst fields = telemetry["fields"];
      if (!fields.isNull()) {
        applyTelemetryField(fields, "magneticField", Topics::MagneticField);
        applyTelemetryField(fields, "temperature", Topics::Temperature);
        applyTelemetryField(fields, "current", Topics::Current);
        applyTelemetryField(fields, "health", Topics::Health);
        applyTelemetryField(fields, "progress", Topics::Progress);
        applyTelemetryField(fields, "elapsedTime", Topics::ElapsedTime);
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] telemetria aplicada, topic="));
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, Topics::Telemetry);
    }

    // Rango 0.1-5.0 de calibrationFactor NO se valida acá -- lo hace
    // CoilChannel::setCalibrationFactor() del lado Mega, que es quien tiene
    // la autoridad sobre sus propios canales. Acá solo se extrae lo que el
    // JSON trae con el tipo correcto.
    inline void loadCoils(JsonArrayConst coils) {
      _coilConfigCount = 0;
      if (coils.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'coils': el Mega arranca con sus defaults"));
        return;
      }

      for (JsonObjectConst coil : coils) {
        if (_coilConfigCount >= MaxCoils) {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] 'coils' excede el tope de 4: se ignora el resto"));
          break;
        }

        const char* name = coil["name"] | "";
        if (name[0] == '\0') {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] entrada de 'coils' sin 'name': se ignora"));
          continue;
        }

        CoilConfig& entry = _coilConfigs[_coilConfigCount];
        entry = CoilConfig();
        copyLabel(entry.name, name);

        if (coil["enabled"].is<bool>()) {
          entry.hasEnabled = true;
          entry.enabled = coil["enabled"].as<bool>();
        }
        if (coil["calibrationFactor"].is<float>()) {
          entry.hasCalibrationFactor = true;
          entry.calibrationFactor = coil["calibrationFactor"].as<float>();
        }

        _coilConfigCount++;
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] 'coils': "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, _coilConfigCount);
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" canales leidos, se reenvian al conectar"));
    }

  }

  // Devuelve true si se llego a aplicar configuracion del archivo. Un false
  // NO es un error que haya que manejar: significa que quedaron vigentes los
  // defaults compilados, que es un estado de arranque perfectamente valido.
  inline bool load(SdStorage& storage) {
    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] ----------------------------------------"));

    if (!storage.isReady()) {
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin tarjeta SD: se usan los defaults compilados"));
      return false;
    }

    if (!storage.exists(ConfigPath)) {
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] no existe "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, ConfigPath);
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(": se usan los defaults compilados"));
      return false;
    }

    // static y no local: 8 KB en el stack desbordarian la tarea de Arduino
    // (loopTask arranca con 8192 bytes en total). Como contrapartida el
    // buffer queda reservado toda la ejecucion aunque se use una sola vez;
    // es el precio de no arriesgar un stack overflow en el arranque, y en la
    // ESP32 esos 8 KB no son un problema.
    static char buffer[MaxFileSize];
    size_t length = 0;

    if (!storage.readFile(ConfigPath, buffer, MaxFileSize, &length)) {
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] no se pudo leer el archivo: se usan los defaults"));
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buffer);
    if (error) {
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] JSON invalido ("));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, error.c_str());
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("): se descarta entero, se usan los defaults"));
      return false;
    }

    int version = doc["schemaVersion"] | 0;
    if (version != SupportedSchemaVersion) {
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] schemaVersion "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, version);
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F(" no soportada (se espera "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, SupportedSchemaVersion);
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("): se descarta el archivo"));
      return false;
    }

    DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] archivo leido, "));
    DEBUG_PRINT(DEBUG_CONFIGLOADER, length);
    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" bytes"));

    loadMenus(doc["menus"]);
    loadIntervals(doc["intervals"]);
    loadMegaIntervals(doc["intervals"]);
    loadControl(doc["control"]);
    loadCoils(doc["coils"]);
    loadDetectorSources(doc["detector"]);
    loadTelemetry(doc["telemetry"]);

    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] configuracion aplicada"));
    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] ----------------------------------------"));
    return true;
  }

};
