#pragma once
#include <Arduino.h>

#include "tasks.hpp"
#include "intervals.hpp"
#include "commands.hpp"
#include "enginestate.hpp"
#include "runtimestate.hpp"
#include "timer.hpp"
#include "seriallink.hpp"
#include "magnetometermanager.hpp"
#include "thermometermanager.hpp"
#include "currentmanager.hpp"
#include "signalgenerator.hpp"
#include "pwmdriver.hpp"
#include "fieldcontroller.hpp"
#include "relaymanager.hpp"
#include "detector.hpp"

class Engine : 
  public EngineStateListener,
  public RuntimeStateListener,
  public TimerListener, 
  public CommandListener,
  public IMagnetometerListener,
  public IThermometerListener,
  public ICurrentSensorListener,
  public DetectorListener
{
  private:
    EngineState _engineState;
    RuntimeState _runtimeState;
    Timer& _timer;
    SerialLink& _serial;
    MagnetometerManager& _magnetometerManager;
    ThermometerManager& _thermometerManager;
    CurrentSensorsManager& _currentSensorsManager;
    SignalGenerator& _signalGenerator;
    PwmDriver& _pwmDriver;
    FieldController& _fieldController;
    RelayManager& _relayManager;
    Detector& _detector;
    bool _isSettlingTime;

    unsigned long _temperatureSampleSendInterval = 3000;
    unsigned long _currentSampleSendInterval = 3000;
    unsigned long _magnetometerSampleSendInterval = 3000;

    unsigned long _lastTemperatureSampleSent;
    unsigned long _lastCurrentSampleSent;
    unsigned long _lastMagnetometerSampleSent;

  public:
    Engine( 
      Timer& timer, 
      SerialLink& serial,
      MagnetometerManager& magnetometerManager, 
      ThermometerManager& thermometerManager,
      CurrentSensorsManager& currentSensorsManager, 
      SignalGenerator& signalGenerator,
      PwmDriver& pwmDriver,
      FieldController& fieldController,
      RelayManager& relayManager, 
      Detector& detector
    );

    void begin();
    void update();
    
  private:
    void _start();
    void _stop();
    void _reset();
    void _evaluateHealthData();

    void _sendState();
    //void _sendTargetData();
    //void _sendProgressData();
    void _sendResultData();
    //void _sendVitalsData();
    
    //void _setTarget(JsonVariantConst params);
    //void _configureSource(JsonVariantConst params);
    void _readRule(JsonVariantConst json, Rule& rule);
    void _sendConfirmSourceConfig(const char* sourceName);

    void _sendFlagsData();
    void _sendTempData();
    void _sendCemData();
    void _sendCurrentData();

  public:
    void onReady() override;
    void onStart() override;
    void onFinish() override;
    void onTarget() override;
    void onProgress() override;
    void onResult() override;
    void onHealth() override;

    void onTimer(const char* name) override;
    void onSerialConnected() override;
    void onSerialDisconnected() override;
    void onCommand(const char* command, JsonVariantConst params) override;
    void onMagnetometerSample(IMagnetometer* magnetometer) override;
    void onThermometerSample(IThermometer* thermometer) override;
    void onCurrentSensorSample(ICurrentSensor* sensor) override;
    void onFlag(SourceEvent& event) override;
};

inline Engine::Engine(
  Timer& timer, 
  SerialLink& serial,
  MagnetometerManager& magnetometerManager, 
  ThermometerManager& thermometerManager,
  CurrentSensorsManager& currentSensorsManager, 
  SignalGenerator& signalGenerator,
  PwmDriver& pwmDriver,
  FieldController& fieldController,
  RelayManager& relayManager, 
  Detector& detector
) : 
  _timer(timer),
  _serial(serial),
  _magnetometerManager(magnetometerManager),
  _thermometerManager(thermometerManager),
  _currentSensorsManager(currentSensorsManager),
  _signalGenerator(signalGenerator),
  _pwmDriver(pwmDriver),
  _fieldController(fieldController),
  _relayManager(relayManager),
  _detector(detector),
  _isSettlingTime(false),
  _lastTemperatureSampleSent(0),
  _lastCurrentSampleSent(0),
  _lastMagnetometerSampleSent(0)
{
  _timer.setTimerListener(this);
  _serial.setListener(this);
  _magnetometerManager.setMagnetometerListener(this);
  _thermometerManager.setThermometerListener(this);
  _currentSensorsManager.setCurrentSensorListener(this);
  _detector.setListener(this);
  _engineState.setListener(this);
  _runtimeState.setListener(this);
}

