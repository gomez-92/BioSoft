#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <time.h>

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
#include "selectionstore.hpp"
#include "remoteconfig.hpp"
#include "remoteping.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_MYSYSTEM = true;

// El antiguo BENCH_SIN_MEGA (constante de compilacion, que habia quedado en
// true en el firmware de produccion) es ahora `requireMega` de la tarjeta
// SD, con default true. Ver MySystem::_benchWithoutMega().



class MySystem :
  public CommandListener,
  public TimerListener,
  public WiFiListener,
  public BrokerListener,
  public ScreenManagerListener,
  public IScreenListener,
  public StateListener,
  public RemoteConfig::Output
{
  private:
    SystemData _data;

    // Configuracion remota (tarjeta 24, remoteconfig.hpp). Los bloques que
    // llegan por MQTT entran por onMessageReceived(), que corre en el nucleo
    // de comunicaciones; la SD la usa el loop principal. Por eso se encolan
    // y se procesan en update(): nunca se toca la SD desde los dos nucleos.
    struct ConfigInboxMessage {
      char payload[BrokerBufferSize];
    };
    QueueHandle_t _configInbox = nullptr;
    // Pings del monitor (remoteping.hpp): mismo motivo que _configInbox, el
    // pong se arma con el estado de la app, que vive en el loop principal.
    QueueHandle_t _pingInbox = nullptr;
    // Lo levanta onBrokerConnected() (otro nucleo) y lo atiende update(): el
    // Timer no es seguro entre nucleos, asi que la tarea se agrega desde aca.
    volatile bool _publishConfigRequested = false;
    SerialLink& _serial;
    Timer& _timer;
    WiFiManager& _wifiManager;
    BrokerManager& _brokerManager;
    DisplayDriver& _display;
    ScreenManager& _screenManager;
    SdStorage& _sdStorage;
    RemoteConfig::CurrentPublisher _configPublisher;
    RemoteConfig::Receiver _configReceiver;

    // Frames de configuracion en vuelo, mandados desde onSerialConnected()
    // y reenviados por Tasks::ReSendConfig hasta que cada uno tenga su ack
    // (docs/config-schema.md seccion 10). Tope: 1 (intervals) + 1 (control)
    // + 1 (detector) + 4 (coils) + 2 (sources) + 6 (3 reglas x 2 fuentes)
    // + 2 (escenarios) + 4 (canales de corriente) + 9 (puntos del mapa) = 30.
    static constexpr uint8_t MaxPendingConfigFrames =
      3 + ConfigLoader::MaxCoils + ConfigLoader::MaxDetectorSources * 5
      + ConfigLoader::MaxCurrentSensors + ConfigLoader::MaxMapPoints;
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
    MySystem(SerialLink& serial, Timer& timer, WiFiManager& wifiManager, BrokerManager& brokerManager, DisplayDriver& display, ScreenManager& screenManager, SdStorage& sdStorage);
    void begin();
    void update();
    void remoteUpdate();

  private:
    void _sendStart();
    void _sendStop();
    void _applyResult(const char* reason, const char* description);
    void _applyResult(const char* reason, const char* description, JsonVariantConst params);
    void _sendReset();

    void _sendMegaConfig();
    void _updateRemoteConfig();
    void _answerPings();
    // Banco sin Mega2560: `requireMega: false` en la tarjeta Y el serial sin
    // conectar. Al vencer el splash se pasa a Principal igual (forzando
    // Ready) en vez de esperar el primer state_data; Iniciar pasa directo a
    // Running (sin Busy ni reintentos de `start`) y Detener fabrica un
    // result_data local ("stopped") que lleva a Resultado. Nada de esto
    // manda comandos al serial. Con el Mega conectado no cambia nada: el
    // flujo normal manda aunque la tarjeta diga false.
    bool _benchWithoutMega();
    void _resendPendingConfig();
    void _cancelPendingConfig(const char* command, const char* key);
    void _registerPendingConfig(const char* command, const char* key);
    bool _sendConfigFrame(const char* command, JsonDocument& doc, const char* key);
    void _buildConfigIntervalsDoc(JsonDocument& doc);
    void _buildConfigControlDoc(JsonDocument& doc);
    void _buildConfigMapDoc(JsonDocument& doc, uint8_t index);
    void _buildConfigCoilDoc(JsonDocument& doc, const ConfigLoader::CoilConfig& coil);
    void _buildConfigSourceDoc(JsonDocument& doc, const ConfigLoader::SourceConfigEntry& source);
    void _buildConfigScenarioDoc(JsonDocument& doc, const ConfigLoader::SourceConfigEntry& source);
    void _buildConfigRuleDoc(JsonDocument& doc, const char* sourceName, const char* ruleName, const ConfigLoader::RuleConfig& rule);
    void _buildConfigCurrentDoc(JsonDocument& doc, const ConfigLoader::CurrentSensorConfigEntry& sensor);
    const ConfigLoader::RuleConfig* _findRuleConfig(const ConfigLoader::SourceConfigEntry& source, const char* ruleName);

    bool _publish(const char* topic, const char* payload, bool retain);
    void _syncTime();
    bool _timeIsValid() const;
    void _publishTelemetry(const Topics::TelemetryGroup& group, JsonDocument& doc);
    void _publishMeasures();
    void _publishCoils();
    void _publishStatus();
    void _publishTargets();
    void _publishAlert(const char* source, const char* type, int count, int limit);
    void _publishResult();


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

    // RemoteConfig::Output
    bool publishConfigMessage(const char* topic, const char* payload, bool retain) override;
    bool experimentInProgress() override;
    void onScreenChanged(ScreenType from, ScreenType to) override;
    void onScreenEvent(ScreenEvent e) override;
    void onStateChanged(const char* oldState, const char* newState) override;

};

