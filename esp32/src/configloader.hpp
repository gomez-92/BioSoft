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
// `intervals.mega`, `control`, `coils`, `detector.sources` y
// `currentSensors` se parsean y quedan guardados acá, pero esta clase NO los
// aplica -- son del Mega, y viajan por el protocolo serie fragmentado que
// dispara MySystem::onSerialConnected() (ver mysystem.hpp::_sendMegaConfig()
// y docs/config-schema.md seccion 10). `telemetry` si se aplica acá, como
// las dos secciones de la fase 1.
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

  // Un punto del mapa de calibracion (control.map): para esa intensidad y esa
  // frecuencia, el duty comun y el balance del campo nulo. El rango de cada
  // valor lo valida el Mega (controlmap.hpp); aca solo se verifica el tipo.
  struct ControlMapPoint {
    float intensity = 0.0f;
    float frequency = 0.0f;
    float duty = 0.0f;
    float balance = 0.0f;
  };
  constexpr uint8_t MaxMapPoints = ConfigurationOptions::MaxFieldIntensityOptions * ConfigurationOptions::MaxFrequencyOptions;

  struct ControlConfig {
    bool hasBalanceMax = false;
    float balanceMax = 0.0f;
    // Puntos del mapa, compactados: un punto al que le falta una clave no
    // ocupa lugar, asi que el indice que viaja al Mega es el de esta lista.
    uint8_t mapCount = 0;
    ControlMapPoint map[MaxMapPoints];
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

  // Credenciales de conectividad. Tope de redes igual al de
  // WiFiConfig::MAX_NETWORKS, que es privado y no se puede leer desde aca.
  constexpr uint8_t MaxWifiNetworks = 5;
  constexpr uint8_t MaxSsidLength = 33;        // 32 caracteres + terminador
  constexpr uint8_t MaxWifiPassLength = 64;    // 63 (maximo WPA2) + terminador
  constexpr uint8_t MaxBrokerHostLength = 64;
  constexpr uint8_t MaxBrokerUserLength = 32;
  constexpr uint8_t MaxBrokerPassLength = 64;
  constexpr uint8_t MaxBrokerClientIdLength = 32;

  // Las cadenas se COPIAN a estos buffers y no se apuntan al JsonDocument:
  // WiFiConfig y MqttConfig guardan punteros, no copias, y el documento
  // muere al terminar de parsear. Los buffers son de duracion estatica
  // (namespace inline), asi que sobreviven todo lo que dura el programa.
  struct WifiNetworkConfig {
    char ssid[MaxSsidLength] = "";
    char password[MaxWifiPassLength] = "";
  };

  struct BrokerConfig {
    char server[MaxBrokerHostLength] = "";
    uint16_t port = 0;
    char user[MaxBrokerUserLength] = "";
    char password[MaxBrokerPassLength] = "";
    char clientId[MaxBrokerClientIdLength] = "";
  };

  constexpr uint8_t MaxDetectorSources = 2;   // CEM1 y TEMP1, fijas por diseño
  constexpr uint8_t MaxSensorNameLength = 16;
  constexpr uint8_t MaxCurrentSensors = 4;    // 2 pares diferenciales x 2 modulos ADS1115

  // El (address, channel) identifica la entrada fisica y es lo que el Mega
  // usa para matchear cada entrada con su slot; el resto son parametros.
  struct CurrentSensorConfigEntry {
    char name[ConfigurationOptions::MaxLabelLength] = "";
    uint8_t address = 0;
    uint8_t channel = 0;
    bool hasEnabled = false;
    bool enabled = false;
    bool hasRatedCurrent = false;
    float ratedCurrent = 0.0f;
    bool hasRatedVoltage = false;
    float ratedVoltage = 0.0f;
    bool hasCalibration = false;
    float calibration = 0.0f;
    bool hasSampleRate = false;
    uint16_t sampleRate = 0;
    bool hasIntegrationTime = false;
    uint16_t integrationTimeMs = 0;
  };

  struct RuleConfig {
    bool has = false;
    uint16_t threshold = 0;
    uint16_t cooldown = 0;
    uint16_t maxEvents = 0;
  };

  // Senal sintetica de una fuente (`detector.sources[].scenario`, tarjeta
  // 22). Los valores son NIVELES relativos a los rangos de la corrida, no
  // valores fisicos: el Mega los convierte en cada start
  // (mega2560/src/scenario.hpp). Cada clave lleva su `has`: solo viaja lo
  // que el archivo trae, y el resto queda en el default del Mega.
  struct ScenarioValue {
    bool has = false;
    float value = 0.0f;
  };

  struct ScenarioConfig {
    bool present = false;
    ScenarioValue base;
    ScenarioValue noise;
    ScenarioValue ramp;
    ScenarioValue stepAt;
    ScenarioValue step;
    ScenarioValue oscAmp;
    ScenarioValue oscPeriod;
    ScenarioValue dropAt;
    ScenarioValue setpointDuty;
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
    // Lecturas perdidas antes del corte por silencio (tarjeta 25). El rango
    // 1-20 lo valida el Mega.
    bool hasMaxMissedSamples = false;
    unsigned long maxMissedSamples = 0;
    RuleConfig critical;
    RuleConfig streak;
    RuleConfig frequency;
    ScenarioConfig scenario;
  };

  // Interruptor general del Detector (`detector.enabled`). Viaja al Mega en
  // config_detector solo si el archivo lo trae: sin la clave, el Mega se
  // queda con su default (encendido).
  struct DetectorConfig {
    bool hasEnabled = false;
    bool enabled = true;
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
  inline CurrentSensorConfigEntry _currentSensorConfigs[MaxCurrentSensors];
  inline uint8_t _currentSensorConfigCount = 0;
  inline WifiNetworkConfig _wifiNetworks[MaxWifiNetworks];
  inline uint8_t _wifiNetworkCount = 0;
  inline BrokerConfig _brokerConfig;
  // `runType`: si las corridas de esta configuracion son de prueba. Default
  // false (experimento) -- es lo que corre sin tarjeta, sin archivo o con un
  // valor que no se entiende. Ver loadRunType().
  inline bool _testRun = false;
  // `requireMega`: si Iniciar exige la placa de control conectada. Default
  // true, el unico valor seguro en produccion; false es la prueba de envio
  // de datos sin Mega (banco). Ver loadRequireMega().
  inline bool _requireMega = true;
  inline DetectorConfig _detectorConfig;

  // MySystem los lee para armar los frames config_intervals/config_control/
  // config_coil al reconectar (ver mysystem.hpp::_sendMegaConfig()).
  inline const MegaIntervalsConfig& megaIntervalsConfig() { return _megaIntervalsConfig; }
  inline const ControlConfig& controlConfig() { return _controlConfig; }
  inline const CoilConfig* coilConfigs() { return _coilConfigs; }
  inline uint8_t coilConfigCount() { return _coilConfigCount; }
  inline const SourceConfigEntry* sourceConfigs() { return _sourceConfigs; }
  inline uint8_t sourceConfigCount() { return _sourceConfigCount; }
  inline const CurrentSensorConfigEntry* currentSensorConfigs() { return _currentSensorConfigs; }
  inline uint8_t currentSensorConfigCount() { return _currentSensorConfigCount; }

  // Los lee SoftEsp32.ino al armar WiFiConfig y MqttConfig. Con 0 redes o
  // con un campo vacio, el sketch se queda con lo compilado en secrets.h.
  inline const WifiNetworkConfig* wifiNetworks() { return _wifiNetworks; }
  inline uint8_t wifiNetworkCount() { return _wifiNetworkCount; }
  inline const BrokerConfig& brokerConfig() { return _brokerConfig; }
  inline bool isTestRun() { return _testRun; }
  inline bool requireMega() { return _requireMega; }
  inline const DetectorConfig& detectorConfig() { return _detectorConfig; }

  // true si alguna fuente pide un driver que NO mide nada real ("sim" o
  // "scenario"). Esas corridas salen marcadas TEST aunque runType diga
  // normal: datos fabricados nunca pueden entrar al historico como un
  // experimento. Sin la clave `sensor` cuenta como real: es el default
  // compilado del Mega para las dos fuentes desde la tarjeta 22.
  inline bool usesSyntheticSensors() {
    for (uint8_t i = 0; i < _sourceConfigCount; i++) {
      const SourceConfigEntry& source = _sourceConfigs[i];
      if (!source.hasSensor) continue;
      if (strcmp(source.sensor, "sim") == 0 || strcmp(source.sensor, "scenario") == 0) return true;
    }
    return false;
  }

  // Lo que decide la marca TEST y el aviso MODO PRUEBA: la declaracion del
  // operador (runType) o datos fabricados.
  inline bool marksRunsAsTest() { return _testRun || usesSyntheticSensors(); }

  // ---------------------------------------------------------------------
  // Trazabilidad (tarjeta 23): con que configuracion corrio cada corrida.
  // ---------------------------------------------------------------------

  // Identificador de la configuracion cargada: CRC32 del JSON ya leido SIN
  // `wifi` ni `broker` (cambiar de red no cambia el experimento), en 8
  // digitos hex. "default" sin tarjeta, sin archivo, o con un archivo que se
  // descarto entero (JSON invalido, schemaVersion no soportada): en todos
  // esos casos lo que corre son los defaults compilados.
  //
  // Lo calcula SOLO la placa. El monitor no lo recalcula, lo guarda: asi no
  // hay dos implementaciones del mismo hash que puedan divergir. Se descarto
  // que el generador escriba el id adentro del archivo porque un archivo
  // editado a mano conservaria un id que ya no describe su contenido.
  constexpr size_t ConfigIdLength = 9;   // 8 hex + '\0'
  inline char _configId[ConfigIdLength] = "default";
  inline const char* configId() { return _configId; }

  // Por que corre lo que corre. "default" junta cinco causas que para el
  // operador no son lo mismo: sin tarjeta o sin archivo es un arranque
  // normal, pero un archivo con JSON roto o una schemaVersion vieja ESTA en
  // la tarjeta y se ignora entero -- quien lo grabo cree que corre su
  // configuracion. Viaja al monitor en el meta de la configuracion vigente y
  // en el pong.
  //   "ok"         se aplico config.json
  //   "nosd"       no hay tarjeta (o no se pudo montar)
  //   "nofile"     la tarjeta no tiene /biosoft/config.json
  //   "unreadable" no se pudo leer (o no entra en el buffer de 8 KB)
  //   "invalid"    JSON invalido: descartado entero
  //   "schema"     schemaVersion no soportada: descartado entero
  inline const char* _loadStatus = "nosd";
  inline const char* loadStatus() { return _loadStatus; }

  // El texto EXACTO que se hasheo: el JSON leido, sin wifi ni broker, en una
  // sola linea. Es lo que la placa publica como "configuracion vigente"
  // (tarjeta 24): el monitor la reconstruye, verifica que su CRC32 de el
  // configId y la guarda. Vacio con los defaults compilados -- no hay un
  // archivo que mostrar.
  //
  // Es el MISMO buffer en el que load() lee el archivo: un segundo static de
  // 8 KB sale del mismo DRAM que el heap, y el heap es lo que el handshake
  // TLS necesita (tarjetas 17 y 18). Por eso load() parsea copiando los
  // strings (deserializeJson con const char*) y recien despues escribe aca
  // el texto sin credenciales.
  inline char _currentText[MaxFileSize] = "";
  inline const char* currentText() { return _currentText; }

  // Relajaciones activas, como mascara de bits en `targets` (`RLX`). Un
  // entero y no texto por el tope de payload. Cada bit es un HECHO de la
  // configuracion, no un juicio: si es preocupante en una corrida normal lo
  // decide WarningMask.
  namespace Relaxations {
    constexpr uint8_t DetectorOff     = 1 << 0;  // detector.enabled = false
    constexpr uint8_t Temp1Unwatched  = 1 << 1;  // TEMP1 no vigilada
    constexpr uint8_t Cem1Unwatched   = 1 << 2;  // CEM1 no vigilada (DEFAULT de fabrica)
    constexpr uint8_t SyntheticSensor = 1 << 3;  // alguna fuente en sim o scenario
    constexpr uint8_t Temp1NoSensor   = 1 << 4;  // TEMP1 con sensor "none"
    constexpr uint8_t ControlOff      = 1 << 5;  // control.enabled = false
    constexpr uint8_t MegaNotRequired = 1 << 6;  // requireMega = false

    // Las que ameritan aviso en una corrida declarada normal. CEM1 sin
    // vigilar queda afuera: es el default de fabrica (sus reglas son
    // provisorias, sin calibrar), y avisarlo marcaria a todo experimento.
    constexpr uint8_t WarningMask = 0x7F & ~Cem1Unwatched;
  };

  inline uint8_t relaxationMask();

  namespace {

    // Copia una cadena del JSON al buffer del label, truncando si hace
    // falta. Las cadenas del documento mueren con el JsonDocument, asi que
    // apuntarlas en vez de copiarlas dejaria punteros colgados.
    inline void copyLabel(char* dest, const char* source) {
      strncpy(dest, source, ConfigurationOptions::MaxLabelLength - 1);
      dest[ConfigurationOptions::MaxLabelLength - 1] = '\0';
    }

    inline void logMenuLoaded(const char* name, uint8_t loaded, size_t available, uint8_t maxOptions = ConfigurationOptions::MaxOptions) {
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] menu "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, name);
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F(": "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, loaded);
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F(" opciones"));
      if (available > loaded) {
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F(" (el archivo traia "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, available);
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F(", se ignora el resto por el tope de "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, maxOptions);
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

    // Igual que applyInterval pero con el piso de 1000 ms de los timeouts de
    // pantalla. Rechaza entero, no recorta: un timeout de 200 ms es un valor
    // que nadie eligio a proposito, y dejarlo en el default compilado es mas
    // seguro que inventarle un 1000 que el operador no escribio.
    constexpr unsigned long MinScreenTimeout = 1000;

    inline void applyScreenTimeout(JsonVariantConst value, unsigned long& target, const char* name) {
      if (!value.is<unsigned long>()) return;

      unsigned long parsed = value.as<unsigned long>();
      if (parsed < MinScreenTimeout) {
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] timeout de pantalla "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, name);
        DEBUG_PRINT(DEBUG_CONFIGLOADER, F(" por debajo de "));
        DEBUG_PRINT(DEBUG_CONFIGLOADER, MinScreenTimeout);
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" ms: se ignora"));
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
          if (i >= ConfigurationOptions::MaxFieldIntensityOptions) break;
          copyLabel(ConfigurationOptions::optionsFieldIntensity[i].label, option["label"] | "");
          ConfigurationOptions::optionsFieldIntensity[i].intensity = option["value"] | 0.0f;
          i++;
        }
        ConfigurationOptions::countFieldIntensity = i;
        logMenuLoaded("fieldIntensity", i, intensity.size(), ConfigurationOptions::MaxFieldIntensityOptions);
      }

      JsonArrayConst frequency = menus["frequency"];
      if (menuIsUsable(frequency, "frequency")) {
        uint8_t i = 0;
        for (JsonObjectConst option : frequency) {
          if (i >= ConfigurationOptions::MaxFrequencyOptions) break;
          copyLabel(ConfigurationOptions::optionsFrequency[i].label, option["label"] | "");
          ConfigurationOptions::optionsFrequency[i].freq = option["value"] | 0;
          i++;
        }
        ConfigurationOptions::countFrequency = i;
        logMenuLoaded("frequency", i, frequency.size(), ConfigurationOptions::MaxFrequencyOptions);
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
      applyInterval(esp32["reSendConfig"], Intervals::ReSendConfig, "reSendConfig");
      // Timeouts de pantalla, no periodos de tarea: ademas del rechazo de
      // 0/negativos que ya hace applyInterval, tienen piso de 1000 ms (ver
      // docs/config-schema.md seccion 4). Por debajo de eso el splash no
      // llega a verse y el Busy se rinde antes de que el Mega conteste.
      applyScreenTimeout(esp32["splashTimeout"], Intervals::SplashTimeout, "splashTimeout");
      applyScreenTimeout(esp32["busyTimeout"], Intervals::BusyTimeout, "busyTimeout");

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
      if (control["balanceMax"].is<float>()) {
        _controlConfig.hasBalanceMax = true;
        _controlConfig.balanceMax = control["balanceMax"].as<float>();
      }

      JsonArrayConst map = control["map"];
      if (!map.isNull()) {
        for (JsonObjectConst point : map) {
          if (_controlConfig.mapCount >= MaxMapPoints) {
            DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] control.map: se ignoran los puntos que exceden el tope de 3 x 3"));
            break;
          }
          if (!point["intensity"].is<float>() || !point["frequency"].is<float>() ||
              !point["duty"].is<float>() || !point["balance"].is<float>()) {
            DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] control.map: punto sin alguna de intensity/frequency/duty/balance, se ignora"));
            continue;
          }
          ControlMapPoint& entry = _controlConfig.map[_controlConfig.mapCount++];
          entry.intensity = point["intensity"].as<float>();
          entry.frequency = point["frequency"].as<float>();
          entry.duty = point["duty"].as<float>();
          entry.balance = point["balance"].as<float>();
        }
      }

      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] seccion 'control' leida, se reenvia al conectar"));
    }

    inline void loadScenarioValue(JsonObjectConst scenario, const char* key, ScenarioValue& target) {
      if (!scenario[key].is<float>()) return;
      target.has = true;
      target.value = scenario[key].as<float>();
    }

    // Solo tipo: los rangos (no negativos, duty en 0..1) los valida el Mega,
    // que es quien aplica la senal.
    inline void loadScenario(JsonObjectConst scenario, ScenarioConfig& target) {
      if (scenario.isNull()) return;
      target.present = true;
      loadScenarioValue(scenario, "base", target.base);
      loadScenarioValue(scenario, "noise", target.noise);
      loadScenarioValue(scenario, "ramp", target.ramp);
      loadScenarioValue(scenario, "stepAt", target.stepAt);
      loadScenarioValue(scenario, "step", target.step);
      loadScenarioValue(scenario, "oscAmp", target.oscAmp);
      loadScenarioValue(scenario, "oscPeriod", target.oscPeriod);
      loadScenarioValue(scenario, "dropAt", target.dropAt);
      loadScenarioValue(scenario, "setpointDuty", target.setpointDuty);
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

      // Solo un booleano de verdad: un "false" de texto no apaga nada, se
      // ignora y el Mega sigue con el detector encendido.
      if (detector["enabled"].is<bool>()) {
        _detectorConfig.hasEnabled = true;
        _detectorConfig.enabled = detector["enabled"].as<bool>();
        if (!_detectorConfig.enabled) {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG][AVISO] detector.enabled=false: ninguna fuente va a poder cortar el experimento"));
        }
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
        if (source["maxMissedSamples"].is<unsigned long>()) {
          entry.hasMaxMissedSamples = true;
          entry.maxMissedSamples = source["maxMissedSamples"].as<unsigned long>();
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

        loadScenario(source["scenario"], entry.scenario);

        _sourceConfigCount++;
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] 'detector.sources': "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, _sourceConfigCount);
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" fuentes leidas, se reenvian al conectar"));
    }

    // `address` y `channel` son obligatorios: son la identidad fisica de la
    // entrada y sin ellos el Mega no sabe a que slot corresponde la entrada
    // (no hay default razonable que inventar acá). El resto son opcionales,
    // como en el resto del esquema.
    inline void loadCurrentSensors(JsonArrayConst sensors) {
      _currentSensorConfigCount = 0;
      if (sensors.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'currentSensors': el Mega arranca con sus defaults"));
        return;
      }

      for (JsonObjectConst sensor : sensors) {
        if (_currentSensorConfigCount >= MaxCurrentSensors) {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] 'currentSensors' excede el tope de 4: se ignora el resto"));
          break;
        }

        if (!sensor["address"].is<uint8_t>() || !sensor["channel"].is<uint8_t>()) {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] canal de corriente sin 'address'/'channel': se ignora"));
          continue;
        }

        CurrentSensorConfigEntry& entry = _currentSensorConfigs[_currentSensorConfigCount];
        entry = CurrentSensorConfigEntry();
        entry.address = sensor["address"].as<uint8_t>();
        entry.channel = sensor["channel"].as<uint8_t>();
        copyLabel(entry.name, sensor["name"] | "");

        if (sensor["enabled"].is<bool>()) {
          entry.hasEnabled = true;
          entry.enabled = sensor["enabled"].as<bool>();
        }
        if (sensor["ratedCurrent"].is<float>()) {
          entry.hasRatedCurrent = true;
          entry.ratedCurrent = sensor["ratedCurrent"].as<float>();
        }
        if (sensor["ratedVoltage"].is<float>()) {
          entry.hasRatedVoltage = true;
          entry.ratedVoltage = sensor["ratedVoltage"].as<float>();
        }
        if (sensor["calibration"].is<float>()) {
          entry.hasCalibration = true;
          entry.calibration = sensor["calibration"].as<float>();
        }
        if (sensor["sampleRate"].is<uint16_t>()) {
          entry.hasSampleRate = true;
          entry.sampleRate = sensor["sampleRate"].as<uint16_t>();
        }
        if (sensor["integrationTimeMs"].is<uint16_t>()) {
          entry.hasIntegrationTime = true;
          entry.integrationTimeMs = sensor["integrationTimeMs"].as<uint16_t>();
        }

        _currentSensorConfigCount++;
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] 'currentSensors': "));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, _currentSensorConfigCount);
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F(" canales leidos, se reenvian al conectar"));
    }

    // Un campo ausente conserva su default compilado (nombre Y enabled): es
    // el mismo criterio del resto del esquema, y acá importa especialmente
    // porque el nombre es la clave que espera el dashboard remoto.
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
    // Un grupo trae enable, topic y cadencia. `interval` se ignora en los
    // grupos por evento (targets/alerts/result): ahi no hay timer que
    // configurar, y aceptarlo sugeriria que se puede espaciar algo que
    // ocurre una sola vez.
    inline void applyTelemetryGroup(JsonObjectConst groups, const char* key,
                                    Topics::TelemetryGroup& target, bool periodic) {
      JsonObjectConst group = groups[key];
      if (group.isNull()) return;

      if (group["enabled"].is<bool>()) {
        target.enabled = group["enabled"].as<bool>();
      }
      if (group["topic"].is<const char*>()) {
        strncpy(target.topic, group["topic"].as<const char*>(), Topics::MaxTopicLength - 1);
        target.topic[Topics::MaxTopicLength - 1] = '\0';
      }
      if (group["retain"].is<bool>()) {
        target.retain = group["retain"].as<bool>();
      }
      if (periodic) {
        applyInterval(group["interval"], target.interval, key);
      }
    }

    // Copia una cadena del JSON a un buffer propio, truncando si no entra.
    // Devuelve false si la clave no estaba o no era texto, para que el
    // llamador sepa que ese campo se queda con el valor compilado.
    inline bool copyString(JsonVariantConst value, char* dest, size_t size) {
      if (!value.is<const char*>()) return false;
      const char* text = value.as<const char*>();
      if (text == nullptr) return false;
      strncpy(dest, text, size - 1);
      dest[size - 1] = '\0';
      return true;
    }

    // Las redes del archivo REEMPLAZAN a las compiladas, no se suman: si se
    // sumaran, una red vieja que quedo en secrets.h no se podria sacar
    // nunca desde la tarjeta. Una lista vacia se trata como ausente (mismo
    // criterio que menuIsUsable) y deja las compiladas en pie -- quedarse
    // sin ninguna red seria peor que ignorar la seccion.
    inline void loadWifi(JsonArrayConst networks) {
      _wifiNetworkCount = 0;

      if (networks.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'wifi': se usan las redes compiladas"));
        return;
      }

      for (JsonObjectConst network : networks) {
        if (_wifiNetworkCount >= MaxWifiNetworks) {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] mas redes que lugares: se ignoran las sobrantes"));
          break;
        }

        WifiNetworkConfig& entry = _wifiNetworks[_wifiNetworkCount];
        if (!copyString(network["ssid"], entry.ssid, MaxSsidLength) || entry.ssid[0] == '\0') {
          DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] red sin ssid: se ignora"));
          continue;
        }
        // Una red abierta es legitima: password ausente queda en vacio.
        copyString(network["password"], entry.password, MaxWifiPassLength);
        _wifiNetworkCount++;
      }

      if (_wifiNetworkCount == 0) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] 'wifi' vacia o invalida: se usan las redes compiladas"));
        return;
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] redes wifi cargadas: "));
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, _wifiNetworkCount);
    }

    // A diferencia de wifi, el broker se aplica clave por clave: cada una
    // que venga pisa a la compilada y el resto se mantiene. Asi se puede
    // cambiar solo el host sin tener que repetir usuario y contrasena.
    inline void loadBroker(JsonObjectConst broker) {
      if (broker.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'broker': se usan los datos compilados"));
        return;
      }

      copyString(broker["server"], _brokerConfig.server, MaxBrokerHostLength);
      copyString(broker["user"], _brokerConfig.user, MaxBrokerUserLength);
      copyString(broker["password"], _brokerConfig.password, MaxBrokerPassLength);
      copyString(broker["clientId"], _brokerConfig.clientId, MaxBrokerClientIdLength);

      // 0 no es un puerto valido, asi que sirve de "no vino" sin necesidad
      // de un flag aparte.
      if (broker["port"].is<uint16_t>()) {
        uint16_t port = broker["port"].as<uint16_t>();
        if (port > 0) _brokerConfig.port = port;
        else DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] broker.port en 0: se ignora"));
      }

      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] broker: "));
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, _brokerConfig.server[0] != '\0' ? _brokerConfig.server : "(compilado)");
    }

    // El tipo de corrida es una DECLARACION del usuario, no un modo de
    // funcionamiento: no habilita ni deshabilita nada, solo marca las
    // corridas (TEST en `targets` y `result`) para que el monitor remoto no
    // mezcle datos de banco con experimentos. Cada relajacion (detector,
    // fuentes, sensores) se configura por su lado, en su propia seccion.
    //
    // Solo "test" marca prueba. Ausente -- o un valor que no se entiende --
    // queda en experimento, el default de todo el esquema; el generador de
    // tools/ no deja descargar un valor fuera de los dos validos, asi que un
    // error de tipeo solo llega acá editando el archivo a mano, y entonces
    // queda dicho en el log.
    inline void loadRunType(JsonVariantConst value) {
      if (value.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin 'runType': corridas normales"));
        return;
      }
      const char* text = value.as<const char*>();
      if (text != nullptr && strcmp(text, "test") == 0) {
        _testRun = true;
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] runType: test -- las corridas se marcan como PRUEBA"));
      }
      else if (text != nullptr && strcmp(text, "normal") == 0) {
        _testRun = false;
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] runType: normal"));
      }
      else {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] runType invalido (se espera \"normal\" o \"test\"): corridas normales"));
      }
    }

    // Ausente o no booleano => true. Es el valor restrictivo: Iniciar solo
    // con la placa de control conectada, que es quien energiza las bobinas y
    // corta por seguridad. false es para el banco (tarjeta 22): recorrer las
    // pantallas y probar el envio de datos con el ESP32 solo.
    inline void loadRequireMega(JsonVariantConst value) {
      if (value.isNull()) return;
      if (!value.is<bool>()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] requireMega no es booleano: se exige el Mega"));
        return;
      }
      _requireMega = value.as<bool>();
      if (!_requireMega) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG][AVISO] requireMega=false: se puede iniciar sin la placa de control"));
      }
    }

    // Print que acumula un CRC32 (polinomio IEEE, el de zip/Ethernet) de lo
    // que se le escribe: serializeJson() lo alimenta byte a byte, sin armar
    // el texto en memoria.
    class Crc32Print : public Print {
      public:
        uint32_t crc = 0xFFFFFFFFu;
        size_t write(uint8_t byte) override {
          crc ^= byte;
          for (uint8_t i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
          }
          return 1;
        }
        uint32_t value() const { return ~crc; }
    };

    // Va despues de cargar todas las secciones: saca wifi y broker del
    // documento (ya se copiaron a sus buffers), lo serializa en una linea y
    // hashea ESE texto. Mismo firmware, misma serializacion, mismo id -- y el
    // texto publicado tiene exactamente ese CRC, que el monitor verifica.
    inline void computeConfigId(JsonDocument& doc) {
      doc.remove("wifi");
      doc.remove("broker");
      size_t length = serializeJson(doc, _currentText, MaxFileSize);
      if (length >= MaxFileSize - 1) {
        // No deberia pasar (el archivo entero entro en MaxFileSize y esto es
        // un subconjunto minificado), pero un texto truncado tendria un CRC
        // que no describe la configuracion: mejor no publicar nada.
        _currentText[0] = '\0';
      }
      Crc32Print crc;
      serializeJson(doc, crc);
      snprintf(_configId, ConfigIdLength, "%08lx", (unsigned long)crc.value());
    }

    inline void loadTelemetry(JsonObjectConst telemetry) {
      if (telemetry.isNull()) {
        DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin seccion 'telemetry': se conservan los defaults"));
        return;
      }

      JsonObjectConst groups = telemetry["groups"];
      if (!groups.isNull()) {
        applyTelemetryGroup(groups, "measures", Topics::Measures, true);
        applyTelemetryGroup(groups, "coils", Topics::Coils, true);
        applyTelemetryGroup(groups, "status", Topics::Status, true);
        applyTelemetryGroup(groups, "targets", Topics::Targets, false);
        applyTelemetryGroup(groups, "alerts", Topics::Alerts, false);
        applyTelemetryGroup(groups, "result", Topics::Result, false);
      }

      JsonObjectConst fields = telemetry["fields"];
      if (!fields.isNull()) {
        applyTelemetryField(fields, "magneticField", Topics::MagneticField);
        applyTelemetryField(fields, "temperature", Topics::Temperature);
        applyTelemetryField(fields, "health", Topics::Health);
        applyTelemetryField(fields, "progress", Topics::Progress);
        applyTelemetryField(fields, "elapsedTime", Topics::ElapsedTime);
      }

      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] telemetria aplicada"));
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
      _loadStatus = "nosd";
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] sin tarjeta SD: se usan los defaults compilados"));
      return false;
    }

    if (!storage.exists(ConfigPath)) {
      _loadStatus = "nofile";
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
    // Es _currentText (ver su declaracion): se usa como buffer de lectura y
    // queda con el texto publicable al final. Cualquier salida por error lo
    // deja vacio, porque ahi adentro queda el archivo crudo, con credenciales.
    char* buffer = _currentText;
    size_t length = 0;

    if (!storage.readFile(ConfigPath, buffer, MaxFileSize, &length)) {
      _currentText[0] = '\0';
      _loadStatus = "unreadable";
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] no se pudo leer el archivo: se usan los defaults"));
      return false;
    }

    // const char*: ArduinoJson COPIA los strings al documento, asi el buffer
    // queda libre para el texto publicable (computeConfigId).
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (const char*)buffer);
    _currentText[0] = '\0';
    if (error) {
      _loadStatus = "invalid";
      DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] JSON invalido ("));
      DEBUG_PRINT(DEBUG_CONFIGLOADER, error.c_str());
      DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("): se descarta entero, se usan los defaults"));
      return false;
    }

    int version = doc["schemaVersion"] | 0;
    if (version != SupportedSchemaVersion) {
      _loadStatus = "schema";
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

    loadRunType(doc["runType"]);
    // El id se calcula al final, sobre el mismo documento: ver configId().
    loadRequireMega(doc["requireMega"]);
    loadMenus(doc["menus"]);
    loadIntervals(doc["intervals"]);
    loadMegaIntervals(doc["intervals"]);
    loadControl(doc["control"]);
    loadCoils(doc["coils"]);
    loadDetectorSources(doc["detector"]);
    loadCurrentSensors(doc["currentSensors"]);
    loadTelemetry(doc["telemetry"]);
    loadWifi(doc["wifi"]);
    loadBroker(doc["broker"]);

    computeConfigId(doc);
    _loadStatus = "ok";
    DEBUG_PRINT(DEBUG_CONFIGLOADER, F("[CONFIG] configId="));
    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, _configId);
    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] configuracion aplicada"));
    DEBUG_PRINTLN(DEBUG_CONFIGLOADER, F("[CONFIG] ----------------------------------------"));
    return true;
  }


  // Cada clave ausente vale su default de fabrica (el del Mega para lo que
  // es del Mega): detector encendido, TEMP1 vigilada, CEM1 NO vigilada,
  // control activo, Mega exigido.
  inline uint8_t relaxationMask() {
    uint8_t mask = 0;
    if (_detectorConfig.hasEnabled && !_detectorConfig.enabled) mask |= Relaxations::DetectorOff;
    if (_controlConfig.hasEnabled && !_controlConfig.enabled) mask |= Relaxations::ControlOff;
    if (!_requireMega) mask |= Relaxations::MegaNotRequired;
    if (usesSyntheticSensors()) mask |= Relaxations::SyntheticSensor;

    bool cem1Watched = false;
    for (uint8_t i = 0; i < _sourceConfigCount; i++) {
      const SourceConfigEntry& source = _sourceConfigs[i];
      if (strcmp(source.name, "CEM1") == 0) {
        cem1Watched = source.hasEnabled && source.enabled;
      }
      else if (strcmp(source.name, "TEMP1") == 0) {
        if (source.hasEnabled && !source.enabled) mask |= Relaxations::Temp1Unwatched;
        if (source.hasSensor && strcmp(source.sensor, "none") == 0) mask |= Relaxations::Temp1NoSensor;
      }
    }
    if (!cem1Watched) mask |= Relaxations::Cem1Unwatched;
    return mask;
  }


  // Id de un documento cualquiera con la misma regla que configId(): lo usa
  // la configuracion remota (remoteconfig.hpp) para informarle al monitor el
  // id que va a tener la configuracion que acaba de grabar.
  inline void configIdOf(JsonDocument& doc, char* out, size_t size) {
    doc.remove("wifi");
    doc.remove("broker");
    Crc32Print crc;
    serializeJson(doc, crc);
    snprintf(out, size, "%08lx", (unsigned long)crc.value());
  }

  // Agrega al documento las secciones wifi y broker TAL COMO LAS TRAIA el
  // archivo vigente (las que no traia no se inventan: siguen saliendo de
  // secrets.h). La configuracion remota nunca las transporta -- no viajan
  // credenciales por MQTT -- asi que al grabar el archivo nuevo hay que
  // volver a ponerlas, o la placa perderia la red en el proximo reinicio.
  inline void addCredentials(JsonDocument& doc) {
    if (_wifiNetworkCount > 0) {
      JsonArray wifi = doc["wifi"].to<JsonArray>();
      for (uint8_t i = 0; i < _wifiNetworkCount; i++) {
        JsonObject network = wifi.add<JsonObject>();
        network["ssid"] = _wifiNetworks[i].ssid;
        network["password"] = _wifiNetworks[i].password;
      }
    }
    bool hasBroker = _brokerConfig.server[0] != '\0' || _brokerConfig.port != 0
      || _brokerConfig.user[0] != '\0' || _brokerConfig.password[0] != '\0'
      || _brokerConfig.clientId[0] != '\0';
    if (hasBroker) {
      JsonObject broker = doc["broker"].to<JsonObject>();
      if (_brokerConfig.server[0] != '\0')   broker["server"] = _brokerConfig.server;
      if (_brokerConfig.port != 0)            broker["port"] = _brokerConfig.port;
      if (_brokerConfig.user[0] != '\0')     broker["user"] = _brokerConfig.user;
      if (_brokerConfig.password[0] != '\0') broker["password"] = _brokerConfig.password;
      if (_brokerConfig.clientId[0] != '\0') broker["clientId"] = _brokerConfig.clientId;
    }
  }

};
