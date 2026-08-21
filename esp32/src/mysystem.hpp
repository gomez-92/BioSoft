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
#include "wifimanager.hpp"
#include "broker.hpp"
#include "display.hpp"
#include "screenmanager.hpp"
#include "configurationoptions.hpp"
#include "tasks.hpp"
#include "intervals.hpp"



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
  static ResultadoController result;
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

    Serial.println();
    Serial.println("========================================");
    Serial.println("[START] Preparando comando START");
    Serial.println("========================================");

    JsonDocument doc;

    // ========================================================
    // CEM
    // ========================================================

    Serial.println("[START] Obteniendo target CEM...");

    auto cem = ConfigurationOptions::optionsFieldIntensity[
        _data.configuration.targetFieldIntensityOption
    ].intensity;

    Serial.print("[START] targetFieldIntensityOption = ");
    Serial.println(_data.configuration.targetFieldIntensityOption);

    Serial.print("[START] cem = ");
    Serial.println(cem, 4);

    doc["cem"] = cem;


    // ========================================================
    // FRECUENCIA
    // ========================================================

    Serial.println("[START] Obteniendo frecuencia...");

    auto freq = ConfigurationOptions::optionsFrequency[
        _data.configuration.targetFieldFrequencyOption
    ].freq;

    Serial.print("[START] targetFieldFrequencyOption = ");
    Serial.println(_data.configuration.targetFieldFrequencyOption);

    Serial.print("[START] freq = ");
    Serial.println(freq);

    doc["freq"] = freq;


    // ========================================================
    // DURACION
    // ========================================================

    Serial.println("[START] Obteniendo duracion...");

    auto dur = ConfigurationOptions::optionsDuration[
        _data.configuration.targetDurationOption
    ].duration;

    Serial.print("[START] targetDurationOption = ");
    Serial.println(_data.configuration.targetDurationOption);

    Serial.print("[START] dur = ");
    Serial.println(dur);


    doc["dur"] = dur;


    // ========================================================
    // TOLERANCIA CEM
    // ========================================================

    Serial.println("[START] Obteniendo tolerancia CEM...");

    auto tol = ConfigurationOptions::optionsTolFieldIntensity[
        _data.configuration.fieldIntensityToleranceOption
    ].tol;

    Serial.print("[START] fieldIntensityToleranceOption = ");
    Serial.println(_data.configuration.fieldIntensityToleranceOption);

    Serial.print("[START] tol = ");
    Serial.println(tol);

    doc["tol"] = tol;


    // ========================================================
    // TEMPERATURA NORMAL MIN
    // ========================================================

    Serial.println("[START] Obteniendo temperatura normal minima...");

    auto tnmin = ConfigurationOptions::optionsRangeNormalTemperature[
        _data.configuration.normalTemperatureRangeOption
    ].tmin;

    Serial.print("[START] normalTemperatureRangeOption = ");
    Serial.println(_data.configuration.normalTemperatureRangeOption);

    Serial.print("[START] tnmin = ");
    Serial.println(tnmin, 2);

    doc["tnmin"] = tnmin;


    // ========================================================
    // TEMPERATURA NORMAL MAX
    // ========================================================

    auto tnmax = ConfigurationOptions::optionsRangeNormalTemperature[
        _data.configuration.normalTemperatureRangeOption
    ].tmax;

    Serial.print("[START] tnmax = ");
    Serial.println(tnmax, 2);

    doc["tnmax"] = tnmax;


    // ========================================================
    // TEMPERATURA CRITICA MIN
    // ========================================================

    Serial.println("[START] Obteniendo temperatura critica minima...");

    auto tcmin = ConfigurationOptions::optionsRangeCriticalTemperature[
        _data.configuration.criticalTemperatureRangeOption
    ].tmin;

    Serial.print("[START] criticalTemperatureRangeOption = ");
    Serial.println(_data.configuration.criticalTemperatureRangeOption);

    Serial.print("[START] tcmin = ");
    Serial.println(tcmin, 2);

    doc["tcmin"] = tcmin;


    // ========================================================
    // TEMPERATURA CRITICA MAX
    // ========================================================

    auto tcmax = ConfigurationOptions::optionsRangeCriticalTemperature[
        _data.configuration.criticalTemperatureRangeOption
    ].tmax;

    Serial.print("[START] tcmax = ");
    Serial.println(tcmax, 2);

    doc["tcmax"] = tcmax;


    // ========================================================
    // JSON FINAL
    // ========================================================

    Serial.println();
    Serial.println("[START] JSON generado:");

    serializeJsonPretty(doc, Serial);

    Serial.println();
    Serial.println();

    // ========================================================
    // ENVIO
    // ========================================================

    Serial.println("[START] Enviando comando START...");

    _serial.sendCommand(Commands::Start, doc);

    Serial.println("[START] Comando START enviado correctamente");

    Serial.println("========================================");
    Serial.println("[START] Fin _sendStart()");
    Serial.println("========================================");
    Serial.println();
}