inline void Engine::begin() {
  
  _relayManager.beginAll();
  _pwmDriver.disable();
  _fieldController.reset();
  _timer.begin();
  _serial.begin();
  _signalGenerator.begin();
  _timer.start();
  _engineState.setState(State::Ready);
}

inline void Engine::update() {
  _timer.tick();
  _serial.update();
}

inline void Engine::_start() {
  _isSettlingTime = true;
  TargetData target = _runtimeState.target();
  _signalGenerator.setSineWave(target.frequencyTarget);
  _fieldController.reset();
  _fieldController.setSetpoint(target.cemTarget);
  _pwmDriver.write(_fieldController.getOutput());
  _pwmDriver.enable();
  _relayManager.closeAll();
  _engineState.setState(State::Running);
}

inline void Engine::_stop() {
  _relayManager.openAll();
  _signalGenerator.off();
  _pwmDriver.disable();
  _fieldController.reset();
  _engineState.setState(State::Finished);
}

inline void Engine::_reset() {
  _relayManager.openAll();
  _signalGenerator.off();
  _pwmDriver.disable();
  _fieldController.reset();
  _detector.reset();
  _engineState.setState(State::Ready);
  _runtimeState.reset();
}

inline void Engine::_evaluateHealthData() {
  float maxScore = 0.0f;

  for (uint8_t i = 0; i < _detector.getCount(); ++i) {
    Source* source = _detector.getSource(i);
    if (source == nullptr) continue;

    if (source->hasCriticalFlags()) {
      _runtimeState.setHealth("critical");
      return;
    }

    maxScore = max(maxScore, source->score());
  }

  if (maxScore >= 75.0f) {
    _runtimeState.setHealth("bad");
  }
  else if (maxScore >= 50.0f) {
    _runtimeState.setHealth("warning");
  }
  else if (maxScore >= 20.0f) {
    _runtimeState.setHealth("good");
  }
  else {
    _runtimeState.setHealth("optimal");
  }
}

inline void Engine::_sendState() {
  JsonDocument doc;
  doc["status"] = _engineState.getState();
  Serial.print("send status: ");
  Serial.println(_engineState.getState());
  _serial.sendCommand(Commands::StateData, doc);
}

/*
inline void Engine::_sendTargetData() {
  TargetData data = _runtimeState.target();
  JsonDocument doc;
  doc["cem"] = data.cemTarget;
  doc["frequency"] = data.frequencyTarget;
  doc["duration"] = data.durationTarget;
  _serial.sendCommand(Commands::TargetData, doc);
}
*/

/*
inline void Engine::_sendProgressData() {
  ProgressData data = _runtimeState.progress();
  JsonDocument doc;
  doc["progress"] = data.progress;
  doc["elapsed"] = data.elapsed;
  _serial.sendCommand(Commands::ProgressData, doc);
}
*/

inline void Engine::_sendResultData() {
  ResultData data = _runtimeState.result();
  JsonDocument doc;
  doc["reason"] = data.reason;
  doc["description"] = data.description;
  _serial.sendCommand(Commands::ResultData, doc);
}

/*
inline void Engine::_sendVitalsData() {
  HealthData data = _runtimeState.health();
  JsonDocument doc;
  doc["health"] = data.health;
  _serial.sendCommand(Commands::HealthData, doc);
}
*/

