#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "eventsname.hpp"
#include "systemdata.hpp"
#include "seriallink.hpp"
#include "commands.hpp"
#include "timer.hpp"
#include "tasks.hpp"
#include "intervals.hpp"
#include "topics.hpp"
#include "wifimanager.hpp"
#include "broker.hpp"
#include "display.hpp"
#include "screenmanager.hpp"
#include "configurationoptions.hpp"
#include "configloader.hpp"
#include "tasks.hpp"
#include "intervals.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_MYSYSTEM = true;



class MySystem :
  public CommandListener,
  public TimerListener,
  public WiFiListener,
  public BrokerListener,
  public ScreenManagerListener,
  public IScreenListener,
  public StateListener
{
  private:
    SystemData _data;
    SerialLink& _serial;
    Timer& _timer;
    WiFiManager& _wifiManager;
    BrokerManager& _brokerManager;
    DisplayDriver& _display;
    ScreenManager& _screenManager;

    // Frames de configuracion en vuelo, mandados desde onSerialConnected()
    // y reenviados por Tasks::ReSendConfig hasta que cada uno tenga su ack
    // (docs/config-schema.md seccion 10). Tope: 1 (intervals) + 1 (control)
    // + 4 (coils) + 2 (sources) + 6 (3 reglas x 2 fuentes) + 4 (canales de
    // corriente) = 18.
    static constexpr uint8_t MaxPendingConfigFrames =
      2 + ConfigLoader::MaxCoils + ConfigLoader::MaxDetectorSources * 4
      + ConfigLoader::MaxCurrentSensors;
    struct PendingConfigFrame {
      bool active = false;
      const char* command = nullptr;
      // Desambigua entre varios pendientes del MISMO comando:
      // config_intervals/config_control tienen uno solo cada uno y no la
      // usan, pero puede haber 4 config_coil, 2 config_source y 6
      // config_rule en vuelo a la vez. Para config_coil es el nombre de la
      // bobina; para config_source, el de la fuente; para config_rule,
      // "<fuente>/<regla>". El Mega devuelve las piezas en el ack.
      char key[ConfigurationOptions::MaxLabelLength * 2] = "";
    };
    PendingConfigFrame _pendingConfig[MaxPendingConfigFrames];

  public:
    MySystem(SerialLink& serial, Timer& timer, WiFiManager& wifiManager, BrokerManager& brokerManager, DisplayDriver& display, ScreenManager& screenManager);
    void begin();
    void update();
    void remoteUpdate();

  private:
    void _sendStart();
    void _sendStop();
    void _sendReset();

    void _sendMegaConfig();
    void _resendPendingConfig();
    void _cancelPendingConfig(const char* command, const char* key);
    void _registerPendingConfig(const char* command, const char* key);
    bool _sendConfigFrame(const char* command, JsonDocument& doc, const char* key);
    void _buildConfigIntervalsDoc(JsonDocument& doc);
    void _buildConfigControlDoc(JsonDocument& doc);
    void _buildConfigCoilDoc(JsonDocument& doc, const ConfigLoader::CoilConfig& coil);
    void _buildConfigSourceDoc(JsonDocument& doc, const ConfigLoader::SourceConfigEntry& source);
    void _buildConfigRuleDoc(JsonDocument& doc, const char* sourceName, const char* ruleName, const ConfigLoader::RuleConfig& rule);
    void _buildConfigCurrentDoc(JsonDocument& doc, const ConfigLoader::CurrentSensorConfigEntry& sensor);
    const ConfigLoader::RuleConfig* _findRuleConfig(const ConfigLoader::SourceConfigEntry& source, const char* ruleName);

    bool _publish(const char* topic, const char* payload);
    void _publishTelemetry(JsonDocument& doc);
    void _publishMeasures();
    void _publishStatus();


    void _processState(const char* status, bool forceStatus = false);

  private:
    void onSerialConnected() override;
    void onSerialDisconnected() override;
    void onCommand(const char* command, JsonVariantConst params) override;
    void onTimer(const char* name) override;
    void onWiFiConnected(const char* ssid) override;
    void onWiFiDisconnected() override;
    void onBrokerConnected() override;
    void onBrokerDisconnected() override;
    void onMessageReceived(const char* topic, const char* payload) override;
    void onScreenChanged(ScreenType from, ScreenType to) override;
    void onScreenEvent(ScreenEvent e) override;
    void onStateChanged(const char* oldState, const char* newState) override;

};

inline MySystem::MySystem(SerialLink& serial, Timer& timer, WiFiManager& wifiManager, BrokerManager& brokerManager, DisplayDriver& display, ScreenManager& screenManager) :
  _serial(serial), 
  _timer(timer), 
  _wifiManager(wifiManager), 
  _brokerManager(brokerManager), 
  _display(display), 
  _screenManager(screenManager) 
{
  _serial.setListener(this);
  _timer.setTimerListener(this);
  _wifiManager.setListener(this);
  _brokerManager.setListener(this);
  _data.setStateListener(this);
}

