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
#include "coilexcitationmanager.hpp"
#include "pwmdriver.hpp"
#include "fieldcontroller.hpp"
#include "relaymanager.hpp"
#include "detector.hpp"
#include "emergencybutton.hpp"
#include "safetymargins.hpp"
#include "detectorconfigbuilder.hpp"
#include "testmoderesolver.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_ENGINE = true;

class Engine :
  public EngineStateListener,
  public RuntimeStateListener,
  public TimerListener,
  public CommandListener,
  public IMagnetometerListener,
  public IThermometerListener,
  public ICurrentSensorListener,
  public DetectorListener,
  public EmergencyButtonListener
{
  private:
    EngineState _engineState;
    RuntimeState _runtimeState;
    Timer& _timer;
    SerialLink& _serial;
    MagnetometerManager& _magnetometerManager;
    ThermometerManager& _thermometerManager;
    CurrentSensorsManager& _currentSensorsManager;
    CoilExcitationManager& _coilExcitation;
    PwmDriver& _pwmDriver;
    FieldController& _fieldController;
    RelayManager& _relayManager;
    Detector& _detector;
    EmergencyButton& _emergencyButton;
    // Mientras esta en true (ver _start()), las muestras de sensores se
    // siguen registrando/enviando pero NO se pasan al Detector -- ventana
    // inicial para que el sistema se estabilice sin que arranques
    // ficticios disparen flags. Se apaga via Tasks::SettlingTime (onStart()
    // la agrega, el handler en onTimer() la apaga). Si esa tarea alguna vez
    // deja de agregarse, esta bandera queda en true para siempre y el
    // Detector no vuelve a evaluar nada por el resto del experimento.
    bool _isSettlingTime;

    // Bring-up temporal INT-001 (2026-08-31, ver tarjeta Trello "Prueba de
    // integracion -- Control de intensidad CEM"): referencias a AMBOS
    // magnetometros (sim y real) para poder elegir cual esta activo por
    // software segun el parametro "testMode" del comando start, sin
    // reflashear en el laboratorio. Ver _applyTestMode().
    IMagnetometer& _simMagnetometer;
    IMagnetometer& _realMagnetometer;
    // true si el lazo FieldController->PwmDriver debe correr en este run
    // (testMode 2,3,5,6). Con testMode 1/4 ("solo sensado") se mantiene en
    // false y onMagnetometerSample()/_start() no tocan el PWM. Quitar junto
    // con testMode una vez terminado el bring-up (en ese punto el lazo
    // vuelve a correr siempre, como antes de esta rama).
    bool _controlLoopEnabled = false;

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
      CoilExcitationManager& coilExcitation,
      PwmDriver& pwmDriver,
      FieldController& fieldController,
      RelayManager& relayManager,
      Detector& detector,
      EmergencyButton& emergencyButton,
      IMagnetometer& simMagnetometer,
      IMagnetometer& realMagnetometer
    );

    void begin();
    void update();

  private:
    void _start();
    void _stop();
    void _reset();
    void _finish(const char* reason, const char* description);
    void _applyTestMode(uint8_t testMode);

    void _sendState();
    //void _sendTargetData();
    //void _sendProgressData();
    void _sendResultData();

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

    void onTimer(const char* name) override;
    void onSerialConnected() override;
    void onSerialDisconnected() override;
    void onCommand(const char* command, JsonVariantConst params) override;
    void onMagnetometerSample(IMagnetometer* magnetometer) override;
    void onThermometerSample(IThermometer* thermometer) override;
    void onCurrentSensorSample(ICurrentSensor* sensor) override;
    void onFlag(SourceEvent& event) override;
    void onEmergencyButtonPressed() override;
};