/*
inline void Engine::_setTarget(JsonVariantConst params) {
  if(!params["cem"] || !params["cem"].is<float>()) return;
  float cem = params["cem"].as<float>();
  
  if(!params["frequency"] || !params["frequency"].is<int>()) return;
  int frequency = params["frequency"].as<int>();

  if(!params["duration"] || !params["duration"].is<unsigned long>()) return;
  unsigned long duration = params["duration"].as<unsigned long>();

  _runtimeState.setTarget(cem, frequency, duration);
}
*/
/*
inline void Engine::_configureSource(JsonVariantConst params) {
  const char* name = params["name"];
  if (name == nullptr) return;
  
  SourceConfig config;
  config.bufferSize = params["bufferSize"] | config.bufferSize;
  config.normalMin   = params["normalMin"]   | config.normalMin;
  config.normalMax   = params["normalMax"]   | config.normalMax;
  config.criticalMin = params["criticalMin"] | config.criticalMin;
  config.criticalMax = params["criticalMax"] | config.criticalMax;
  
  _readRule(params["critical"], config.critical);
  _readRule(params["streak"], config.streak);
  _readRule(params["frequency"], config.frequency);
  
  if(_detector.configureSource(name, config)) {
    _sendConfirmSourceConfig(name);
  }
}
*/

inline void Engine::_readRule(JsonVariantConst json, Rule& rule) {
  if (json.isNull())  return;
  if (!json.is<JsonObjectConst>()) return;
  rule.cooldown = json["cooldown"] | rule.cooldown;
  rule.maxEvents = json["maxEvents"] | rule.maxEvents;
  rule.threshold = json["threshold"] | rule.threshold;
}

inline void Engine::_sendConfirmSourceConfig(const char* sourceName) {
  /*
  JsonDocument doc;
  doc["source"] = sourceName;
  _serial.sendCommand(Commands::SourceConfigOk, doc);
  */
}

inline void Engine::_sendFlagsData() {
  /*
  for (uint8_t i = 0; i < _detector.getCount(); i++) {
    Source* source = _detector.getSource(i);
    if (source == nullptr) continue;
    JsonDocument sourceData;
    sourceData["source"] = source->getName();
    source->publish(sourceData);
    _serial.sendCommand(Commands::FlagsData, sourceData);
  }  
  */
}

inline void Engine::_sendTempData() {
  JsonDocument doc;
  _thermometerManager.publishMeasures(doc);
  _serial.sendCommand(Commands::TempData, doc);
}

inline void Engine::_sendCemData() {
  JsonDocument doc;
  _magnetometerManager.publishMeasures(doc);
  _serial.sendCommand(Commands::CemData, doc);
}

inline void Engine::_sendCurrentData() {
  JsonDocument doc;
  _currentSensorsManager.publishMeasures(doc);
  bool queued = _serial.sendCommand(Commands::CurrentData, doc);
  Serial.print(F("[CURRENT] current_data encolado: "));
  Serial.println(queued ? F("OK") : F("FALLO (cola TX llena)"));
}

// Engine State Callbacks
inline void Engine::onReady() {
  Serial.println("On Ready!");
  _timer.removeTask(Tasks::SendResult);
  _timer.addTask(Tasks::SendState, Intervals::SendState);
}

inline void Engine::onStart() {
  TargetData target = _runtimeState.target();
  //_timer.addTask(Tasks::Finish, target.durationTarget);

  _timer.addTask(Tasks::MeasureTemperature, Intervals::MeasureTemperature);
  _timer.addTask(Tasks::MeasureCurrent, Intervals::MeasureCurrent);
  //_timer.addTask(Tasks::MeasureMagneticField, Intervals::MeasureMagneticField);
  //_timer.addTask(Tasks::UpdateProgress, Intervals::UpdateProgress);
  //_timer.addTask(Tasks::UpdateVitals, Intervals::UpdateVitals);
  //_timer.addTask(Tasks::SendFlags, Intervals::SendFlags);
  //_timer.addTask(Tasks::SettlingTime, Intervals::SettlingTime);
}

inline void Engine::onFinish() {
  _timer.removeTask(Tasks::Finish);
  _timer.removeTask(Tasks::MeasureTemperature);
  _timer.removeTask(Tasks::MeasureCurrent);
  _timer.removeTask(Tasks::MeasureMagneticField);
  _timer.removeTask(Tasks::UpdateProgress);
  _timer.removeTask(Tasks::UpdateVitals);
  _timer.removeTask(Tasks::SendFlags);
  _timer.removeTask(Tasks::SettlingTime);
  
  // debo construir el resultado
  _timer.addTask(Tasks::SendResult, Intervals::SendResult);
}