inline void MySystem::begin() {
  _serial.begin();
  _timer.begin();
  _timer.start();
  _display.begin();

  /* ---------------- INSTANCIAS ---------------- */
  static SplashController splash;
  static PrincipalController principal(_data);
  static ConfigurationController config(_data);
  static RunningController running(_data);
  static ResultadoController result(_data);
  static BusyController busy(_data);
  
  /* ---------------- LISTA ---------------- */
  static IScreen* screens[] = {
    &splash,
    &principal,
    &config,
    &running,
    &result,
    &busy
  };
   
  const int totalScreens = sizeof(screens) / sizeof(screens[0]);
  const int initialIndex = 0;

  _screenManager.begin(screens, totalScreens, initialIndex);
  _screenManager.init();  
  _screenManager.setScreenListener(this); 
  _screenManager.setManagerListener(this);
  _timer.addTask(Tasks::UpdateScreens, Intervals::UpdateScreens);
  _processState(StateData::Idle, true);
}

inline void MySystem::update() {
  _serial.update();
  _timer.tick();
}

inline void MySystem::remoteUpdate() {
  _wifiManager.update();
  _brokerManager.loop();
}

inline void MySystem::_sendStart() {

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, );
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "========================================");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Preparando comando START");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "========================================");

    JsonDocument doc;

    // ========================================================
    // CEM
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo target CEM...");

    auto cem = ConfigurationOptions::optionsFieldIntensity[
        _data.configuration.targetFieldIntensityOption
    ].intensity;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] targetFieldIntensityOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.targetFieldIntensityOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] cem = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, cem, 4);

    doc["cem"] = cem;


    // ========================================================
    // FRECUENCIA
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo frecuencia...");

    auto freq = ConfigurationOptions::optionsFrequency[
        _data.configuration.targetFieldFrequencyOption
    ].freq;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] targetFieldFrequencyOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.targetFieldFrequencyOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] freq = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, freq);

    doc["freq"] = freq;


    // ========================================================
    // DURACION
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo duracion...");

    auto dur = ConfigurationOptions::optionsDuration[
        _data.configuration.targetDurationOption
    ].duration;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] targetDurationOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.targetDurationOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] dur = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, dur);


    doc["dur"] = dur;


    // ========================================================
    // TOLERANCIA CEM
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo tolerancia CEM...");

    auto tol = ConfigurationOptions::optionsTolFieldIntensity[
        _data.configuration.fieldIntensityToleranceOption
    ].tol;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] fieldIntensityToleranceOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.fieldIntensityToleranceOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] tol = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, tol);

    doc["tol"] = tol;


    // ========================================================
    // TEMPERATURA NORMAL MIN
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo temperatura normal minima...");

    auto tnmin = ConfigurationOptions::optionsRangeNormalTemperature[
        _data.configuration.normalTemperatureRangeOption
    ].tmin;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] normalTemperatureRangeOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.normalTemperatureRangeOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] tnmin = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, tnmin, 2);

    doc["tnmin"] = tnmin;


    // ========================================================
    // TEMPERATURA NORMAL MAX
    // ========================================================

    auto tnmax = ConfigurationOptions::optionsRangeNormalTemperature[
        _data.configuration.normalTemperatureRangeOption
    ].tmax;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] tnmax = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, tnmax, 2);

    doc["tnmax"] = tnmax;


    // ========================================================
    // TEMPERATURA CRITICA MIN
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo temperatura critica minima...");

    auto tcmin = ConfigurationOptions::optionsRangeCriticalTemperature[
        _data.configuration.criticalTemperatureRangeOption
    ].tmin;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] criticalTemperatureRangeOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.criticalTemperatureRangeOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] tcmin = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, tcmin, 2);

    doc["tcmin"] = tcmin;


    // ========================================================
    // TEMPERATURA CRITICA MAX
    // ========================================================

    auto tcmax = ConfigurationOptions::optionsRangeCriticalTemperature[
        _data.configuration.criticalTemperatureRangeOption
    ].tmax;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] tcmax = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, tcmax, 2);

    doc["tcmax"] = tcmax;


    // ========================================================
    // JSON FINAL
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, );
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] JSON generado:");

    serializeJsonPretty(doc, Serial);

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, );
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, );

    // ========================================================
    // ENVIO
    // ========================================================

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Enviando comando START...");

    _serial.sendCommand(Commands::Start, doc);

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Comando START enviado correctamente");

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "========================================");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Fin _sendStart()");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "========================================");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, );
}

inline void MySystem::_sendStop() {
  JsonDocument doc;
  _serial.sendCommand(Commands::Stop, doc);
}

inline void MySystem::_sendReset() {
  JsonDocument doc;
  _serial.sendCommand(Commands::Reset, doc);
}

