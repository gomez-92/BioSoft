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
    

  public:
    MySystem(SerialLink& serial, Timer& timer, WiFiManager& wifiManager, BrokerManager& brokerManager, DisplayDriver& display, ScreenManager& screenManager);
    void begin();
    void update();
    void remoteUpdate();

  private:
    void _sendStart();
    void _sendStop();
    void _sendReset();

    bool _publish(const char* topic, const char* payload);
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

inline bool MySystem::_publish(const char* topic, const char* payload) {
  return _brokerManager.publish(topic, payload);
}

// Nombres de campo (CEM1/TEMP1/BOB1/ESTADO/PROGRESS/ELAPSED_TIME) elegidos
// para calzar con el Decoder ya configurado del lado de Datacake -- no
// cambiar sin actualizar tambien ese Decoder. BOB1 es el nombre historico
// del Decoder para la bobina; se reusa para la corriente medida
// (measureCurrent, sensor SCT013) en vez de agregar un campo nuevo.
inline void MySystem::_publishMeasures() {
  JsonDocument doc;
  doc["CEM1"] = _data.measures.measureMagneticField;
  doc["TEMP1"] = _data.measures.measureTemperature;
  doc["BOB1"] = _data.measures.measureCurrent;

  char payload[128];
  serializeJson(doc, payload, sizeof(payload));
  _publish(Topics::Telemetry, payload);
}

inline void MySystem::_publishStatus() {
  JsonDocument doc;
  doc["ESTADO"] = _data.progress.health;
  doc["PROGRESS"] = (int)_data.progressPercent();
  doc["ELAPSED_TIME"] = _data.elapsedTime();

  char payload[128];
  serializeJson(doc, payload, sizeof(payload));
  _publish(Topics::Telemetry, payload);
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
}

inline void MySystem::onSerialConnected() {
  _data.communication.serialOk = true;
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
      _processState(StateData::Running);
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