// Runtime State Callbacks
inline void Engine::onTarget() {
  //_sendTargetData();
}

inline void Engine::onProgress() {
  //_sendProgressData();
}

inline void Engine::onResult() {
  _sendResultData();
}

inline void Engine::onHealth() {
  //_sendVitalsData();
}

// Timer Callback
inline void Engine::onTimer(const char* name) {
  Serial.print("Iniciando: ");
  Serial.println(name);
  unsigned long t1 = millis();
  if(strcmp(name, Tasks::SendState) == 0) {
    _sendState();
  }
  else if(strcmp(name, Tasks::Ping) == 0) {
    _serial.sendPing();
  }
  else if(strcmp(name, Tasks::MeasureTemperature) == 0) {
    _thermometerManager.updateAll();
    JsonDocument doc;
    _thermometerManager.publishMeasures(doc);
    Serial.print(F("[TEMP] JSON: "));
    serializeJson(doc, Serial);
    Serial.println();
  }
  else if(strcmp(name, Tasks::MeasureCurrent) == 0) {
    _currentSensorsManager.updateAll();
    JsonDocument doc;
    _currentSensorsManager.publishMeasures(doc);
    Serial.print(F("[CURRENT] JSON: "));
    serializeJson(doc, Serial);
    Serial.println();
  }
  else if(strcmp(name, Tasks::MeasureMagneticField) == 0) {
    _magnetometerManager.updateAll();
  }
  else if(strcmp(name, Tasks::UpdateProgress) == 0) {
    _runtimeState.updateProgress();
  }
  else if(strcmp(name, Tasks::UpdateVitals) == 0) {
    _evaluateHealthData();
  }
  else if(strcmp(name, Tasks::SendFlags) == 0) {
    _sendFlagsData();
  }
  else if(strcmp(name, Tasks::SendResult) == 0) {
    _sendResultData();
  }
  else if(strcmp(name, Tasks::SettlingTime) == 0) {
    _isSettlingTime = false;
    _timer.removeTask(Tasks::SettlingTime);
  }
  else if(strcmp(name, Tasks::Finish) == 0) {
    _stop();
  }
  unsigned long t2 = millis();
  Serial.print(name);
  Serial.print(" tardó ");
  Serial.print(t2 - t1);
  Serial.println(" ms");
  
}