inline void MySystem::_buildConfigIntervalsDoc(JsonDocument& doc) {
  const auto& mega = ConfigLoader::megaIntervalsConfig();
  if (mega.ping.has) doc["ping"] = mega.ping.value;
  if (mega.sendState.has) doc["sendState"] = mega.sendState.value;
  if (mega.measureTemperature.has) doc["measureTemperature"] = mega.measureTemperature.value;
  if (mega.measureCurrent.has) doc["measureCurrent"] = mega.measureCurrent.value;
  if (mega.measureMagneticField.has) doc["measureMagneticField"] = mega.measureMagneticField.value;
  if (mega.updateProgress.has) doc["updateProgress"] = mega.updateProgress.value;
  if (mega.sendFlags.has) doc["sendFlags"] = mega.sendFlags.value;
  if (mega.sendResult.has) doc["sendResult"] = mega.sendResult.value;
  if (mega.settlingTime.has) doc["settlingTime"] = mega.settlingTime.value;
}

inline void MySystem::_buildConfigControlDoc(JsonDocument& doc) {
  const auto& control = ConfigLoader::controlConfig();
  if (control.hasKp) doc["kp"] = control.kp;
  if (control.hasMaxStep) doc["maxStep"] = control.maxStep;
  if (control.hasDeadBand) doc["deadBand"] = control.deadBand;
}

inline void MySystem::_buildConfigCoilDoc(JsonDocument& doc, const ConfigLoader::CoilConfig& coil) {
  doc["name"] = coil.name;
  if (coil.hasEnabled) doc["enabled"] = coil.enabled;
  if (coil.hasCalibrationFactor) doc["calibrationFactor"] = coil.calibrationFactor;
}

// Solo la cabecera de la fuente: las 3 reglas van en frames config_rule
// aparte porque una fuente entera NO entra en un frame (ver
// test_serialframes en el Mega, que fija ese limite).
inline void MySystem::_buildConfigSourceDoc(JsonDocument& doc, const ConfigLoader::SourceConfigEntry& source) {
  doc["name"] = source.name;
  if (source.hasEnabled) doc["enabled"] = source.enabled;
  if (source.hasSensor) doc["sensor"] = source.sensor;
  if (source.hasBufferSize) doc["bufferSize"] = source.bufferSize;
  if (source.hasCriticalMultiplier) doc["criticalMultiplier"] = source.criticalMultiplier;
}

inline void MySystem::_buildConfigRuleDoc(JsonDocument& doc, const char* sourceName, const char* ruleName, const ConfigLoader::RuleConfig& rule) {
  doc["source"] = sourceName;
  doc["rule"] = ruleName;
  doc["threshold"] = rule.threshold;
  doc["cooldown"] = rule.cooldown;
  doc["maxEvents"] = rule.maxEvents;
}

// address y channel van siempre: son la identidad del slot del otro lado, no
// un parametro configurable. El resto solo si el archivo lo trajo.
inline void MySystem::_buildConfigCurrentDoc(JsonDocument& doc, const ConfigLoader::CurrentSensorConfigEntry& sensor) {
  doc["address"] = sensor.address;
  doc["channel"] = sensor.channel;
  if (sensor.name[0] != '\0') doc["name"] = sensor.name;
  if (sensor.hasEnabled) doc["enabled"] = sensor.enabled;
  if (sensor.hasRatedCurrent) doc["ratedCurrent"] = sensor.ratedCurrent;
  if (sensor.hasRatedVoltage) doc["ratedVoltage"] = sensor.ratedVoltage;
  if (sensor.hasCalibration) doc["calibration"] = sensor.calibration;
  if (sensor.hasSampleRate) doc["sampleRate"] = sensor.sampleRate;
  if (sensor.hasIntegrationTime) doc["integrationTimeMs"] = sensor.integrationTimeMs;
}

inline const ConfigLoader::RuleConfig* MySystem::_findRuleConfig(const ConfigLoader::SourceConfigEntry& source, const char* ruleName) {
  if (strcmp(ruleName, "critical") == 0) return &source.critical;
  if (strcmp(ruleName, "streak") == 0) return &source.streak;
  if (strcmp(ruleName, "frequency") == 0) return &source.frequency;
  return nullptr;
}

inline void MySystem::_registerPendingConfig(const char* command, const char* key) {
  for (uint8_t i = 0; i < MaxPendingConfigFrames; i++) {
    if (_pendingConfig[i].active) continue;
    _pendingConfig[i].active = true;
    _pendingConfig[i].command = command;
    snprintf(_pendingConfig[i].key, sizeof(_pendingConfig[i].key), "%s", key == nullptr ? "" : key);
    return;
  }
}

// Encola un frame respetando la cola del SerialLink: sendCommand() devuelve
// false cuando esta llena (9 mensajes utiles, drena uno cada 50ms) y hasta
// ahora ese false se ignoraba. Con 14 frames de configuracion eso ya no es
// teorico: los que no entran se dejan pendientes y salen en el siguiente
// tick de Tasks::ReSendConfig, que es exactamente para lo que existe.
inline bool MySystem::_sendConfigFrame(const char* command, JsonDocument& doc, const char* key) {
  if (!_serial.sendCommand(command, doc)) {
    DEBUG_PRINT(DEBUG_MYSYSTEM, F("[CONFIG] cola llena, queda pendiente: "));
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, command);
    return false;
  }
  return true;
}