inline Engine::Engine(
  Timer& timer, 
  SerialLink& serial,
  MagnetometerManager& magnetometerManager, 
  ThermometerManager& thermometerManager,
  CurrentSensorsManager& currentSensorsManager, 
  CoilExcitationManager& coilExcitation,
  PwmDriver& pwmDriver,
  FieldController& fieldController,
  RelayManager& relayManager,
  Detector& detector,
  EmergencyButton& emergencyButton,
  IMagnetometer& simMagnetometer,
  IMagnetometer& realMagnetometer
) :
  _timer(timer),
  _serial(serial),
  _magnetometerManager(magnetometerManager),
  _thermometerManager(thermometerManager),
  _currentSensorsManager(currentSensorsManager),
  _coilExcitation(coilExcitation),
  _pwmDriver(pwmDriver),
  _fieldController(fieldController),
  _relayManager(relayManager),
  _detector(detector),
  _emergencyButton(emergencyButton),
  _isSettlingTime(false),
  _simMagnetometer(simMagnetometer),
  _realMagnetometer(realMagnetometer),
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
  _emergencyButton.setListener(this);
  _engineState.setListener(this);
  _runtimeState.setListener(this);
}

inline void Engine::begin() {

  _relayManager.beginAll();
  _pwmDriver.disable();
  _fieldController.reset();
  _timer.begin();
  _serial.begin();
  _coilExcitation.begin();
  _emergencyButton.begin();
  _timer.start();
  _engineState.setState(State::Ready);
}

inline void Engine::update() {
  _timer.tick();
  _serial.update();
  _emergencyButton.update();
}

inline void Engine::_start() {
  // Ver comentario de _isSettlingTime en la declaracion de la clase.
  _isSettlingTime = true;
  TargetData target = _runtimeState.target();
  _coilExcitation.start(target.frequencyTarget);
  _fieldController.reset();
  // Bring-up INT-001: con testMode "solo sensado" (_controlLoopEnabled ==
  // false) el PWM se deja apagado -- ver _applyTestMode().
  if (_controlLoopEnabled) {
    _fieldController.setSetpoint(target.cemTarget);
    _pwmDriver.write(_fieldController.getOutput());
    _pwmDriver.enable();
  }
  _relayManager.closeAll();
  _engineState.setState(State::Running);
}

inline void Engine::_stop() {
  _relayManager.openAll();
  _coilExcitation.stop();
  _pwmDriver.disable();
  _fieldController.reset();
  _engineState.setState(State::Finished);
}

inline void Engine::_reset() {
  _relayManager.openAll();
  _coilExcitation.stop();
  _pwmDriver.disable();
  _fieldController.reset();
  _detector.reset();
  _engineState.setState(State::Ready);
  _runtimeState.reset();
}

// Bring-up temporal INT-001: ver comentario de _controlLoopEnabled en la
// declaracion de la clase y testmoderesolver.hpp para la tabla completa.
inline void Engine::_applyTestMode(uint8_t testMode) {
  TestModeConfig cfg = resolveTestMode(testMode);
  _controlLoopEnabled = cfg.controlLoopEnabled;

  _magnetometerManager.clearMagnetometers();
  _magnetometerManager.addMagnetometer(
    cfg.useRealSensor ? &_realMagnetometer : &_simMagnetometer
  );

  _detector.removeSource("CEM1");
  if (cfg.closedLoop) {
    _detector.addSource("CEM1");
  }

  DEBUG_PRINT(DEBUG_ENGINE, F("[TESTMODE] testMode="));
  DEBUG_PRINT(DEBUG_ENGINE, testMode);
  DEBUG_PRINT(DEBUG_ENGINE, cfg.useRealSensor ? F(" sensor=real") : F(" sensor=sim"));
  DEBUG_PRINT(DEBUG_ENGINE, cfg.closedLoop ? F(" detector=on") : F(" detector=off"));
  DEBUG_PRINTLN(DEBUG_ENGINE, cfg.controlLoopEnabled ? F(" control=on") : F(" control=off"));
}

// Punto unico donde termina un experimento (tiempo cumplido, stop manual o
// corte de seguridad por flag). _reset() borra el resultado de
// RuntimeState (via _runtimeState.reset()) y _stop()/_reset() dejan el
// Engine en Ready antes de que el ESP32 pueda ver "finished" en state_data
// -- por eso setResult() tiene que llamarse ANTES de _stop(), asi
// onResult() dispara el envio de result_data mientras el dato todavia es
// valido.
inline void Engine::_finish(const char* reason, const char* description) {
  _runtimeState.setResult(reason, description);
  _stop();
  _reset();
}