inline MySystem::MySystem(SerialLink& serial, Timer& timer, WiFiManager& wifiManager, BrokerManager& brokerManager, DisplayDriver& display, ScreenManager& screenManager, SdStorage& sdStorage) :
  _serial(serial), 
  _timer(timer), 
  _wifiManager(wifiManager), 
  _brokerManager(brokerManager), 
  _display(display), 
  _screenManager(screenManager),
  _sdStorage(sdStorage),
  _configReceiver(sdStorage)
{
  _serial.setListener(this);
  _timer.setTimerListener(this);
  _wifiManager.setListener(this);
  _brokerManager.setListener(this);
  _data.setStateListener(this);
}

inline void MySystem::begin() {
  // Restaura la seleccion del operador antes de calcular los labels: si hay
  // una guardada en la SD, Principal tiene que mostrar ESA y no el indice 0
  // de cada menu. Corre despues de ConfigLoader (que ya definio los menus),
  // asi que los indices se validan contra los menus definitivos. Sin SD no
  // pasa nada: quedan los defaults compilados.
  SelectionStore::load(_sdStorage, _data.configuration);

  // Se lee una sola vez porque es lo unico que se puede saber: la libreria
  // SD no notifica insercion ni extraccion (ver CommunicationData).
  _data.communication.sdOk = _sdStorage.isReady();

  // Los labels de Principal se calculan una vez en el constructor de
  // SystemData, que corre en la inicializacion estatica -- antes de
  // setup() y por lo tanto antes de ConfigLoader::load(). Sin esta
  // relectura la pantalla muestra los defaults compilados mientras
  // _sendStart() manda al Mega los valores de la SD: dos numeros
  // distintos para el mismo experimento. begin() corre despues de
  // ConfigLoader, asi que aca las tablas ya son las definitivas.
  _data.updatePrincipalConfigurationLabels();

  // Configuracion remota (tarjeta 24): la cola entre nucleos y la
  // suscripcion. subscribe() guarda el topic y lo reaplica en cada
  // reconexion del broker.
  _configInbox = xQueueCreate(4, sizeof(ConfigInboxMessage));
  _brokerManager.subscribe(RemoteConfig::TopicSet);
  _pingInbox = xQueueCreate(2, sizeof(RemotePing::Request));
  _brokerManager.subscribe(RemotePing::TopicPing);

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
  // -1 = el manager no muestra ninguna pantalla en init(). Cual es la
  // primera lo decide el _processState(Idle) de abajo, que ademas deja el
  // StateData coherente con lo que se ve; con initialIndex=0 el splash se
  // mostraba dos veces (una acá y otra ahí) y habia dos lugares
  // decidiendo lo mismo.
  const int initialIndex = -1;

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
  _updateRemoteConfig();
  _answerPings();
}

// Contesta los pings del monitor con lo minimo para saber que la placa esta
// viva y en que anda. Va por la cola de publicacion como la telemetria: si el
// broker se cayo, el pong se pierde y el monitor lo ve como "sin respuesta",
// que es exactamente lo que paso.
inline void MySystem::_answerPings() {
  if (_pingInbox == nullptr) return;
  RemotePing::Request request;
  while (xQueueReceive(_pingInbox, &request, 0) == pdTRUE) {
    char payload[160];
    size_t length = RemotePing::buildPong(request, _data.getState(), ConfigLoader::configId(),
                                          ConfigLoader::loadStatus(), _serial.isConnected(), millis() / 1000,
                                          payload, sizeof(payload));
    if (length == 0) continue;
    DEBUG_PRINTF(DEBUG_MYSYSTEM, "[PING] %s -> pong\n", request.id);
    _publish(RemotePing::TopicPong, payload, false);
  }
}

inline void MySystem::_updateRemoteConfig() {
  if (_configInbox != nullptr) {
    ConfigInboxMessage message;
    while (xQueueReceive(_configInbox, &message, 0) == pdTRUE) {
      _configReceiver.handle(message.payload, *this);
    }
  }
  _configReceiver.checkTimeout(*this);

  // Al conectar (y en cada reconexion) se republica la configuracion
  // vigente, retenida: el monitor la encuentra aunque se conecte despues.
  if (_publishConfigRequested) {
    _publishConfigRequested = false;
    _configPublisher.begin(ConfigLoader::currentText());
    _timer.addTask(Tasks::PublishCurrentConfig, 300);
  }
}

inline bool MySystem::publishConfigMessage(const char* topic, const char* payload, bool retain) {
  return _publish(topic, payload, retain);
}

// Starting y Stopping cuentan como "en curso": el Mega puede estar
// energizando o cortando, y grabar un archivo en ese momento no aporta nada.
inline bool MySystem::experimentInProgress() {
  const char* state = _data.getState();
  return strcmp(state, StateData::Running) == 0
      || strcmp(state, StateData::Starting) == 0
      || strcmp(state, StateData::Stopping) == 0;
}