// Dispara al conectar/reconectar (onSerialConnected): registra los hasta 14
// frames de configuracion del Mega como pendientes, manda los que entren en
// la cola, y arranca Tasks::ReSendConfig para el resto y para los que no
// reciban ack. Claves ausentes en ConfigLoader (has=false) simplemente no se
// incluyen en el frame -- el Mega se queda con su propio default.
inline void MySystem::_sendMegaConfig() {
  for (uint8_t i = 0; i < MaxPendingConfigFrames; i++) {
    _pendingConfig[i] = PendingConfigFrame();
  }

  _registerPendingConfig(Commands::ConfigIntervals, nullptr);
  _registerPendingConfig(Commands::ConfigControl, nullptr);

  uint8_t coilCount = ConfigLoader::coilConfigCount();
  const ConfigLoader::CoilConfig* coils = ConfigLoader::coilConfigs();
  for (uint8_t i = 0; i < coilCount; i++) {
    _registerPendingConfig(Commands::ConfigCoil, coils[i].name);
  }

  static const char* const ruleNames[] = { "critical", "streak", "frequency" };
  uint8_t sourceCount = ConfigLoader::sourceConfigCount();
  const ConfigLoader::SourceConfigEntry* sources = ConfigLoader::sourceConfigs();
  for (uint8_t i = 0; i < sourceCount; i++) {
    _registerPendingConfig(Commands::ConfigSource, sources[i].name);

    for (uint8_t r = 0; r < 3; r++) {
      const ConfigLoader::RuleConfig* rule = _findRuleConfig(sources[i], ruleNames[r]);
      // Una regla que el archivo no trajo no se manda: el Mega se queda con
      // su default compilado, que es el contrato de la seccion 1.1.
      if (rule == nullptr || !rule->has) continue;

      char key[ConfigurationOptions::MaxLabelLength * 2];
      snprintf(key, sizeof(key), "%s/%s", sources[i].name, ruleNames[r]);
      _registerPendingConfig(Commands::ConfigRule, key);
    }
  }

  // La clave de un canal de corriente es "<address>/<channel>": el Mega
  // matchea por esas dos, no por nombre, y las devuelve en el ack.
  uint8_t currentCount = ConfigLoader::currentSensorConfigCount();
  const ConfigLoader::CurrentSensorConfigEntry* currents = ConfigLoader::currentSensorConfigs();
  for (uint8_t i = 0; i < currentCount; i++) {
    char key[ConfigurationOptions::MaxLabelLength];
    snprintf(key, sizeof(key), "%u/%u", currents[i].address, currents[i].channel);
    _registerPendingConfig(Commands::ConfigCurrent, key);
  }

  _timer.addTask(Tasks::ReSendConfig, Intervals::ReSendConfig);
  _resendPendingConfig();
}

// Manda cada frame que sigue activo, reconstruyendolo desde ConfigLoader (no
// cachea el JSON armado). Corta apenas la cola del SerialLink se llena: lo
// que quede sale en el proximo tick. Un pendiente cuya fuente/bobina ya no
// este en ConfigLoader (no deberia pasar, la config no cambia en runtime)
// simplemente no se manda.
inline void MySystem::_resendPendingConfig() {
  uint8_t coilCount = ConfigLoader::coilConfigCount();
  const ConfigLoader::CoilConfig* coils = ConfigLoader::coilConfigs();
  uint8_t sourceCount = ConfigLoader::sourceConfigCount();
  const ConfigLoader::SourceConfigEntry* sources = ConfigLoader::sourceConfigs();

  for (uint8_t i = 0; i < MaxPendingConfigFrames; i++) {
    if (!_pendingConfig[i].active) continue;

    const char* command = _pendingConfig[i].command;
    const char* key = _pendingConfig[i].key;
    JsonDocument doc;

    if (strcmp(command, Commands::ConfigIntervals) == 0) {
      _buildConfigIntervalsDoc(doc);
    }
    else if (strcmp(command, Commands::ConfigControl) == 0) {
      _buildConfigControlDoc(doc);
    }
    else if (strcmp(command, Commands::ConfigCoil) == 0) {
      const ConfigLoader::CoilConfig* coil = nullptr;
      for (uint8_t c = 0; c < coilCount; c++) {
        if (strcmp(coils[c].name, key) == 0) coil = &coils[c];
      }
      if (coil == nullptr) continue;
      _buildConfigCoilDoc(doc, *coil);
    }
    else if (strcmp(command, Commands::ConfigSource) == 0) {
      const ConfigLoader::SourceConfigEntry* source = nullptr;
      for (uint8_t s = 0; s < sourceCount; s++) {
        if (strcmp(sources[s].name, key) == 0) source = &sources[s];
      }
      if (source == nullptr) continue;
      _buildConfigSourceDoc(doc, *source);
    }
    else if (strcmp(command, Commands::ConfigRule) == 0) {
      // key es "<fuente>/<regla>"
      const char* separator = strchr(key, '/');
      if (separator == nullptr) continue;

      const ConfigLoader::SourceConfigEntry* source = nullptr;
      for (uint8_t s = 0; s < sourceCount; s++) {
        if (strncmp(sources[s].name, key, separator - key) == 0) source = &sources[s];
      }
      if (source == nullptr) continue;

      const ConfigLoader::RuleConfig* rule = _findRuleConfig(*source, separator + 1);
      if (rule == nullptr) continue;
      _buildConfigRuleDoc(doc, source->name, separator + 1, *rule);
    }
    else if (strcmp(command, Commands::ConfigCurrent) == 0) {
      // key es "<address>/<channel>"
      uint8_t currentCount = ConfigLoader::currentSensorConfigCount();
      const ConfigLoader::CurrentSensorConfigEntry* currents = ConfigLoader::currentSensorConfigs();
      const ConfigLoader::CurrentSensorConfigEntry* sensor = nullptr;

      for (uint8_t c = 0; c < currentCount; c++) {
        char candidate[ConfigurationOptions::MaxLabelLength];
        snprintf(candidate, sizeof(candidate), "%u/%u", currents[c].address, currents[c].channel);
        if (strcmp(candidate, key) == 0) sensor = &currents[c];
      }
      if (sensor == nullptr) continue;
      _buildConfigCurrentDoc(doc, *sensor);
    }
    else {
      continue;
    }

    if (!_sendConfigFrame(command, doc, key)) return;
  }
}