inline void Engine::_sendState() {
  JsonDocument doc;
  doc["status"] = _engineState.getState();
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

// _readRule/_sendConfirmSourceConfig/_sendFlagsData (y su unico caller,
// Tasks::SendFlags en onTimer) forman un cluster INACTIVO: el dump
// periodico completo de todas las sources fue reemplazado por el push
// individual por evento via Detector::onFlag/flag_data (ver CLAUDE.md).
// Los cuerpos comentados no son un olvido -- Tasks::SendFlags tampoco se
// llega a agregar nunca en onStart() (esta comentado ahi tambien), asi que
// todo el cluster esta deliberadamente apagado, no roto a medias.
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
  _serial.sendCommand(Commands::CurrentData, doc);
}

// Engine State Callbacks
inline void Engine::onReady() {
  DEBUG_PRINTLN(DEBUG_ENGINE, "On Ready!");
  _timer.removeTask(Tasks::SendResult);
  _timer.addTask(Tasks::SendState, Intervals::SendState);
}

inline void Engine::onStart() {
  TargetData target = _runtimeState.target();
  _timer.addTask(Tasks::Finish, target.durationTarget);

  _timer.addTask(Tasks::MeasureTemperature, Intervals::MeasureTemperature);
  _timer.addTask(Tasks::MeasureCurrent, Intervals::MeasureCurrent);
  _timer.addTask(Tasks::MeasureMagneticField, Intervals::MeasureMagneticField);
  //_timer.addTask(Tasks::UpdateProgress, Intervals::UpdateProgress);
  //_timer.addTask(Tasks::SendFlags, Intervals::SendFlags);
  // Critico: es la unica tarea que apaga _isSettlingTime (ver su
  // comentario en la declaracion de la clase). Si esta linea se borra o se
  // comenta, el Detector nunca vuelve a evaluar nada en todo el experimento.
  _timer.addTask(Tasks::SettlingTime, Intervals::SettlingTime);
}

inline void Engine::onFinish() {
  _timer.removeTask(Tasks::Finish);
  _timer.removeTask(Tasks::MeasureTemperature);
  _timer.removeTask(Tasks::MeasureCurrent);
  _timer.removeTask(Tasks::MeasureMagneticField);
  _timer.removeTask(Tasks::UpdateProgress);
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

// Timer Callback
inline void Engine::onTimer(const char* name) {
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
    DEBUG_PRINT(DEBUG_ENGINE, F("[TEMP] JSON: "));
    serializeJson(doc, Serial);
    DEBUG_PRINTLN(DEBUG_ENGINE, );
  }
  else if(strcmp(name, Tasks::MeasureCurrent) == 0) {
    _currentSensorsManager.updateAll();
    JsonDocument doc;
    _currentSensorsManager.publishMeasures(doc);
    DEBUG_PRINT(DEBUG_ENGINE, F("[CURRENT] JSON: "));
    serializeJson(doc, Serial);
    DEBUG_PRINTLN(DEBUG_ENGINE, );
  }
  else if(strcmp(name, Tasks::MeasureMagneticField) == 0) {
    _magnetometerManager.updateAll();
  }
  else if(strcmp(name, Tasks::UpdateProgress) == 0) {
    _runtimeState.updateProgress();
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
    _finish("completed", "Duracion completa alcanzada");
  }
}