inline void MySystem::_sendStop() {
  JsonDocument doc;
  _serial.sendCommand(Commands::Stop, doc);
}

inline void MySystem::_sendReset() {
  JsonDocument doc;
  _serial.sendCommand(Commands::Reset, doc);
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
    if(current == ScreenType::PRINCIPAL || current == ScreenType::CONFIG) return;
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
  Serial.print("Command: ");
  Serial.println(command);
  if(strcmp(command, Commands::Ping) == 0) {
    if (!params["value"].is<long>()) return;
    long value = params["value"].as<long>();
    Serial.print("value: ");
    Serial.println(value);
    _serial.sendPong(value);
  }
  else if(strcmp(command, Commands::Pong) == 0) {
    if (!params["value"].is<long>()) return;
    long value = params["value"].as<long>();
    Serial.print("value: ");
    Serial.println(value);
    _serial.handlePingResponse(value);
  }
  else if(strcmp(command, Commands::StateData) == 0) {
    if (params.containsKey("status") && params["status"].is<const char*>()) {
      const char* status = params["status"].as<const char*>();
      Serial.print("status: ");
      Serial.println(status);
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
  //else if(strcmp(command, Commands::ResultData) == 0) {}
  //else if(strcmp(command, Commands::FlagsData) == 0) {}
  else if(strcmp(command, Commands::OneFlagsData) == 0) {
    const char* source = params["source"] | "?";
    const char* type   = params["type"] | "?";
    int count = params["count"] | 0;
    int limit = params["limit"] | 0;

    Serial.print(F("[FLAG] "));
    Serial.print(source);
    Serial.print(F(" tipo="));
    Serial.print(type);
    Serial.print(F(" count="));
    Serial.print(count);
    Serial.print(F("/"));
    Serial.println(limit);

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
  Serial.print("Screen event: ");
  Serial.println(e.name);

  if(e.type == ScreenType::SPLASH && e.name == EventName::Timeout) {
    _data.setInitialized(true);
  }
  else if(e.type == ScreenType::PRINCIPAL && e.name == EventName::GoToConfig) {
    _screenManager.show(ScreenType::CONFIG);
  }
  else if(e.type == ScreenType::PRINCIPAL && e.name == EventName::Start) {
    Serial.println("Procesando start!");
    _sendStart();
    _timer.addTask(Tasks::ReSendStart, Intervals::ReSendStart);
    _processState(StateData::Starting);
  }
  else if(e.type == ScreenType::RUNNING && e.name == EventName::Stop) {
    Serial.println("Procesando stop!");
    _sendStop();
    _timer.addTask(Tasks::ReSendStop, Intervals::ReSendStop);
    _processState(StateData::Stopping);
  }
  else if(e.type == ScreenType::CONFIG && e.name == EventName::Back) {
    _screenManager.show(ScreenType::PRINCIPAL);
  }
  else if(e.type == ScreenType::CONFIG && e.name == EventName::Save) {
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
  Serial.print("oldstate: ");
  Serial.println(oldState);

  Serial.print("newstate: ");
  Serial.println(newState);

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
  }
  
  if(strcmp(newState, StateData::Ready) == 0) {
    _timer.addTask(Tasks::ReSendReset, Intervals::ReSendReset);
  }
  else if(strcmp(newState, StateData::Running) == 0) {
    _timer.addTask(Tasks::UpdateProgress, Intervals::UpdateProgress);
  }
  else if(strcmp(newState, StateData::Starting) == 0) {
    _timer.addTask(Tasks::ReSendStart, Intervals::ReSendStart);
  }
  else if(strcmp(newState, StateData::Stopping) == 0) {
    _timer.addTask(Tasks::ReSendStop, Intervals::ReSendStop);
  }
}