// Marca inactivo el pendiente que matchea comando + clave. Remueve
// Tasks::ReSendConfig apenas no queda ninguno activo.
inline void MySystem::_cancelPendingConfig(const char* command, const char* key) {
  bool anyActive = false;

  for (uint8_t i = 0; i < MaxPendingConfigFrames; i++) {
    if (!_pendingConfig[i].active) continue;

    bool matches = _pendingConfig[i].command != nullptr && strcmp(_pendingConfig[i].command, command) == 0;
    if (matches && key != nullptr) {
      matches = strcmp(_pendingConfig[i].key, key) == 0;
    }

    if (matches) {
      _pendingConfig[i].active = false;
    } else {
      anyActive = true;
    }
  }

  if (!anyActive) {
    _timer.removeTask(Tasks::ReSendConfig);
  }
}

inline bool MySystem::_publish(const char* topic, const char* payload) {
  return _brokerManager.publish(topic, payload);
}

// Los nombres de campo y el topic salen de Topics (topics.hpp), que trae los
// defaults compilados (CEM1/TEMP1/BOB1/ESTADO/PROGRESS/ELAPSED_TIME) y los
// pisa la seccion `telemetry` del archivo de la SD. Esos defaults son los que
// espera el Decoder ya configurado del lado de Datacake: renombrar uno obliga
// a actualizar tambien ese Decoder, o el valor deja de llegar sin error
// visible. BOB1 es el nombre historico del Decoder para la bobina; se reusa
// para la corriente medida (measureCurrent, sensor SCT013) en vez de agregar
// un campo nuevo.
inline void MySystem::_publishMeasures() {
  JsonDocument doc;
  if (Topics::MagneticField.enabled) doc[Topics::MagneticField.name] = _data.measures.measureMagneticField;
  if (Topics::Temperature.enabled)   doc[Topics::Temperature.name]   = _data.measures.measureTemperature;
  if (Topics::Current.enabled)       doc[Topics::Current.name]       = _data.measures.measureCurrent;

  _publishTelemetry(doc);
}

// Serializa y publica, salvo que no haya nada que mandar o que el payload no
// entre en el buffer.
//
// El chequeo de truncado NO es defensivo de mas: serializeJson recorta en
// silencio si no entra y devuelve lo que escribio, y hasta ahora ese retorno
// se ignoraba. Con los nombres de campo configurables desde la SD, un par de
// nombres largos alcanzan para pasarse de 128 y publicar un JSON cortado, que
// el Decoder de Datacake no puede parsear: se perderia la tanda entera sin un
// solo error visible. Preferimos no publicar y dejarlo dicho en el log.
inline void MySystem::_publishTelemetry(JsonDocument& doc) {
  if (doc.size() == 0) return;

  char payload[128];
  size_t length = serializeJson(doc, payload, sizeof(payload));

  if (length >= sizeof(payload) - 1) {
    DEBUG_PRINT(DEBUG_MYSYSTEM, F("[TELEMETRIA] payload no entra en "));
    DEBUG_PRINT(DEBUG_MYSYSTEM, (int)sizeof(payload));
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, F(" bytes -- no se publica (acortar los nombres de campo)"));
    return;
  }

  _publish(Topics::Telemetry, payload);
}

inline void MySystem::_publishStatus() {
  JsonDocument doc;
  if (Topics::Health.enabled)      doc[Topics::Health.name]      = _data.progress.health;
  if (Topics::Progress.enabled)    doc[Topics::Progress.name]    = (int)_data.progressPercent();
  if (Topics::ElapsedTime.enabled) doc[Topics::ElapsedTime.name] = _data.elapsedTime();

  _publishTelemetry(doc);
}