// Serial Callbacks
inline void Engine::onCommand(const char* command, JsonVariantConst params) {

  if (strcmp(command, Commands::Ping) == 0) {

    if (!params["value"].is<long>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[PING][ERROR] Parametro 'value' invalido o ausente"));
      return;
    }

    long value = params["value"].as<long>();
    _serial.sendPong(value);
  }

  else if (strcmp(command, Commands::Pong) == 0) {

    if (!params["value"].is<long>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[PONG][ERROR] Parametro 'value' invalido o ausente"));
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

    DEBUG_PRINTLN(DEBUG_ENGINE, );
    DEBUG_PRINTLN(DEBUG_ENGINE, F("========================================"));
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Comando START recibido"));
    DEBUG_PRINTLN(DEBUG_ENGINE, F("========================================"));

    // ========================================================
    // CONFIGURANDO TARGETS
    // ========================================================

    DEBUG_PRINTLN(DEBUG_ENGINE, F("Configurando targets..."));

    // --- CEM ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'cem'..."));

    if (!params["cem"].is<float>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'cem' invalido o ausente"));
      return;
    }

    float cemTarget = params["cem"].as<float>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] cemTarget = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, cemTarget, 4);


    // --- FRECUENCIA ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'freq'..."));

    if (!params["freq"].is<int>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'freq' invalido o ausente"));
      return;
    }

    int freqTarget = params["freq"].as<int>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] freqTarget = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, freqTarget);


    // --- DURACION ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'dur'..."));

    if (!params["dur"].is<unsigned long>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'dur' invalido o ausente"));
      return;
    }

    unsigned long durTarget = params["dur"].as<unsigned long>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] durTarget = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, durTarget);


    // --- GUARDAR TARGETS ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Guardando targets en RuntimeState..."));

    _runtimeState.setTarget(
      cemTarget,
      freqTarget,
      durTarget
    );

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Targets configuradas correctamente"));


    // ========================================================
    // MODO DE EXPERIMENTO (campo X / campo nulo)
    // ========================================================

    // Parametro OPCIONAL: si la ESP32 no lo manda, se asume campo X. Es la
    // unica forma de que una version vieja de la pantalla siga arrancando
    // experimentos sin romper. El default es X (y no Null) a proposito: si
    // el modo se pierde en el camino, el experimento corre con campo, que
    // es la condicion visible y verificable; un campo nulo silencioso
    // pareceria un experimento normal sin serlo.
    FieldMode mode = FieldMode::X;
    if (params["mode"].is<const char*>()) {
      const char* modeParam = params["mode"].as<const char*>();
      if (strcmp(modeParam, "null") == 0) {
        mode = FieldMode::Null;
      }
      DEBUG_PRINT(DEBUG_ENGINE, F("[START] mode recibido = "));
      DEBUG_PRINTLN(DEBUG_ENGINE, modeParam);
    } else {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] sin parametro 'mode', se asume campo X"));
    }

    // setMode() se rechaza si ya hay un experimento corriendo, pero acá
    // todavia no arranco: _start() va al final de este handler.
    _coilExcitation.setMode(mode);


    // ========================================================
    // TEST MODE (bring-up temporal INT-001, ver tarjeta Trello
    // "Prueba de integracion -- Control de intensidad CEM" -- quitar este
    // bloque y Engine::_applyTestMode() una vez terminadas las 6 pruebas
    // de laboratorio)
    // ========================================================

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'testMode'..."));

    if (!params["testMode"].is<int>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'testMode' invalido o ausente"));
      return;
    }

    uint8_t testMode = params["testMode"].as<int>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] testMode = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, testMode);

    // Aplicado ACA, antes de configurar CEM1: _applyTestMode() decide si la
    // source CEM1 existe en el Detector (removeSource + addSource
    // condicional, ver su implementacion). Si se aplicara mas tarde (como
    // antes, ver bug corregido 2026-09-01 mas abajo), configureSource("CEM1",
    // ...) de mas abajo corre contra una source que todavia no existe (o que
    // _applyTestMode va a recrear vacia despues), y la config se pierde en
    // silencio -- Source::addSample() no hace nada si _configured es false,
    // asi que el Detector nunca evalua CEM1 sin importar los rangos.
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Aplicando testMode..."));

    _applyTestMode(testMode);


    // ========================================================
    // CONFIGURACION SOURCE CEM
    // ========================================================

    DEBUG_PRINTLN(DEBUG_ENGINE, );
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Configurando source CEM1..."));

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'tol'..."));

    if (!params["tol"].is<int>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'tol' invalido o ausente"));
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Abortando comando START"));
      return;
    }

    int cemTol = params["tol"].as<int>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] cemTol = "));
    DEBUG_PRINT(DEBUG_ENGINE, cemTol);
    DEBUG_PRINTLN(DEBUG_ENGINE, F("%"));


    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Calculando configuracion de CEM1..."));

    SourceConfig configCem = DetectorConfigBuilder::buildCemConfig(cemTarget, cemTol);

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] CEM criticalMin = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, configCem.criticalMin, 4);

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] CEM normalMin   = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, configCem.normalMin, 4);

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] CEM normalMax   = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, configCem.normalMax, 4);

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] CEM criticalMax = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, configCem.criticalMax, 4);


    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Aplicando configuracion a CEM1..."));

    _detector.configureSource("CEM1", configCem);

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Source CEM1 configurada correctamente"));


    // ========================================================
    // CONFIGURACION SOURCE TEMP
    // ========================================================

    DEBUG_PRINTLN(DEBUG_ENGINE, );
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Configurando source TEMP1..."));


    // --- TEMP NORMAL MIN ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'tnmin'..."));

    if (!params["tnmin"].is<float>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'tnmin' invalido o ausente"));
      return;
    }

    float tempNormalMin = params["tnmin"].as<float>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] TEMP normalMin = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, tempNormalMin, 2);


    // --- TEMP NORMAL MAX ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'tnmax'..."));

    if (!params["tnmax"].is<float>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'tnmax' invalido o ausente"));
      return;
    }

    float tempNormalMax = params["tnmax"].as<float>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] TEMP normalMax = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, tempNormalMax, 2);


    // --- TEMP CRITICAL MIN ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'tcmin'..."));

    if (!params["tcmin"].is<float>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'tcmin' invalido o ausente"));
      return;
    }

    float tempCriticalMin = params["tcmin"].as<float>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] TEMP criticalMin = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, tempCriticalMin, 2);


    // --- TEMP CRITICAL MAX ---
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Validando parametro 'tcmax'..."));

    if (!params["tcmax"].is<float>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[START][ERROR] Parametro 'tcmax' invalido o ausente"));
      return;
    }

    float tempCriticalMax = params["tcmax"].as<float>();

    DEBUG_PRINT(DEBUG_ENGINE, F("[START] TEMP criticalMax = "));
    DEBUG_PRINTLN(DEBUG_ENGINE, tempCriticalMax, 2);


    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Calculando configuracion de TEMP1..."));

    SourceConfig configTemp = DetectorConfigBuilder::buildTempConfig(
      tempNormalMin, tempNormalMax, tempCriticalMin, tempCriticalMax);

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Aplicando configuracion a TEMP1..."));

    _detector.configureSource("TEMP1", configTemp);

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Source TEMP1 configurada correctamente"));


    // ========================================================
    // ACK
    // ========================================================

    DEBUG_PRINTLN(DEBUG_ENGINE, );
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Preparando ACK..."));

    JsonDocument doc;

    doc["command"] = Commands::Start;

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Enviando ACK..."));

    _serial.sendCommand(
      Commands::Ack,
      doc
    );

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] ACK enviado correctamente"));


    // ========================================================
    // INICIO
    // ========================================================

    DEBUG_PRINTLN(DEBUG_ENGINE, );
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Iniciando experimento..."));

    _start();

    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] _start() finalizado"));

    DEBUG_PRINTLN(DEBUG_ENGINE, F("========================================"));
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Comando START procesado"));
    DEBUG_PRINTLN(DEBUG_ENGINE, F("========================================"));
    DEBUG_PRINTLN(DEBUG_ENGINE, );
  }

  else if (strcmp(command, Commands::Stop) == 0) {

    _finish("stopped", "Detenido manualmente por el operador");

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

inline void Engine::onEmergencyButtonPressed() {
  DEBUG_PRINTLN(DEBUG_ENGINE, F("[EMERGENCY] Pulsador de parada de emergencia presionado"));

  // Si no hay experimento corriendo, no hay nada que detener -- evita
  // mandarle a la ESP32 un result_data "stopped" para un experimento que
  // nunca arranco (la llevaria a la pantalla Resultado sin motivo).
  if (strcmp(_engineState.getState(), State::Running) != 0) {
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[EMERGENCY] Ignorado: no hay experimento en curso"));
    return;
  }

  _finish("stopped", "Parada de emergencia fisica activada por el operador");
}

// Sample Sensor Callbacks
inline void Engine::onMagnetometerSample(IMagnetometer* magnetometer) {
  // Bring-up INT-001: ver _applyTestMode()/_controlLoopEnabled.
  if (_controlLoopEnabled) {
    _fieldController.update(magnetometer->getMagneticField());
    float duty = _fieldController.getOutput();
    _pwmDriver.write(duty);

    DEBUG_PRINT(DEBUG_ENGINE, F("[TESTMODE][PWM] "));
    DEBUG_PRINT(DEBUG_ENGINE, magnetometer->getName());
    DEBUG_PRINT(DEBUG_ENGINE, F("="));
    DEBUG_PRINT(DEBUG_ENGINE, magnetometer->getMagneticField(), 4);
    DEBUG_PRINT(DEBUG_ENGINE, F(" duty="));
    DEBUG_PRINTLN(DEBUG_ENGINE, duty, 4);
  }

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
  DEBUG_PRINT(DEBUG_ENGINE, F("[CURRENT] sample "));
  DEBUG_PRINT(DEBUG_ENGINE, sensor->getName());
  DEBUG_PRINT(DEBUG_ENGINE, F(" = "));
  DEBUG_PRINT(DEBUG_ENGINE, sensor->getCurrent());
  DEBUG_PRINTLN(DEBUG_ENGINE, F(" A"));

  // addSample() aca es un no-op en la practica: "SCT013-1" nunca se
  // registra como source del Detector (detector.addSource en el .ino) --
  // por diseno, la corriente no es una fuente de seguridad, solo se
  // monitorea/telemetriza. Ver MOD-006 en Trello.
  if(!_isSettlingTime) {
    _detector.addSample(sensor->getName(), sensor->getCurrent());
  }

  unsigned long now = millis();
  if(now - _lastCurrentSampleSent >= _currentSampleSendInterval) {
    _lastCurrentSampleSent = now;
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

  const char* typeStr = "unknown";
  if(event.type == EventType::Critical) {
    typeStr = "critical";
  }
  else if(event.type == EventType::Streak) {
    typeStr = "streak";
  }
  else if(event.type == EventType::Frequency) {
    typeStr = "frequency";
  }
  sourceData["type"] = typeStr;

  DEBUG_PRINT(DEBUG_ENGINE, F("[FLAG] "));
  DEBUG_PRINT(DEBUG_ENGINE, source->getName());
  DEBUG_PRINT(DEBUG_ENGINE, F(" tipo="));
  DEBUG_PRINT(DEBUG_ENGINE, typeStr);
  DEBUG_PRINT(DEBUG_ENGINE, F(" count="));
  DEBUG_PRINT(DEBUG_ENGINE, event.count);
  DEBUG_PRINT(DEBUG_ENGINE, F("/"));
  DEBUG_PRINTLN(DEBUG_ENGINE, event.limit);

  _serial.sendCommand(Commands::OneFlagsData, sourceData);

  // Corta el experimento cuando CUALQUIERA de los 3 tipos de regla llega a
  // su propio maxEvents -- no es exclusivo del tipo "critical". Una racha
  // (streak) o una inestabilidad sostenida (frequency) que se repitan
  // suficientes veces cortan igual, aunque la muestra individual nunca haya
  // sido "critica".
  if(event.limit > 0 && event.count >= event.limit) {
    DEBUG_PRINT(DEBUG_ENGINE, F("[FLAG] limite alcanzado para "));
    DEBUG_PRINT(DEBUG_ENGINE, source->getName());
    DEBUG_PRINTLN(DEBUG_ENGINE, F(" -> interrumpiendo experimento"));

    char description[64];
    snprintf(
        description, sizeof(description),
        "%s: limite de alertas %s alcanzado (%u/%u)",
        source->getName(), typeStr, event.count, event.limit
    );
    _finish("critical", description);
  }
}