// Serial Callbacks
inline void Engine::onCommand(const char* command, JsonVariantConst params) {

  Serial.print(F("command: "));
  Serial.println(command);

  if (strcmp(command, Commands::Ping) == 0) {

    if (!params["value"].is<long>()) {
      Serial.println(F("[PING][ERROR] Parametro 'value' invalido o ausente"));
      return;
    }

    long value = params["value"].as<long>();
    _serial.sendPong(value);
  }

  else if (strcmp(command, Commands::Pong) == 0) {

    if (!params["value"].is<long>()) {
      Serial.println(F("[PONG][ERROR] Parametro 'value' invalido o ausente"));
      return;
    }

    long value = params["value"].as<long>();
    _serial.handlePingResponse(value);
  }

  /*
  else if (strcmp(command, Commands::SetTarget) == 0) {
    _setTarget(params);
  }
  else if (strcmp(command, Commands::ConfigSource) == 0) {
    _configureSource(params);
  }
  */

  else if (strcmp(command, Commands::Start) == 0) {

    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("[START] Comando START recibido"));
    Serial.println(F("========================================"));

    // ========================================================
    // CONFIGURANDO TARGETS
    // ========================================================

    Serial.println(F("Configurando targets..."));

    // --- CEM ---
    Serial.println(F("[START] Validando parametro 'cem'..."));

    if (!params["cem"].is<float>()) {
      Serial.println(F("[START][ERROR] Parametro 'cem' invalido o ausente"));
      return;
    }

    float cemTarget = params["cem"].as<float>();

    Serial.print(F("[START] cemTarget = "));
    Serial.println(cemTarget, 4);


    // --- FRECUENCIA ---
    Serial.println(F("[START] Validando parametro 'freq'..."));

    if (!params["freq"].is<int>()) {
      Serial.println(F("[START][ERROR] Parametro 'freq' invalido o ausente"));
      return;
    }

    int freqTarget = params["freq"].as<int>();

    Serial.print(F("[START] freqTarget = "));
    Serial.println(freqTarget);


    // --- DURACION ---
    Serial.println(F("[START] Validando parametro 'dur'..."));

    if (!params["dur"].is<unsigned long>()) {
      Serial.println(F("[START][ERROR] Parametro 'dur' invalido o ausente"));
      return;
    }

    unsigned long durTarget = params["dur"].as<unsigned long>();

    Serial.print(F("[START] durTarget = "));
    Serial.println(durTarget);


    // --- GUARDAR TARGETS ---
    Serial.println(F("[START] Guardando targets en RuntimeState..."));

    _runtimeState.setTarget(
      cemTarget,
      freqTarget,
      durTarget
    );

    Serial.println(F("[START] Targets configuradas correctamente"));


    // ========================================================
    // CONFIGURACION SOURCE CEM
    // ========================================================

    Serial.println();
    Serial.println(F("[START] Configurando source CEM1..."));

    Serial.println(F("[START] Validando parametro 'tol'..."));

    if (!params["tol"].is<int>()) {
      Serial.println(F("[START][ERROR] Parametro 'tol' invalido o ausente"));
      Serial.println(F("[START] Abortando comando START"));
      return;
    }

    int cemTol = params["tol"].as<int>();

    Serial.print(F("[START] cemTol = "));
    Serial.print(cemTol);
    Serial.println(F("%"));


    SourceConfig configCem;

    Serial.println(F("[START] Calculando limites CEM..."));

    configCem.criticalMin =
      cemTarget * (1.0f - 1.25f * cemTol / 100.0f);

    configCem.criticalMax =
      cemTarget * (1.0f + 1.25f * cemTol / 100.0f);

    configCem.normalMin =
      cemTarget * (1.0f - cemTol / 100.0f);

    configCem.normalMax =
      cemTarget * (1.0f + cemTol / 100.0f);


    Serial.print(F("[START] CEM criticalMin = "));
    Serial.println(configCem.criticalMin, 4);

    Serial.print(F("[START] CEM normalMin   = "));
    Serial.println(configCem.normalMin, 4);

    Serial.print(F("[START] CEM normalMax   = "));
    Serial.println(configCem.normalMax, 4);

    Serial.print(F("[START] CEM criticalMax = "));
    Serial.println(configCem.criticalMax, 4);


    Serial.println(F("[START] Aplicando configuracion a CEM1..."));

    _detector.configureSource("CEM1", configCem);

    Serial.println(F("[START] Source CEM1 configurada correctamente"));


    // ========================================================
    // CONFIGURACION SOURCE TEMP
    // ========================================================

    SourceConfig configTemp;

    Serial.println();
    Serial.println(F("[START] Configurando source TEMP1..."));


    // --- TEMP NORMAL MIN ---
    Serial.println(F("[START] Validando parametro 'tnmin'..."));

    if (!params["tnmin"].is<float>()) {
      Serial.println(F("[START][ERROR] Parametro 'tnmin' invalido o ausente"));
      return;
    }

    configTemp.normalMin = params["tnmin"].as<float>();

    Serial.print(F("[START] TEMP normalMin = "));
    Serial.println(configTemp.normalMin, 2);


    // --- TEMP NORMAL MAX ---
    Serial.println(F("[START] Validando parametro 'tnmax'..."));

    if (!params["tnmax"].is<float>()) {
      Serial.println(F("[START][ERROR] Parametro 'tnmax' invalido o ausente"));
      return;
    }

    configTemp.normalMax = params["tnmax"].as<float>();

    Serial.print(F("[START] TEMP normalMax = "));
    Serial.println(configTemp.normalMax, 2);


    // --- TEMP CRITICAL MIN ---
    Serial.println(F("[START] Validando parametro 'tcmin'..."));

    if (!params["tcmin"].is<float>()) {
      Serial.println(F("[START][ERROR] Parametro 'tcmin' invalido o ausente"));
      return;
    }

    configTemp.criticalMin = params["tcmin"].as<float>();

    Serial.print(F("[START] TEMP criticalMin = "));
    Serial.println(configTemp.criticalMin, 2);


    // --- TEMP CRITICAL MAX ---
    Serial.println(F("[START] Validando parametro 'tcmax'..."));

    if (!params["tcmax"].is<float>()) {
      Serial.println(F("[START][ERROR] Parametro 'tcmax' invalido o ausente"));
      return;
    }

    configTemp.criticalMax = params["tcmax"].as<float>();

    Serial.print(F("[START] TEMP criticalMax = "));
    Serial.println(configTemp.criticalMax, 2);


    Serial.println(F("[START] Aplicando configuracion a TEMP1..."));

    _detector.configureSource("TEMP1", configTemp);

    Serial.println(F("[START] Source TEMP1 configurada correctamente"));


    // ========================================================
    // ACK
    // ========================================================

    Serial.println();
    Serial.println(F("[START] Preparando ACK..."));

    JsonDocument doc;

    doc["command"] = Commands::Start;

    Serial.println(F("[START] Enviando ACK..."));

    _serial.sendCommand(
      Commands::Ack,
      doc
    );

    Serial.println(F("[START] ACK enviado correctamente"));


    // ========================================================
    // INICIO
    // ========================================================

    Serial.println();
    Serial.println(F("[START] Iniciando experimento..."));

    _start();

    Serial.println(F("[START] _start() finalizado"));

    Serial.println(F("========================================"));
    Serial.println(F("[START] Comando START procesado"));
    Serial.println(F("========================================"));
    Serial.println();
  }

  else if (strcmp(command, Commands::Stop) == 0) {

    _stop();

    JsonDocument doc;
    doc["command"] = Commands::Stop;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  else if (strcmp(command, Commands::Reset) == 0) {

    _reset();

    JsonDocument doc;
    doc["command"] = Commands::Reset;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }
}