inline void MySystem::_processState(const char* status, bool forceStatus) {
  IScreen* screen = _screenManager.getCurrent();
  ScreenType current = screen->getType();
  if (strcmp(status, StateData::Idle) == 0) {
    if (forceStatus) {
      _data.setInitialized(false);
    }
    _data.setState(status);
    _screenManager.show(ScreenType::SPLASH);
  }
  else if(strcmp(status, StateData::Ready) == 0) {
    if(!_data.isInitialized()) return;
    // RESULT: el Mega ya volvio a Ready para cuando este "ready" llega (ver
    // Engine::_finish -- _stop()+_reset() son sincronicos), pero la
    // pantalla de Resultado se abandona solo con el boton Volver, no con un
    // ready espureo que llegue por el timer periodico de SendState.
    if(current == ScreenType::PRINCIPAL || current == ScreenType::CONFIG || current == ScreenType::RESULT) return;
    if(current == ScreenType::BUSY) {
      // Starting: un "ready" que llegue mientras se espera confirmacion de
      // un start es viejo/espureo -- se ignora salvo forceStatus. Stopping
      // NO tiene la misma guarda: el "ready" es justamente la confirmacion
      // real que se esta esperando, bloquearla dejaba "Detener" colgado en
      // la pantalla Busy para siempre (o cayendo al timeout, que hoy vuelve
      // a Running en vez de a Principal).
      if(_data.getState() == StateData::Starting) {
        if(!forceStatus) return;
      }
    }
    _data.setState(status);
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(strcmp(status, StateData::Running) == 0) {
    if(current == ScreenType::RUNNING) return;
    // Misma duracion (ms) que _sendStart() ya mando como "dur" -- se
    // calcula el progreso localmente, sin esperar nada nuevo del Mega.
    _data.progress.startTime = millis();
    _data.progress.duration = ConfigurationOptions::optionsDuration[
        _data.configuration.targetDurationOption
    ].duration;
    _data.alerts = AlertsData();
    snprintf(_data.progress.health, sizeof(_data.progress.health), "%s", HealthData::Normal);
    _data.setState(status);
    _screenManager.show(ScreenType::RUNNING);
  }
  else if(strcmp(status, StateData::Starting) == 0) {
    _data.busy.messageProcess = "Iniciando...";
    if(current == ScreenType::BUSY) return;
    _data.setState(status);
    _screenManager.show(ScreenType::BUSY);
  }
  else if(strcmp(status, StateData::Stopping) == 0) {
    _data.busy.messageProcess = "Deteniendo...";
    if(current == ScreenType::BUSY) return;
    _data.setState(status);
    _screenManager.show(ScreenType::BUSY);
  }
}

inline void MySystem::onCommand(const char* command, JsonVariantConst params) {
  DEBUG_PRINT(DEBUG_MYSYSTEM, "Command: ");
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, command);
  if(strcmp(command, Commands::Ping) == 0) {
    if (!params["value"].is<long>()) return;
    long value = params["value"].as<long>();
    DEBUG_PRINT(DEBUG_MYSYSTEM, "value: ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, value);
    _serial.sendPong(value);
  }
  else if(strcmp(command, Commands::Pong) == 0) {
    if (!params["value"].is<long>()) return;
    long value = params["value"].as<long>();
    DEBUG_PRINT(DEBUG_MYSYSTEM, "value: ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, value);
    _serial.handlePingResponse(value);
  }
  else if(strcmp(command, Commands::StateData) == 0) {
    if (params.containsKey("status") && params["status"].is<const char*>()) {
      const char* status = params["status"].as<const char*>();
      DEBUG_PRINT(DEBUG_MYSYSTEM, "status: ");
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, status);
      _processState(status);
    }
  }
  else if(strcmp(command, Commands::Ack) == 0) {
    if (params.containsKey("command") && params["command"].is<const char*>()) {
      const char* ack = params["command"].as<const char*>();
      if(strcmp(ack, Commands::Reset) == 0) {
        _timer.removeTask(Tasks::ReSendReset);
      }
      else if(strcmp(ack, Commands::Start) == 0) {
        _timer.removeTask(Tasks::ReSendStart);
      }
      else if(strcmp(ack, Commands::Stop) == 0) {
        _timer.removeTask(Tasks::ReSendStop);
      }
      else if(strcmp(ack, Commands::ConfigIntervals) == 0 || strcmp(ack, Commands::ConfigControl) == 0) {
        _cancelPendingConfig(ack, nullptr);
      }
      else if(strcmp(ack, Commands::ConfigCoil) == 0 || strcmp(ack, Commands::ConfigSource) == 0) {
        _cancelPendingConfig(ack, params["name"] | "");
      }
      else if(strcmp(ack, Commands::ConfigCurrent) == 0) {
        char key[ConfigurationOptions::MaxLabelLength];
        snprintf(key, sizeof(key), "%u/%u",
                 params["address"] | 0, params["channel"] | 0);
        _cancelPendingConfig(ack, key);
      }
      else if(strcmp(ack, Commands::ConfigRule) == 0) {
        // El Mega devuelve fuente y regla por separado; acá se rearma la
        // misma clave "<fuente>/<regla>" con la que se registro el pendiente.
        char key[ConfigurationOptions::MaxLabelLength * 2];
        snprintf(key, sizeof(key), "%s/%s",
                 params["source"] | "", params["rule"] | "");
        _cancelPendingConfig(ack, key);
      }
    }
  }
  //else if(strcmp(command, Commands::ProgressData) == 0) {}
  //else if(strcmp(command, Commands::FlagsData) == 0) {}
  else if(strcmp(command, Commands::ResultData) == 0) {
    const char* reason = params["reason"] | "";
    const char* description = params["description"] | "";

    snprintf(_data.result.reason, sizeof(_data.result.reason), "%s", reason);
    snprintf(_data.result.description, sizeof(_data.result.description), "%s", description);

    // Snapshot de lo que el ESP32 ya venia trackeando en vivo, congelado en
    // el instante del corte (ver comentario de ResultData en systemdata.hpp).
    snprintf(_data.result.health, sizeof(_data.result.health), "%s", _data.progress.health);
    _data.result.progressPercent = _data.progressPercent();
    snprintf(_data.result.elapsed, sizeof(_data.result.elapsed), "%s", _data.elapsedTime());

    DEBUG_PRINT(DEBUG_MYSYSTEM, F("[RESULT] reason="));
    DEBUG_PRINT(DEBUG_MYSYSTEM, reason);
    DEBUG_PRINT(DEBUG_MYSYSTEM, F(" description="));
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, description);

    _screenManager.show(ScreenType::RESULT);
  }
  else if(strcmp(command, Commands::OneFlagsData) == 0) {
    const char* source = params["source"] | "?";
    const char* type   = params["type"] | "?";
    int count = params["count"] | 0;
    int limit = params["limit"] | 0;

    DEBUG_PRINT(DEBUG_MYSYSTEM, F("[FLAG] "));
    DEBUG_PRINT(DEBUG_MYSYSTEM, source);
    DEBUG_PRINT(DEBUG_MYSYSTEM, F(" tipo="));
    DEBUG_PRINT(DEBUG_MYSYSTEM, type);
    DEBUG_PRINT(DEBUG_MYSYSTEM, F(" count="));
    DEBUG_PRINT(DEBUG_MYSYSTEM, count);
    DEBUG_PRINT(DEBUG_MYSYSTEM, F("/"));
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, limit);

    _data.pushAlert(type, source, count, limit);

    // La salud se recalcula al vuelo con cada flag que llega, no por
    // polling -- ver tambien el reset a Normal al entrar a Running.
    const char* health = _data.evaluateHealth();
    snprintf(_data.progress.health, sizeof(_data.progress.health), "%s", health);
  }
  
  else if(strcmp(command, Commands::TempData) == 0) {
    // La clave es el nombre real del sensor, tal como lo registra el Mega
    // (ver ThermometerManager::publishMeasures / "TEMP1" en el .ino), no un
    // nombre de campo genérico.
    if (params.containsKey("TEMP1") && params["TEMP1"].is<float>()) {
      float temp1 = params["TEMP1"].as<float>();
      _data.measures.measureTemperature = temp1;
      _data.measures.latestTemperatureUpdate = millis();
    }
  }
  else if(strcmp(command, Commands::CemData) == 0) {
    if (params.containsKey("CEM1") && params["CEM1"].is<float>()) {
      float cem1 = params["CEM1"].as<float>();
      _data.measures.measureMagneticField = cem1;
      _data.measures.latestMagneticFieldUpdate = millis();
    }
  }
  else if(strcmp(command, Commands::CurrentData) == 0) {
    if (params.containsKey("SCT013-1") && params["SCT013-1"].is<float>()) {
      float curr1 = params["SCT013-1"].as<float>();
      _data.measures.measureCurrent = curr1;
      _data.measures.latestCurrentUpdate = millis();
    }
  }
  
}