inline void MySystem::remoteUpdate() {
  _wifiManager.update();

  // Sin hora valida no se intenta conectar al broker: el handshake TLS
  // fallaria igual por fecha de certificado, y cada intento fallido es un
  // handshake completo que se come tiempo de CPU para nada.
  if (!_timeIsValid()) {
    static bool warned = false;
    if (!warned && _data.communication.wifiOk) {
      warned = true;
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, F("[NTP] esperando hora valida antes de conectar al broker"));
    }
    return;
  }

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
    // MODO DE EXPERIMENTO (campo X / campo nulo)
    // ========================================================

    // Se manda SIEMPRE, aunque hoy sea siempre "x": el Mega trata `mode`
    // como opcional justamente para tolerar una pantalla vieja que no lo
    // mande (engine.hpp, handler de Start), pero esta no lo es. Mandarlo
    // explicito deja el modo visible en el log del Mega en cada arranque,
    // que es donde se confirma en banco cual de las dos condiciones
    // experimentales quedo activa.

    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "[START] Obteniendo modo de experimento...");

    auto mode = ConfigurationOptions::optionsFieldMode[
        _data.configuration.fieldModeOption
    ].value;

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] fieldModeOption = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, _data.configuration.fieldModeOption);

    DEBUG_PRINT(DEBUG_MYSYSTEM, "[START] mode = ");
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, mode);

    doc["mode"] = mode;


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
  if (control.hasBalanceMax) doc["balanceMax"] = control.balanceMax;
  // `enabled` se leia de la SD pero nunca llegaba al Mega: control.enabled
  // = false no tenia efecto. Va en el mismo frame que el resto del control.
  if (control.hasEnabled) doc["enabled"] = control.enabled;
}

// Un punto del mapa de calibracion. Claves cortas por el presupuesto del
// frame (test_config_map_fits_with_every_key): k indice, n cuantos puntos
// trae la tarjeta, i intensidad, f frecuencia, d duty, b balance.
inline void MySystem::_buildConfigMapDoc(JsonDocument& doc, uint8_t index) {
  const auto& control = ConfigLoader::controlConfig();
  doc["k"] = index;
  doc["n"] = control.mapCount;
  // Tarjeta sin mapa: el frame es solo k=0, n=0, y le dice al Mega que vacie
  // el que tuviera de antes (si no, el campo nulo correria con puntos que
  // esta tarjeta no dice).
  if (control.mapCount == 0) return;
  const ConfigLoader::ControlMapPoint& point = control.map[index];
  doc["i"] = point.intensity;
  doc["f"] = point.frequency;
  doc["d"] = point.duty;
  doc["b"] = point.balance;
}

inline void MySystem::_buildConfigCoilDoc(JsonDocument& doc, const ConfigLoader::CoilConfig& coil) {
  doc["name"] = coil.name;
  if (coil.hasEnabled) doc["enabled"] = coil.enabled;
  if (coil.hasCalibrationFactor) doc["calibrationFactor"] = coil.calibrationFactor;
}

// Solo la cabecera de la fuente: las 3 reglas van en frames config_rule
// aparte porque una fuente entera NO entra en un frame (ver
// test_serialframes en el Mega, que fija ese limite).
// Claves cortas: el frame tiene 256 bytes y una fuente puede traer las 9
// (test_config_scenario_fits_with_every_key, en el Mega, fija el presupuesto).
inline void MySystem::_buildConfigScenarioDoc(JsonDocument& doc, const ConfigLoader::SourceConfigEntry& source) {
  const ConfigLoader::ScenarioConfig& s = source.scenario;
  doc["name"] = source.name;
  if (s.base.has)         doc["b"]  = s.base.value;
  if (s.noise.has)        doc["n"]  = s.noise.value;
  if (s.ramp.has)         doc["r"]  = s.ramp.value;
  if (s.stepAt.has)       doc["sa"] = s.stepAt.value;
  if (s.step.has)         doc["s"]  = s.step.value;
  if (s.oscAmp.has)       doc["oa"] = s.oscAmp.value;
  if (s.oscPeriod.has)    doc["op"] = s.oscPeriod.value;
  if (s.dropAt.has)       doc["da"] = s.dropAt.value;
  if (s.setpointDuty.has) doc["d"]  = s.setpointDuty.value;
}