inline void Engine::onSerialConnected() {}
inline void Engine::onSerialDisconnected() {}

// Sample Sensor Callbacks
inline void Engine::onMagnetometerSample(IMagnetometer* magnetometer) {
  _fieldController.update(magnetometer->getMagneticField());
  _pwmDriver.write(_fieldController.getOutput());

  if(!_isSettlingTime) {
    _detector.addSample(magnetometer->getName(), magnetometer->getMagneticField());
  }

  unsigned long now = millis();
  if(now - _lastMagnetometerSampleSent >= _magnetometerSampleSendInterval) {
    _lastMagnetometerSampleSent = now;
    _sendCemData();
  }  
}

inline void Engine::onThermometerSample(IThermometer* thermometer) {
  if(!_isSettlingTime) {
    _detector.addSample(thermometer->getName(), thermometer->getTemperature());
  }

  unsigned long now = millis();
  if(now - _lastTemperatureSampleSent >= _temperatureSampleSendInterval) {
    _lastTemperatureSampleSent = now;
    _sendTempData();
  }
}

inline void Engine::onCurrentSensorSample(ICurrentSensor* sensor) {
  Serial.print(F("[CURRENT] sample "));
  Serial.print(sensor->getName());
  Serial.print(F(" = "));
  Serial.print(sensor->getCurrent());
  Serial.println(F(" A"));

  if(!_isSettlingTime) {
    _detector.addSample(sensor->getName(), sensor->getCurrent());
  }

  unsigned long now = millis();
  if(now - _lastCurrentSampleSent >= _currentSampleSendInterval) {
    _lastCurrentSampleSent = now;
    Serial.println(F("[CURRENT] enviando current_data al ESP32..."));
    _sendCurrentData();
  }
}

// Detector Callback
inline void Engine::onFlag(SourceEvent& event) {
  JsonDocument sourceData;
  const Source* source = event.source;
  sourceData["source"] = source->getName();
  sourceData["count"] = event.count;
  sourceData["limit"] = event.limit;
  if(event.type == EventType::Critical) {
    sourceData["type"] = "critical";
  }
  else if(event.type == EventType::Streak) {
    sourceData["type"] = "streak";
  }
  else if(event.type == EventType::Frequency) {
    sourceData["type"] = "frequency";
  }
  else {
    sourceData["type"] = "unknown";
  }
  //_serial.sendCommand(Commands::OneFlagsData, sourceData);  
}