inline void MySystem::onTimer(const char* name) {

  if(strcmp(name, Tasks::UpdateScreens) == 0) {
    _screenManager.update();
  }
  else if(strcmp(name, Tasks::ReSendStart) == 0) {
    _sendStart();
  }
  else if(strcmp(name, Tasks::ReSendStop) == 0) {
    _sendStop();
  }
  else if(strcmp(name, Tasks::ReSendReset) == 0) {
    _sendReset();
  }
  else if(strcmp(name, Tasks::PublishMeasures) == 0) {
    _publishMeasures();
  }
  else if(strcmp(name, Tasks::PublishStatus) == 0) {
    _publishStatus();
  }
  else if(strcmp(name, Tasks::ReSendConfig) == 0) {
    _resendPendingConfig();
  }
}

inline void MySystem::onSerialConnected() {
  _data.communication.serialOk = true;
  // Dispara en cada conexion/reconexion (docs/config-schema.md seccion
  // 10.2): el Mega queda configurado antes de que pueda existir un
  // experimento, y un reset del Mega lo reconfigura solo al reconectar.
  _sendMegaConfig();
}

inline void MySystem::onSerialDisconnected() {
  _data.communication.serialOk = false;
}

inline void MySystem::onWiFiConnected(const char* ssid) {
  _data.communication.wifiOk = true;
}