inline void MySystem::_buildConfigSourceDoc(JsonDocument& doc, const ConfigLoader::SourceConfigEntry& source) {
  doc["name"] = source.name;
  if (source.hasEnabled) doc["enabled"] = source.enabled;
  if (source.hasMaxMissedSamples) doc["maxMissedSamples"] = source.maxMissedSamples;
  if (source.hasSensor) doc["sensor"] = source.sensor;
  if (source.hasAddress) doc["address"] = source.address;
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
  // Solo si el archivo trae detector.enabled: sin la clave no hay nada que
  // pisar y el Mega se queda con el detector encendido.
  if (ConfigLoader::detectorConfig().hasEnabled) {
    _registerPendingConfig(Commands::ConfigDetector, nullptr);
  }

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
    // El escenario viaja aunque la fuente no lo use hoy: es barato, y una
    // tarjeta que lo trae es porque se piensa usar.
    if (sources[i].scenario.present) {
      _registerPendingConfig(Commands::ConfigScenario, sources[i].name);
    }

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

  // Los puntos del mapa de calibracion se identifican por su indice.
  const ConfigLoader::ControlConfig& controlCfg = ConfigLoader::controlConfig();
  // Sin puntos va igual UN frame (k=0, n=0): vacia el mapa del Mega.
  uint8_t mapFrames = controlCfg.mapCount == 0 ? 1 : controlCfg.mapCount;
  for (uint8_t i = 0; i < mapFrames; i++) {
    char key[ConfigurationOptions::MaxLabelLength];
    snprintf(key, sizeof(key), "%u", i);
    _registerPendingConfig(Commands::ConfigMap, key);
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
    else if (strcmp(command, Commands::ConfigDetector) == 0) {
      doc["enabled"] = ConfigLoader::detectorConfig().enabled;
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
    else if (strcmp(command, Commands::ConfigScenario) == 0) {
      const ConfigLoader::SourceConfigEntry* source = nullptr;
      for (uint8_t s = 0; s < sourceCount; s++) {
        if (strcmp(sources[s].name, key) == 0) source = &sources[s];
      }
      if (source == nullptr) continue;
      _buildConfigScenarioDoc(doc, *source);
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
    else if (strcmp(command, Commands::ConfigMap) == 0) {
      // key es el indice del punto
      uint8_t index = (uint8_t)atoi(key);
      uint8_t mapCount = ConfigLoader::controlConfig().mapCount;
      if (index >= (mapCount == 0 ? 1 : mapCount)) continue;
      _buildConfigMapDoc(doc, index);
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

inline bool MySystem::_publish(const char* topic, const char* payload, bool retain) {
  return _brokerManager.publish(topic, payload, retain);
}

// TLS necesita un reloj: todo certificado tiene fecha de validez, y un ESP32
// arranca en 1970, asi que mbedTLS rechaza hasta el certificado correcto por
// "todavia no es valido". Se dispara al conectar el WiFi y no bloquea; quien
// espera es _timeIsValid(), que mide si ya llego la hora.
inline void MySystem::_syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, F("[NTP] sincronizacion pedida"));
}

// Cualquier fecha posterior al desarrollo de esto alcanza para distinguir
// "el reloj ya se sincronizo" de "sigue en el epoch". No hace falta saber la
// hora exacta, solo que no sea 1970.
inline bool MySystem::_timeIsValid() const {
  return time(nullptr) > 1735689600;   // 2025-01-01
}

// Los nombres de campo y el topic salen de Topics (topics.hpp), que trae los
// defaults compilados (CEM1/TEMP1/BOB1/ESTADO/PROGRESS/ELAPSED_TIME) y los
// pisa la seccion `telemetry` del archivo de la SD. Esos defaults son los que
// espera el dashboard remoto ya configurado: renombrar uno obliga
// a actualizar tambien ese Decoder, o el valor deja de llegar sin error
// visible. BOB1 es el nombre historico del Decoder para la bobina; se reusa
// para la corriente medida (coilCurrent[0], sensor SCT013-1) en vez de agregar
// un campo nuevo.
inline void MySystem::_publishMeasures() {
  JsonDocument doc;
  if (Topics::MagneticField.enabled) doc[Topics::MagneticField.name] = _data.measures.measureMagneticField;
  if (Topics::Temperature.enabled)   doc[Topics::Temperature.name]   = _data.measures.measureTemperature;

  _publishTelemetry(Topics::Measures, doc);
}

// Corriente y duty de cada bobina que EXISTE del otro lado -- las que
// reportaron al menos una vez en un frame coil_data. Publicar las 4 siempre
// mandaria ceros de bobinas que no estan montadas, indistinguibles de una
// bobina real que no esta recibiendo nada.
inline void MySystem::_publishCoils() {
  JsonDocument doc;
  for (uint8_t i = 0; i < MAX_COILS; i++) {
    if (_data.measures.latestCoilUpdate[i] == 0) continue;
    char key[4];
    snprintf(key, sizeof(key), "c%u", i + 1);
    doc[key] = _data.measures.coilCurrent[i];
    snprintf(key, sizeof(key), "d%u", i + 1);
    doc[key] = _data.measures.coilDuty[i];
  }

  _publishTelemetry(Topics::Coils, doc);
}

// Los objetivos del experimento en curso: una sola vez, al arrancar. Sin
// esto el dashboard muestra mediciones sin saber contra que consigna
// compararlas, y sobre todo no distingue campo X de campo nulo -- que es la
// diferencia entre el grupo tratado y el grupo control.
inline void MySystem::_publishTargets() {
  JsonDocument doc;
  const auto& configuration = _data.configuration;

  doc["MODE"] = ConfigurationOptions::optionsFieldMode[configuration.fieldModeOption].value;
  doc["CEM"]  = ConfigurationOptions::optionsFieldIntensity[configuration.targetFieldIntensityOption].intensity;
  doc["FREQ"] = ConfigurationOptions::optionsFrequency[configuration.targetFieldFrequencyOption].freq;
  doc["DUR"]  = ConfigurationOptions::optionsDuration[configuration.targetDurationOption].duration;
  doc["TOL"]  = ConfigurationOptions::optionsTolFieldIntensity[configuration.fieldIntensityToleranceOption].tol;

  const auto& normal = ConfigurationOptions::optionsRangeNormalTemperature[configuration.normalTemperatureRangeOption];
  const auto& critical = ConfigurationOptions::optionsRangeCriticalTemperature[configuration.criticalTemperatureRangeOption];
  doc["TNMIN"] = normal.tmin;
  doc["TNMAX"] = normal.tmax;
  doc["TCMIN"] = critical.tmin;
  doc["TCMAX"] = critical.tmax;
  // Siempre explicito, true o false: la AUSENCIA de TEST es lo que el monitor
  // lee como "firmware anterior a la marca", y no puede confundirse con un
  // experimento declarado. Forzado a true si alguna fuente usa un sensor
  // simulado o un escenario (ConfigLoader::marksRunsAsTest).
  doc["TEST"] = ConfigLoader::marksRunsAsTest();
  // Trazabilidad (tarjeta 23): con que configuracion corrio y que
  // relajaciones tenia. Ver ConfigLoader::configId() / relaxationMask().
  doc["CFG"] = ConfigLoader::configId();
  doc["RLX"] = ConfigLoader::relaxationMask();

  _publishTelemetry(Topics::Targets, doc);
}

// Una por cada flag del Mega, en el momento. No hay version periodica: una
// alerta es un evento, y reenviarla cada N segundos haria imposible
// distinguir una alerta que se repite de la misma alerta reenviada.
inline void MySystem::_publishAlert(const char* source, const char* type, int count, int limit) {
  JsonDocument doc;
  doc["SRC"] = source;
  doc["TYPE"] = type;
  doc["COUNT"] = count;
  doc["LIMIT"] = limit;

  _publishTelemetry(Topics::Alerts, doc);
}

// El cierre del experimento. Es el mensaje mas pesado (lleva la descripcion
// que arma el Mega) y el unico que el monitor remoto no puede deducir de
// ningun otro: sin esto, una corrida cortada por temperatura critica se ve
// desde afuera igual que una que termino bien.
inline void MySystem::_publishResult() {
  JsonDocument doc;
  doc["REASON"] = _data.result.reason;
  doc["DESC"] = _data.result.description;
  doc["PROGRESS"] = (int) _data.result.progressPercent;
  doc["ELAPSED"] = _data.result.elapsed;
  if (_data.result.hasMeanMagneticField) doc["MEAN"] = _data.result.meanMagneticField;
  if (_data.result.source[0] != '\0') {
    doc["SRC"] = _data.result.source;
    doc["TYPE"] = _data.result.type;
    doc["COUNT"] = _data.result.count;
    doc["LIMIT"] = _data.result.limit;
  }
  // Tambien en result: si el monitor se perdio el targets (backend caido al
  // arrancar la corrida), la huerfana que arma con este result conserva la
  // marca igual.
  doc["TEST"] = ConfigLoader::marksRunsAsTest();

  _publishTelemetry(Topics::Result, doc);
}

// Serializa y publica, salvo que no haya nada que mandar o que el payload no
// entre en el buffer.
//
// El chequeo de truncado NO es defensivo de mas: serializeJson recorta en
// silencio si no entra y devuelve lo que escribio, y hasta ahora ese retorno
// se ignoraba. Con los nombres de campo configurables desde la SD, un par de
// nombres largos alcanzan para pasarse de 128 y publicar un JSON cortado, que
// el dashboard no puede parsear: se perderia la tanda entera sin un
// solo error visible. Preferimos no publicar y dejarlo dicho en el log.
inline void MySystem::_publishTelemetry(const Topics::TelemetryGroup& group, JsonDocument& doc) {
  if (!group.enabled) return;
  if (doc.size() == 0) return;

  char payload[Topics::MaxPayloadLength];
  size_t length = serializeJson(doc, payload, sizeof(payload));

  // serializeJson trunca en silencio si no entra, y un JSON truncado es
  // ilegible para el Decoder: se perderia la tanda entera sin una linea que
  // lo explique. Mejor no publicar y decirlo.
  if (length >= sizeof(payload) - 1) {
    DEBUG_PRINT(DEBUG_MYSYSTEM, F("[TELEMETRIA] payload de "));
    DEBUG_PRINT(DEBUG_MYSYSTEM, group.topic);
    DEBUG_PRINT(DEBUG_MYSYSTEM, F(" no entra en "));
    DEBUG_PRINT(DEBUG_MYSYSTEM, (int)sizeof(payload));
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, F(" bytes -- no se publica"));
    return;
  }

  _publish(group.topic, payload, group.retain);
}

inline void MySystem::_publishStatus() {
  JsonDocument doc;
  if (Topics::Health.enabled)      doc[Topics::Health.name]      = _data.progress.health;
  if (Topics::Progress.enabled)    doc[Topics::Progress.name]    = (int)_data.progressPercent();
  if (Topics::ElapsedTime.enabled) doc[Topics::ElapsedTime.name] = _data.elapsedTime();
  doc["REMAINING"] = (unsigned long) _data.remainingSeconds();
  doc["STATE"] = _data.getState();
  // Estado del enlace con el Mega: desde afuera es la diferencia entre "el
  // experimento va bien" y "hace rato que no sabemos nada de la placa que
  // lo controla".
  doc["MEGA"] = _data.communication.serialOk;

  _publishTelemetry(Topics::Status, doc);
}




inline void MySystem::_processState(const char* status, bool forceStatus) {
  IScreen* screen = _screenManager.getCurrent();
  // En la primera llamada (el Idle de begin()) todavia no se mostro
  // ninguna pantalla -- ScreenManager arranca con initialIndex = -1 y
  // getCurrent() devuelve nullptr. SPLASH es la respuesta correcta ahi:
  // es lo que ui_init() dejo cargado en el display.
  ScreenType current = (screen != nullptr) ? screen->getType() : ScreenType::SPLASH;
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
    _data.clearAlertHistory();
    // Todas las mediciones arrancan en 0, no en lo que dejo el experimento
    // anterior ni en los valores de maqueta del export: hasta que llegue la
    // primera muestra, 0 es lo unico que se puede afirmar.
    _data.measures.measureMagneticField = 0.0f;
    _data.measures.measureTemperature = 0.0f;
    for (uint8_t i = 0; i < MAX_COILS; i++) {
      _data.measures.coilCurrent[i] = 0.0f;
      _data.measures.coilDuty[i] = 0.0f;
      _data.measures.latestCoilUpdate[i] = 0;
    }
    _data.measures.latestMagneticFieldUpdate = 0;
    _data.measures.latestTemperatureUpdate = 0;
    snprintf(_data.progress.health, sizeof(_data.progress.health), "%s", HealthData::Normal);
    // Acumulador del campo medio que muestra Resultado: arranca de cero
    // con cada experimento.
    _data.measures.magneticFieldSum = 0.0;
    _data.measures.magneticFieldSamples = 0;
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
      else if(strcmp(ack, Commands::ConfigIntervals) == 0 || strcmp(ack, Commands::ConfigControl) == 0
              || strcmp(ack, Commands::ConfigDetector) == 0) {
        _cancelPendingConfig(ack, nullptr);
      }
      else if(strcmp(ack, Commands::ConfigCoil) == 0 || strcmp(ack, Commands::ConfigSource) == 0
              || strcmp(ack, Commands::ConfigScenario) == 0) {
        _cancelPendingConfig(ack, params["name"] | "");
      }
      else if(strcmp(ack, Commands::ConfigMap) == 0) {
        char key[ConfigurationOptions::MaxLabelLength];
        snprintf(key, sizeof(key), "%d", params["k"] | -1);
        _cancelPendingConfig(ack, key);
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
    _applyResult(reason, description, params);
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
    // Dos registros distintos a proposito: pushAlert dedupe por fuente+tipo
    // (lo que necesitan la salud y Resultado) y pushAlertEvent NO dedupe,
    // porque los 4 paneles de En curso muestran los ultimos 4 EVENTOS.
    _data.pushAlertEvent(source, type);
    _publishAlert(source, type, count, limit);

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
      // Solo cuenta para el campo medio mientras corre el experimento:
      // fuera de Running el Mega sigue mandando cem_data (campo ambiente).
      if (strcmp(_data.getState(), StateData::Running) == 0) {
        _data.addMagneticFieldSample(cem1);
      }
    }
  }
  else if(strcmp(command, Commands::CoilData) == 0) {
    // "dN" (duty aplicado, en %) y "cN" (corriente del SCT013-N) por cada
    // bobina REGISTRADA en el Mega; ver Engine::_sendCoilData(). Una bobina
    // que no aparece en el frame no existe del otro lado, y por eso la
    // pantalla no le dibuja fila: el sello latestCoilUpdate se marca solo
    // para las que si vinieron.
    //
    // El duty puede venir sin la corriente (bobina registrada cuyo sensor
    // no esta, que es el caso de bring-up con un solo ADS1115), asi que
    // cada clave se mira por separado y el sello lo marca cualquiera de
    // las dos.
    for (uint8_t i = 0; i < MAX_COILS; i++) {
      char key[4];
      bool reported = false;

      snprintf(key, sizeof(key), "d%u", i + 1);
      if (params[key].is<float>()) {
        _data.measures.coilDuty[i] = params[key].as<float>();
        reported = true;
      }

      snprintf(key, sizeof(key), "c%u", i + 1);
      if (params[key].is<float>()) {
        _data.measures.coilCurrent[i] = params[key].as<float>();
        reported = true;
      }

      if (reported) _data.measures.latestCoilUpdate[i] = millis();
    }
  }
  
}

inline void MySystem::onTimer(const char* name) {

  if(strcmp(name, Tasks::UpdateScreens) == 0) {
    _screenManager.update();
  }
  else if(strcmp(name, Tasks::PublishCurrentConfig) == 0) {
    _configPublisher.publishNext(*this, ConfigLoader::configId());
    if (!_configPublisher.active()) _timer.removeTask(Tasks::PublishCurrentConfig);
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
  else if(strcmp(name, Tasks::PublishCoils) == 0) {
    _publishCoils();
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

inline bool MySystem::_benchWithoutMega() {
  return !ConfigLoader::requireMega() && !_serial.isConnected();
}

inline void MySystem::onSerialDisconnected() {
  _data.communication.serialOk = false;
}

// Guarda el resultado y navega a Resultado. Lo llama el handler de
// result_data y, en banco sin Mega, el puente de Detener.
inline void MySystem::_applyResult(const char* reason, const char* description) {
  _applyResult(reason, description, JsonVariantConst());
}

inline void MySystem::_applyResult(const char* reason, const char* description, JsonVariantConst params) {
  snprintf(_data.result.reason, sizeof(_data.result.reason), "%s", reason);
  snprintf(_data.result.description, sizeof(_data.result.description), "%s", description);

  // Campos crudos: solo vienen cuando aplican al motivo. Lo que no vino
  // queda en su valor por defecto y la pantalla cae en `description`.
  _data.result.source[0] = '\0';
  _data.result.type[0] = '\0';
  _data.result.count = 0;
  _data.result.limit = 0;
  _data.result.fromEmergency = false;
  _data.result.cause[0] = '\0';

  if (params["cause"].is<const char*>()) {
    snprintf(_data.result.cause, sizeof(_data.result.cause), "%s", params["cause"].as<const char*>());
  }
  if (params["source"].is<const char*>()) {
    snprintf(_data.result.source, sizeof(_data.result.source), "%s", params["source"].as<const char*>());
  }
  if (params["type"].is<const char*>()) {
    snprintf(_data.result.type, sizeof(_data.result.type), "%s", params["type"].as<const char*>());
  }
  if (params["count"].is<uint16_t>()) _data.result.count = params["count"].as<uint16_t>();
  if (params["limit"].is<uint16_t>()) _data.result.limit = params["limit"].as<uint16_t>();
  if (params["emerg"].is<bool>())     _data.result.fromEmergency = params["emerg"].as<bool>();

  // Snapshot de lo que el ESP32 ya venia trackeando en vivo, congelado en
  // el instante del corte (ver comentario de ResultData en systemdata.hpp).
  snprintf(_data.result.health, sizeof(_data.result.health), "%s", _data.progress.health);
  _data.result.progressPercent = _data.progressPercent();
  snprintf(_data.result.elapsed, sizeof(_data.result.elapsed), "%s", _data.elapsedTime());
  _data.result.elapsedSeconds = _data.elapsedSeconds();
  snprintf(_data.result.fieldMode, sizeof(_data.result.fieldMode), "%s", _data.principal.fieldModeValue);
  _data.result.hasMeanMagneticField = _data.measures.magneticFieldSamples > 0;
  _data.result.meanMagneticField = _data.meanMagneticField();
  _data.result.alertCount = _data.activeAlertCount();

  // Start rechazado: el experimento termino antes de empezar. Los getters
  // de arriba leyeron el progreso de la corrida ANTERIOR (startTime y
  // duration siguen siendo los suyos), asi que se ponen a cero a mano.
  // Tampoco se publica: sin un targets previo, el monitor web armaria con
  // este result una corrida huerfana de un experimento que nunca existio.
  bool refused = strcmp(reason, "refused") == 0;
  if (refused) {
    _data.result.progressPercent = 0.0f;
    snprintf(_data.result.elapsed, sizeof(_data.result.elapsed), "00:00:00");
    _data.result.elapsedSeconds = 0;
    _data.result.hasMeanMagneticField = false;
    _data.result.meanMagneticField = 0.0f;
    _data.result.alertCount = 0;

    // El Mega sigue en Ready: se cierra aca la espera del start. Sin esto el
    // estado quedaria en Starting y el reenvio de start vivo.
    if (strcmp(_data.getState(), StateData::Starting) == 0) {
      _data.setState(StateData::Ready);
    }
  }
  else {
    // Se publica DESPUES de armar el snapshot completo: el mensaje lleva el
    // progreso y el transcurrido congelados, no los getters en vivo.
    _publishResult();
  }

  DEBUG_PRINT(DEBUG_MYSYSTEM, F("[RESULT] reason="));
  DEBUG_PRINT(DEBUG_MYSYSTEM, reason);
  DEBUG_PRINT(DEBUG_MYSYSTEM, F(" description="));
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, description);

  _screenManager.show(ScreenType::RESULT);
}

inline void MySystem::onWiFiConnected(const char* ssid) {
  _data.communication.wifiOk = true;
  _syncTime();
}

inline void MySystem::onWiFiDisconnected() {
  _data.communication.wifiOk = false;
}

inline void MySystem::onBrokerConnected() {
  _data.communication.brokerOk = true;
  _publishConfigRequested = true;
}

inline void MySystem::onBrokerDisconnected() {
  _data.communication.brokerOk = false;
}

// Escucha biosoft/config/set (tarjeta 24) y biosoft/ping. Corre en el nucleo de
// comunicaciones: no procesa nada, encola para el loop principal. Con la cola
// llena el bloque se descarta -- el monitor no recibe su confirmacion y lo
// reenvia.
inline void MySystem::onMessageReceived(const char* topic, const char* payload) {
  if (strcmp(topic, RemotePing::TopicPing) == 0) {
    RemotePing::Request request;
    if (_pingInbox != nullptr && RemotePing::parse(payload, request)) xQueueSend(_pingInbox, &request, 0);
    return;
  }
  if (_configInbox == nullptr || strcmp(topic, RemoteConfig::TopicSet) != 0) return;
  ConfigInboxMessage message;
  strncpy(message.payload, payload, sizeof(message.payload) - 1);
  message.payload[sizeof(message.payload) - 1] = '\0';
  xQueueSend(_configInbox, &message, 0);
}

inline void MySystem::onScreenChanged(ScreenType from, ScreenType to) {
  // Las pantallas se crean y se destruyen al navegar (ver ScreenManager),
  // asi que el heap se mueve en cada transicion. Este log es la forma de
  // ver que efectivamente se libera y que el bloque contiguo se mantiene
  // grande -- que es lo que necesita el handshake TLS.
  DEBUG_PRINTF(DEBUG_MYSYSTEM, "[HEAP] pantalla -> libre %u, bloque mayor %u\n",
               (unsigned) ESP.getFreeHeap(), (unsigned) ESP.getMaxAllocHeap());
}

inline void MySystem::onScreenEvent(ScreenEvent e) {
  DEBUG_PRINT(DEBUG_MYSYSTEM, "Screen event: ");
  DEBUG_PRINTLN(DEBUG_MYSYSTEM, e.name);

  if(e.type == ScreenType::SPLASH && e.name == EventName::Timeout) {
    _data.setInitialized(true);
    if(_benchWithoutMega()) {
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, "requireMega=false: sin Mega, paso a Principal sin esperar state_data");
      _processState(StateData::Ready, true);
    }
  }
  else if(e.type == ScreenType::PRINCIPAL && e.name == EventName::GoToConfig) {
    _screenManager.show(ScreenType::CONFIG);
  }
  else if(e.type == ScreenType::PRINCIPAL && e.name == EventName::Start) {
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "Procesando start!");
    if(_benchWithoutMega()) {
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, "requireMega=false: sin Mega, paso a Running sin mandar start");
      _processState(StateData::Running);
      return;
    }
    // Con requireMega (el default) Iniciar exige la placa de control: es
    // quien energiza las bobinas y quien corta por seguridad. Normalmente
    // sin Mega ni se llega a Principal, pero el enlace puede caerse estando
    // ahi; mandar start a nadie solo dejaria al operador en Esperando hasta
    // el timeout.
    if(!_serial.isConnected()) {
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, "Iniciar ignorado: sin conexion con el Mega (requireMega)");
      return;
    }
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
    if(_benchWithoutMega()) {
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, "requireMega=false: sin Mega, fabrico result_data stopped");
      _data.setState(StateData::Ready);
      _applyResult("stopped", "Detenido desde el banco (sin Mega)");
      return;
    }
    _sendStop();
    // Mismo patron redundante-pero-seguro que el caso Start de arriba.
    _timer.addTask(Tasks::ReSendStop, Intervals::ReSendStop);
    _processState(StateData::Stopping);
  }
  else if(e.type == ScreenType::CONFIG && e.name == EventName::Back) {
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(e.type == ScreenType::CONFIG && e.name == EventName::Save) {
    // ConfigurationController::onSave() (screencontroller.hpp) ya escribio
    // _data.configuration ANTES de emitir este evento (y ya valido que los
    // rangos de temperatura esten anidados); aca solo se persiste y se
    // navega. El resultado de save() se ignora a proposito: sin tarjeta SD
    // la seleccion vale igual para esta sesion, solo no sobrevive al
    // reinicio, y no hay nada que el operador pueda hacer al respecto
    // desde esta pantalla.
    SelectionStore::save(_sdStorage, _data.configuration);
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(e.type == ScreenType::RESULT && e.name == EventName::Back) {
    _data.result = ResultData();
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  // REPETIR: el mismo camino que Iniciar desde Principal, con la
  // configuracion intacta -- desde Resultado no se puede haber cambiado, la
  // pantalla Configuraciones no es alcanzable desde aca.
  else if(e.type == ScreenType::RESULT && e.name == EventName::Repeat) {
    DEBUG_PRINTLN(DEBUG_MYSYSTEM, "Procesando repetir!");
    _data.result = ResultData();

    if(_benchWithoutMega()) {
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, "requireMega=false: sin Mega, paso a Running sin mandar start");
      _processState(StateData::Running);
      return;
    }
    if(!_serial.isConnected()) {
      DEBUG_PRINTLN(DEBUG_MYSYSTEM, "Repetir ignorado: sin conexion con el Mega (requireMega)");
      return;
    }
    _sendStart();
    _timer.addTask(Tasks::ReSendStart, Intervals::ReSendStart);
    _processState(StateData::Starting);
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
    _timer.removeTask(Tasks::PublishCoils);
    _timer.removeTask(Tasks::PublishStatus);
  }

  if(strcmp(newState, StateData::Ready) == 0) {
    _timer.addTask(Tasks::ReSendReset, Intervals::ReSendReset);
  }
  else if(strcmp(newState, StateData::Running) == 0) {
    _timer.addTask(Tasks::UpdateProgress, Intervals::UpdateProgress);
    // Telemetria al monitor remoto (via EMQX) -- solo mientras corre el
    // experimento, no tiene sentido gastar cuota del plan en idle.
    // Una tarea por grupo periodico, cada una con SU intervalo. Un grupo
    // deshabilitado no registra tarea: _publishTelemetry igual lo filtraria,
    // pero no tiene sentido despertar al timer para descartar.
    if (Topics::Measures.enabled) _timer.addTask(Tasks::PublishMeasures, Topics::Measures.interval);
    if (Topics::Coils.enabled)    _timer.addTask(Tasks::PublishCoils, Topics::Coils.interval);
    if (Topics::Status.enabled)   _timer.addTask(Tasks::PublishStatus, Topics::Status.interval);

    // Los objetivos van una sola vez, al arrancar: no cambian durante el
    // experimento y repetirlos seria trafico repetido sin informacion nueva.
    _publishTargets();
  }
  else if(strcmp(newState, StateData::Starting) == 0) {
    _timer.addTask(Tasks::ReSendStart, Intervals::ReSendStart);
  }
  else if(strcmp(newState, StateData::Stopping) == 0) {
    _timer.addTask(Tasks::ReSendStop, Intervals::ReSendStop);
  }
}