inline void MySystem::onWiFiDisconnected() {
  _data.communication.wifiOk = false;
}

inline void MySystem::onBrokerConnected() {
  _data.communication.brokerOk = true;
}

inline void MySystem::onBrokerDisconnected() {
  _data.communication.brokerOk = false;
}

inline void MySystem::onMessageReceived(const char* topic, const char* payload) {}

inline void MySystem::onScreenChanged(ScreenType from, ScreenType to) {}

inline void MySystem::onScreenEvent(ScreenEvent e) {
  DEBUG_PRINT(DEBUG_MYSYSTEM, "Screen event: ");
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, e.name);

  if(e.type == ScreenType::SPLASH && e.name == EventName::Timeout) {
    _data.setInitialized(true);
  }
  else if(e.type == ScreenType::PRINCIPAL && e.name == EventName::GoToConfig) {
    _screenManager.show(ScreenType::CONFIG);
  }
  else if(e.type == ScreenType::PRINCIPAL && e.name == EventName::Start) {
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "Procesando start!");
    _sendStart();
    // addTask acá y de nuevo en onStateChanged() cuando newState pasa a
    // Starting (esta misma llamada a _processState dispara ese callback) --
    // Timer::addTask no duplica tareas con el mismo nombre (ver
    // Timer::includeTask), asi que el segundo intento es un no-op seguro,
    // no un bug.
    _timer.addTask(Tasks::ReSendStart, Intervals::ReSendStart);
    _processState(StateData::Starting);
  }
  else if(e.type == ScreenType::RUNNING && e.name == EventName::Stop) {
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "Procesando stop!");
    _sendStop();
    // Mismo patron redundante-pero-seguro que el caso Start de arriba.
    _timer.addTask(Tasks::ReSendStop, Intervals::ReSendStop);
    _processState(StateData::Stopping);
  }
  else if(e.type == ScreenType::CONFIG && e.name == EventName::Back) {
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(e.type == ScreenType::CONFIG && e.name == EventName::Save) {
    // No hay nada que persistir aca -- ConfigurationController::onSave()
    // (screencontroller.hpp) ya escribio _data.configuration ANTES de
    // emitir este evento; este handler solo navega de vuelta.
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(e.type == ScreenType::RESULT && e.name == EventName::Back) {
    _data.result = ResultData();
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(e.type == ScreenType::BUSY && e.name == EventName::Timeout) {
    if(_data.getState() == StateData::Starting) {
      _processState(StateData::Ready);
    }
    else if(_data.getState() == StateData::Stopping) {
      _processState(StateData::Ready);
    }
  }
}

inline void MySystem::onStateChanged(const char* oldState, const char* newState) {
  DEBUG_PRINT(DEBUG_MYSYSTEM, "oldstate: ");
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, oldState);

  DEBUG_PRINT(DEBUG_MYSYSTEM, "newstate: ");
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, newState);

  if(strcmp(oldState, StateData::Starting) == 0) {
    _timer.removeTask(Tasks::ReSendStart);
  }
  else if(strcmp(oldState, StateData::Stopping) == 0) {
    _timer.removeTask(Tasks::ReSendStop);
  }
  else if(strcmp(oldState, StateData::Finished) == 0) {
    _timer.removeTask(Tasks::ReSendReset);
  }
  else if(strcmp(oldState, StateData::Running) == 0) {
    _timer.removeTask(Tasks::UpdateProgress);
    _timer.removeTask(Tasks::PublishMeasures);
    _timer.removeTask(Tasks::PublishStatus);
  }

  if(strcmp(newState, StateData::Ready) == 0) {
    _timer.addTask(Tasks::ReSendReset, Intervals::ReSendReset);
  }
  else if(strcmp(newState, StateData::Running) == 0) {
    _timer.addTask(Tasks::UpdateProgress, Intervals::UpdateProgress);
    // Telemetria a Datacake (via EMQX) -- solo mientras corre el
    // experimento, no tiene sentido gastar cuota del plan en idle.
    _timer.addTask(Tasks::PublishMeasures, Intervals::PublishMeasures);
    _timer.addTask(Tasks::PublishStatus, Intervals::PublishStatus);
  }
  else if(strcmp(newState, StateData::Starting) == 0) {
    _timer.addTask(Tasks::ReSendStart, Intervals::ReSendStart);
  }
  else if(strcmp(newState, StateData::Stopping) == 0) {
    _timer.addTask(Tasks::ReSendStop, Intervals::ReSendStop);
  }
}
