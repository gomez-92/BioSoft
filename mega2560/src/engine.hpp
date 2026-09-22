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
#include <Wire.h>
#include "currentmanager.hpp"
#include "currentsensorsct013.hpp"
#include "coilexcitation.hpp"
#include "pwmdriver.hpp"
#include "coilchannel.hpp"
#include "fieldcontroller.hpp"
#include "mainpowerswitch.hpp"
#include "detector.hpp"
#include "emergencybutton.hpp"
#include "safetymargins.hpp"
#include "detectorconfigbuilder.hpp"
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
    CoilExcitation& _coilExcitation;
    CoilChannels& _coilChannels;
    FieldController& _fieldController;
    MainPowerSwitch& _mainPowerSwitch;
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

    // Referencias a AMBOS magnetometros: cual alimenta a CEM1 lo elige la
    // clave `sensor` de detector.sources (archivo de la SD), aplicada en
    // _applySourceSettings(). Antes lo decidia el combo temporal de
    // testMode, retirado al implementar esa seccion del esquema.
    IMagnetometer& _simMagnetometer;
    IMagnetometer& _realMagnetometer;
    // true si el lazo FieldController->CoilChannels debe actuar sobre el
    // PWM. En false el sistema mide y reporta pero no excita las bobinas
    // ("solo sensado", lo que antes eran los testMode 1/4). Lo pisa la
    // clave `enabled` de la seccion `control` via config_control; el
    // default compilado es true para que sin tarjeta SD el equipo corra un
    // experimento completo, como antes del bring-up.
    bool _controlLoopEnabled = true;

    // Plantillas de configuracion del Detector, una por fuente. Traen el
    // bufferSize y las 3 reglas: arrancan en los defaults compilados y las
    // pisa el archivo de la SD (config_source/config_rule). Los RANGOS que
    // tambien viven en SourceConfig no se usan acá -- los escribe cada
    // start, a partir de lo que eligio el operador (ver el handler de
    // Start), sobre una COPIA de la plantilla, para que las reglas
    // configuradas sobrevivan de un experimento al siguiente.
    struct SourceSettings {
      const char* name;
      bool enabled;
      bool useRealSensor;        // solo CEM1
      float criticalMultiplier;  // solo CEM1 (sus rangos son derivados)
      SourceConfig config;
    };
    static constexpr uint8_t SourceCount = 2;
    SourceSettings _sourceSettings[SourceCount];

    // Plantillas de los sensores de corriente. Los objetos viven en el .ino
    // (estaticos, uno por entrada fisica posible) y llegan por referencia;
    // acá solo se guarda si estan habilitados y con que parametros, para
    // aplicarlos al arrancar. Se matchean con lo que trae el archivo por
    // (address, channel), que es la identidad fisica de la entrada: un
    // objeto estatico esta ligado de por vida a su ADS.
    struct CurrentSensorSettings {
      CurrentSensorSct013* sensor;
      uint8_t address;
      bool enabled;
      CurrentSensorConfig config;
    };
    static constexpr uint8_t CurrentSensorCount = 4;
    CurrentSensorSettings _currentSensorSettings[CurrentSensorCount];

    unsigned long _temperatureSampleSendInterval = 3000;
    unsigned long _magnetometerSampleSendInterval = 3000;

    unsigned long _lastTemperatureSampleSent;
    unsigned long _lastMagnetometerSampleSent;

  public:
    Engine( 
      Timer& timer, 
      SerialLink& serial,
      MagnetometerManager& magnetometerManager, 
      ThermometerManager& thermometerManager,
      CurrentSensorsManager& currentSensorsManager, 
      CoilExcitation& coilExcitation,
      CoilChannels& coilChannels,
      FieldController& fieldController,
      MainPowerSwitch& mainPowerSwitch,
      Detector& detector,
      EmergencyButton& emergencyButton,
      IMagnetometer& simMagnetometer,
      IMagnetometer& realMagnetometer
    );

    void begin();
    void update();

    // Registra un slot de sensor de corriente. Los objetos son estaticos y
    // viven en el .ino (uno por entrada fisica posible, ligado de por vida a
    // su ADS); acá solo se guarda la referencia para poder habilitarlos y
    // parametrizarlos desde el archivo de la SD. Se llama en setup().
    bool registerCurrentSensor(CurrentSensorSct013* sensor, uint8_t address, uint8_t channel);
    // Default compilado de un slot, para el `.ino`: que canales estan
    // habilitados si no hay tarjeta SD o si el archivo no trae la seccion.
    bool enableCurrentSensor(uint8_t address, uint8_t channel, bool enabled);

  private:
    void _start();
    void _stop();
    void _reset();
    void _finish(const char* reason, const char* description);
    void _finish(const ResultData& result);
    // Aplica al Detector/magnetometros lo que trajo detector.sources. Se
    // llama al arrancar un experimento, NO al recibir la configuracion: ver
    // el comentario de su implementacion.
    void _applySourceSettings();
    Engine::SourceSettings* _findSourceSettings(const char* name);
    // Registra los sensores de corriente habilitados, salteando los de un
    // modulo que no conteste en el bus. Ver su implementacion.
    void _applyCurrentSensorSettings();
    Engine::CurrentSensorSettings* _findCurrentSensorSettings(uint8_t address, uint8_t channel);
    static bool _i2cDeviceResponds(uint8_t address);

    void _sendState();
    //void _sendTargetData();
    //void _sendProgressData();
    void _sendResultData();

    //void _setTarget(JsonVariantConst params);
    void _readRule(JsonVariantConst json, Rule& rule);
    void _sendConfirmSourceConfig(const char* sourceName);

    void _sendFlagsData();
    void _sendTempData();
    void _sendCemData();
    void _sendCoilData();

    // Aplica config_intervals/config_control/config_coil (docs/config-schema.md
    // seccion 10) -- cada clave se valida independiente, una invalida o
    // ausente deja el default/valor actual sin bloquear el resto del frame.
    void _applyIntervalParam(JsonVariantConst params, const char* key, unsigned long& target);

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
  CoilExcitation& coilExcitation,
  CoilChannels& coilChannels,
  FieldController& fieldController,
  MainPowerSwitch& mainPowerSwitch,
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
  _coilChannels(coilChannels),
  _fieldController(fieldController),
  _mainPowerSwitch(mainPowerSwitch),
  _detector(detector),
  _emergencyButton(emergencyButton),
  _isSettlingTime(false),
  _simMagnetometer(simMagnetometer),
  _realMagnetometer(realMagnetometer),
  _lastTemperatureSampleSent(0),
  _lastMagnetometerSampleSent(0)
{
  // Defaults compilados de las dos fuentes (docs/config-schema.md seccion
  // 12): TEMP1 vigilada, CEM1 no. CEM1 arranca deshabilitada porque su
  // entrada analogica puede no estar cableada, y una A0 flotante dispara
  // flags falsos apenas termina el settling time. El archivo de la SD la
  // habilita cuando el hardware esta listo, sin recompilar.
  _sourceSettings[0].name = "CEM1";
  _sourceSettings[0].enabled = false;
  _sourceSettings[0].useRealSensor = false;
  _sourceSettings[0].criticalMultiplier = SafetyMargins::CemCriticalMultiplier;
  _sourceSettings[0].config = DetectorConfigBuilder::defaultConfig();

  _sourceSettings[1].name = "TEMP1";
  _sourceSettings[1].enabled = true;
  _sourceSettings[1].useRealSensor = false;
  _sourceSettings[1].criticalMultiplier = SafetyMargins::CemCriticalMultiplier;
  _sourceSettings[1].config = DetectorConfigBuilder::defaultConfig();

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

  _mainPowerSwitch.begin();
  _coilChannels.beginAll();
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
  // Con el lazo deshabilitado (control.enabled == false, "solo sensado")
  // el PWM se deja apagado -- ver _controlLoopEnabled.
  if (_controlLoopEnabled) {
    _fieldController.setSetpoint(target.cemTarget);
    _coilChannels.writeAll(_fieldController.getOutput());
    _coilChannels.enableAll();
  }
  _mainPowerSwitch.enable();
  _engineState.setState(State::Running);
}

inline void Engine::_stop() {
  _mainPowerSwitch.disable();
  _coilExcitation.stop();
  _coilChannels.disableAll();
  _fieldController.reset();
  _engineState.setState(State::Finished);
}

inline void Engine::_reset() {
  _mainPowerSwitch.disable();
  _coilExcitation.stop();
  _coilChannels.disableAll();
  _fieldController.reset();
  _detector.reset();
  _engineState.setState(State::Ready);
  _runtimeState.reset();
}

// Bring-up temporal INT-001: ver comentario de _controlLoopEnabled en la
// declaracion de la clase y testmoderesolver.hpp para la tabla completa.
inline bool Engine::registerCurrentSensor(CurrentSensorSct013* sensor, uint8_t address, uint8_t channel) {
  if (sensor == nullptr) return false;

  for (uint8_t i = 0; i < CurrentSensorCount; i++) {
    if (_currentSensorSettings[i].sensor != nullptr) continue;

    _currentSensorSettings[i].sensor = sensor;
    _currentSensorSettings[i].address = address;
    _currentSensorSettings[i].enabled = false;
    // La plantilla arranca con lo que el .ino le puso al objeto: son los
    // defaults compilados, y config_current solo pisa las claves que trae.
    _currentSensorSettings[i].config = sensor->getConfig();
    _currentSensorSettings[i].config.address = address;
    _currentSensorSettings[i].config.channel = channel;
    return true;
  }
  return false;
}

inline bool Engine::enableCurrentSensor(uint8_t address, uint8_t channel, bool enabled) {
  CurrentSensorSettings* settings = _findCurrentSensorSettings(address, channel);
  if (settings == nullptr) return false;

  settings->enabled = enabled;
  return true;
}

inline Engine::CurrentSensorSettings* Engine::_findCurrentSensorSettings(uint8_t address, uint8_t channel) {
  for (uint8_t i = 0; i < CurrentSensorCount; i++) {
    CurrentSensorSettings& settings = _currentSensorSettings[i];
    if (settings.sensor == nullptr) continue;
    if (settings.address == address && settings.config.channel == channel) {
      return &settings;
    }
  }
  return nullptr;
}

// Registra en el manager los sensores habilitados. El probe del bus no esta
// acá sino en CurrentSensorSct013::begin() (que addCurrentSensor() invoca):
// asi la proteccion contra el cuelgue por I2C viaja con el driver y vale
// tambien para cualquier otro registro, no solo para este camino.
inline void Engine::_applyCurrentSensorSettings() {
  _currentSensorsManager.clearCurrentSensors();

  for (uint8_t i = 0; i < CurrentSensorCount; i++) {
    CurrentSensorSettings& settings = _currentSensorSettings[i];
    if (settings.sensor == nullptr) continue;

    if (!settings.enabled) {
      DEBUG_PRINT(DEBUG_ENGINE, F("[CURRENT] slot 0x"));
      DEBUG_PRINT(DEBUG_ENGINE, settings.address, HEX);
      DEBUG_PRINT(DEBUG_ENGINE, F("/"));
      DEBUG_PRINT(DEBUG_ENGINE, settings.config.channel);
      DEBUG_PRINTLN(DEBUG_ENGINE, F(": deshabilitado"));
      continue;
    }

    settings.sensor->setConfig(settings.config);
    _currentSensorsManager.addCurrentSensor(settings.sensor);
  }
}

inline Engine::SourceSettings* Engine::_findSourceSettings(const char* name) {
  if (name == nullptr) return nullptr;
  for (uint8_t i = 0; i < SourceCount; i++) {
    if (strcmp(_sourceSettings[i].name, name) == 0) {
      return &_sourceSettings[i];
    }
  }
  return nullptr;
}

// Registra/desregistra cada fuente segun su `enabled` y elige que
// magnetometro alimenta a CEM1 segun su `sensor`.
//
// Se llama al ARRANCAR un experimento, no al recibir config_source. Dos
// motivos:
//  1) removeSource()/addSource() destruyen y recrean el Source, que vuelve
//     sin configurar (Source::addSample es un no-op mientras _configured es
//     false). Aplicarlo al vuelo por una reconexion serie a mitad de
//     experimento dejaria al Detector sin evaluar esa fuente por el resto
//     de la corrida, en silencio y justo cuando mas importa.
//  2) El orden importa: esto tiene que correr ANTES del configureSource()
//     de mas abajo, o la config se aplica sobre una source que todavia no
//     existe (o que esta funcion va a recrear vacia despues).
inline void Engine::_applySourceSettings() {
  _magnetometerManager.clearMagnetometers();

  for (uint8_t i = 0; i < SourceCount; i++) {
    SourceSettings& settings = _sourceSettings[i];

    _detector.removeSource(settings.name);
    if (settings.enabled) {
      _detector.addSource(settings.name);
    }

    DEBUG_PRINT(DEBUG_ENGINE, F("[SOURCES] "));
    DEBUG_PRINT(DEBUG_ENGINE, settings.name);
    DEBUG_PRINTLN(DEBUG_ENGINE, settings.enabled ? F(": detector=on") : F(": detector=off"));
  }

  SourceSettings* cem = _findSourceSettings("CEM1");
  bool useRealSensor = (cem != nullptr) && cem->useRealSensor;
  _magnetometerManager.addMagnetometer(
    useRealSensor ? &_realMagnetometer : &_simMagnetometer
  );

  DEBUG_PRINT(DEBUG_ENGINE, F("[SOURCES] CEM1 sensor="));
  DEBUG_PRINT(DEBUG_ENGINE, useRealSensor ? F("real") : F("sim"));
  DEBUG_PRINTLN(DEBUG_ENGINE, _controlLoopEnabled ? F(" control=on") : F(" control=off"));
}

// Punto unico donde termina un experimento (tiempo cumplido, stop manual o
// corte de seguridad por flag). _reset() borra el resultado de
// RuntimeState (via _runtimeState.reset()) y _stop()/_reset() dejan el
// Engine en Ready antes de que el ESP32 pueda ver "finished" en state_data
// -- por eso setResult() tiene que llamarse ANTES de _stop(), asi
// onResult() dispara el envio de result_data mientras el dato todavia es
// valido.
inline void Engine::_finish(const char* reason, const char* description) {
  ResultData data;
  strncpy(data.reason, reason, sizeof(data.reason) - 1);
  data.reason[sizeof(data.reason) - 1] = '\0';
  strncpy(data.description, description, sizeof(data.description) - 1);
  data.description[sizeof(data.description) - 1] = '\0';
  _finish(data);
}

// Sigue siendo el UNICO lugar donde termina un experimento: las tres
// variantes de arriba arman su ResultData y entran por aca.
inline void Engine::_finish(const ResultData& result) {
  _runtimeState.setResult(result);
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

  // Los campos crudos solo viajan cuando aplican: el frame tiene 256 bytes
  // y mandarlos siempre sumaria claves vacias en el caso mas largo (ver
  // test_resultdata_fits). El ESP32 los usa para redactar el texto de la
  // pantalla Resultado y cae en `description` si no vinieron.
  if (strcmp(data.reason, "critical") == 0) {
    doc["source"] = data.source;
    doc["type"] = data.type;
    doc["count"] = data.count;
    doc["limit"] = data.limit;
  }
  else if (strcmp(data.reason, "stopped") == 0) {
    doc["emerg"] = data.fromEmergency;
  }

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
inline void Engine::_applyIntervalParam(JsonVariantConst params, const char* key, unsigned long& target) {
  if (!params[key].is<unsigned long>()) return;

  unsigned long value = params[key].as<unsigned long>();
  if (value == 0) {
    DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_INTERVALS] Valor invalido para "));
    DEBUG_PRINTLN(DEBUG_ENGINE, key);
    return;
  }
  target = value;
}

inline void Engine::_readRule(JsonVariantConst json, Rule& rule) {
  if (json.isNull())  return;
  if (!json.is<JsonObjectConst>()) return;
  rule.cooldown = json["cooldown"] | rule.cooldown;
  rule.maxEvents = json["maxEvents"] | rule.maxEvents;
  rule.threshold = json["threshold"] | rule.threshold;
}

// _sendConfirmSourceConfig/_sendFlagsData (y su unico caller,
// Tasks::SendFlags en onTimer) forman un cluster INACTIVO: el dump
// periodico completo de todas las sources fue reemplazado por el push
// individual por evento via Detector::onFlag/flag_data (ver CLAUDE.md).
// Los cuerpos comentados no son un olvido -- Tasks::SendFlags tampoco se
// llega a agregar nunca en onStart() (esta comentado ahi tambien), asi que
// todo el cluster esta deliberadamente apagado, no roto a medias.
// _readRule() SI es codigo vivo desde que existe config_rule (lo usa ese
// handler para parsear {threshold, cooldown, maxEvents}); ya no forma parte
// de este cluster.
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

// Una tanda por bobina REGISTRADA: duty aplicado y corriente del sensor
// homonimo (la bobina N lleva el SCT013-N). Las claves son "dN"/"cN" y no
// los nombres de los sensores porque el frame tiene 256 bytes contados: con
// 4 bobinas, "SCT013-1".."SCT013-4" mas las de duty no entrarian, y un
// serializeJson que no entra TRUNCA EN SILENCIO con CRC valido (ver
// SerialLink::_sendFrame). test_coildata_fits lo fija.
//
// Cuantas claves salen es tambien el dato que dice cuantas bobinas hay: el
// ESP32 dibuja una fila por bobina que aparezca aca cuando no tiene la
// seccion coils de la SD.
inline void Engine::_sendCoilData() {
  JsonDocument doc;

  uint8_t count = _coilChannels.count();
  for (uint8_t i = 0; i < count; i++) {
    CoilChannel* channel = _coilChannels.getChannel(i);
    if (channel == nullptr) continue;

    char key[4];
    snprintf(key, sizeof(key), "d%u", i + 1);
    // Duty en POR CIENTO y con un decimal: la pantalla lo muestra asi, y
    // redondear aca acota cuanto ocupa cada valor en el frame (un float sin
    // redondear se serializa con hasta 9 cifras significativas).
    doc[key] = roundf(channel->appliedDuty() * 1000.0f) / 10.0f;

    char sensorName[12];
    snprintf(sensorName, sizeof(sensorName), "SCT013-%u", i + 1);
    ICurrentSensor* sensor = _currentSensorsManager.findByName(sensorName);
    if (sensor == nullptr) continue;

    snprintf(key, sizeof(key), "c%u", i + 1);
    doc[key] = roundf(sensor->getCurrent() * 100.0f) / 100.0f;
  }

  _serial.sendCommand(Commands::CoilData, doc);
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

    // coil_data sale de ACA y no del callback del sensor de corriente: el
    // frame lleva tambien el duty, que no depende de que haya un SCT013.
    // Colgado del callback, un banco sin ADS1115 cableado (el caso normal
    // de bring-up) no emitia ni un solo frame, y la pantalla En curso se
    // quedaba sin duty y -- sin tarjeta SD -- sin ninguna fila de bobina,
    // sin nada en el log que lo explicara.
    _sendCoilData();
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
    // FUENTES DEL DETECTOR
    // ========================================================

    // Aplicado ACA, antes de los configureSource() de mas abajo:
    // _applySourceSettings() registra/desregistra cada fuente segun lo que
    // trajo el archivo de la SD, y una source recien creada vuelve sin
    // configurar. Si corriera despues, la config se perderia en silencio
    // (Source::addSample() no hace nada si _configured es false, asi que el
    // Detector nunca evaluaria esa fuente sin importar los rangos).
    DEBUG_PRINTLN(DEBUG_ENGINE, F("[START] Aplicando configuracion de fuentes..."));

    _applySourceSettings();
    _applyCurrentSensorSettings();


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

    // Copia de la plantilla (bufferSize + reglas, ya sea los defaults
    // compilados o lo que pisó la SD) mas los rangos derivados de lo que
    // eligio el operador. Se trabaja sobre una copia para que la plantilla
    // siga intacta para el proximo experimento.
    SourceSettings* cemSettings = _findSourceSettings("CEM1");
    SourceConfig configCem = cemSettings->config;
    DetectorConfigBuilder::applyCemRanges(
      configCem, cemTarget, cemTol, cemSettings->criticalMultiplier);

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

    SourceConfig configTemp = _findSourceSettings("TEMP1")->config;
    DetectorConfigBuilder::applyTempRanges(
      configTemp, tempNormalMin, tempNormalMax, tempCriticalMin, tempCriticalMax);

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

  // config_intervals/config_control/config_coil: frames fragmentados que la
  // ESP32 manda al reconectar con las secciones intervals.mega/control/coils
  // del archivo de la SD (docs/config-schema.md seccion 10). El ack confirma
  // que el frame llego y se pudo parsear, no que cada clave era valida --
  // una clave invalida se loguea y esa clave particular queda en su valor
  // actual, sin bloquear el ack ni las demas claves del mismo frame.

  else if (strcmp(command, Commands::ConfigIntervals) == 0) {

    _applyIntervalParam(params, "ping", Intervals::Ping);
    _applyIntervalParam(params, "sendState", Intervals::SendState);
    _applyIntervalParam(params, "measureTemperature", Intervals::MeasureTemperature);
    _applyIntervalParam(params, "measureCurrent", Intervals::MeasureCurrent);
    _applyIntervalParam(params, "measureMagneticField", Intervals::MeasureMagneticField);
    _applyIntervalParam(params, "updateProgress", Intervals::UpdateProgress);
    _applyIntervalParam(params, "sendFlags", Intervals::SendFlags);
    _applyIntervalParam(params, "sendResult", Intervals::SendResult);
    _applyIntervalParam(params, "settlingTime", Intervals::SettlingTime);

    JsonDocument doc;
    doc["command"] = Commands::ConfigIntervals;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  else if (strcmp(command, Commands::ConfigControl) == 0) {

    if (params["kp"].is<float>() && !_fieldController.setKp(params["kp"].as<float>())) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_CONTROL] kp invalido -- se ignora"));
    }
    if (params["maxStep"].is<float>() && !_fieldController.setMaxStep(params["maxStep"].as<float>())) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_CONTROL] maxStep invalido -- se ignora"));
    }
    if (params["deadBand"].is<float>() && !_fieldController.setDeadBand(params["deadBand"].as<float>())) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_CONTROL] deadBand invalido -- se ignora"));
    }
    // En false el lazo mide pero no toca el PWM (lo que antes eran los
    // testMode 1/4, "solo sensado").
    if (params["enabled"].is<bool>()) {
      _controlLoopEnabled = params["enabled"].as<bool>();
      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_CONTROL] lazo de intensidad="));
      DEBUG_PRINTLN(DEBUG_ENGINE, _controlLoopEnabled ? F("on") : F("off"));
    }

    JsonDocument doc;
    doc["command"] = Commands::ConfigControl;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  else if (strcmp(command, Commands::ConfigCoil) == 0) {

    const char* coilName = params["name"];
    if (coilName == nullptr) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_COIL][ERROR] Parametro 'name' ausente -- sin ack"));
      return;
    }

    CoilChannel* channel = _coilChannels.findByName(coilName);
    if (channel == nullptr) {
      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_COIL] Canal desconocido: "));
      DEBUG_PRINTLN(DEBUG_ENGINE, coilName);
    } else {
      if (params["enabled"].is<bool>()) {
        if (params["enabled"].as<bool>()) channel->enable();
        else channel->disable();
      }
      if (params["calibrationFactor"].is<float>()) {
        channel->setCalibrationFactor(params["calibrationFactor"].as<float>());
      }
    }

    // El frame llego bien aunque el canal no exista -- el ack lleva `name`
    // para que la ESP32 sepa cual de sus hasta 4 config_coil pendientes
    // cancelar (a diferencia de Start/Stop/Reset, acá puede haber varios
    // frames del mismo comando en vuelo a la vez).
    JsonDocument doc;
    doc["command"] = Commands::ConfigCoil;
    doc["name"] = coilName;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  // Cabecera de una fuente del Detector. Las 3 reglas NO vienen acá: no
  // entran en un frame junto con esto (ver test_serialframes), viajan como
  // config_rule por separado.
  //
  // Nada de esto se aplica al Detector en el momento: se guarda en la
  // plantilla de la fuente y lo aplica el proximo start
  // (_applySourceSettings), para no reconfigurar en caliente una fuente de
  // un experimento en curso.
  else if (strcmp(command, Commands::ConfigSource) == 0) {

    const char* sourceName = params["name"];
    SourceSettings* settings = _findSourceSettings(sourceName);
    if (settings == nullptr) {
      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_SOURCE][ERROR] Fuente desconocida o sin 'name': "));
      DEBUG_PRINTLN(DEBUG_ENGINE, sourceName == nullptr ? "(ausente)" : sourceName);
      return;
    }

    if (params["enabled"].is<bool>()) {
      settings->enabled = params["enabled"].as<bool>();
    }

    // Solo CEM1 tiene dos drivers posibles; para TEMP1 la clave se ignora.
    if (params["sensor"].is<const char*>()) {
      settings->useRealSensor = (strcmp(params["sensor"].as<const char*>(), "mlx90393") == 0);
    }

    if (params["bufferSize"].is<size_t>()) {
      size_t bufferSize = params["bufferSize"].as<size_t>();
      if (DetectorConfigBuilder::isValidBufferSize(bufferSize)) {
        settings->config.bufferSize = bufferSize;
      } else {
        DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_SOURCE] bufferSize fuera de rango -- se ignora"));
      }
    }

    if (params["criticalMultiplier"].is<float>()) {
      float multiplier = params["criticalMultiplier"].as<float>();
      if (DetectorConfigBuilder::isValidCriticalMultiplier(multiplier)) {
        settings->criticalMultiplier = multiplier;
      } else {
        DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_SOURCE] criticalMultiplier fuera de rango -- se ignora"));
      }
    }

    JsonDocument doc;
    doc["command"] = Commands::ConfigSource;
    doc["name"] = settings->name;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  // Una regla de una fuente. Se valida ENTERA contra el bufferSize vigente
  // de esa fuente y se descarta completa si algo cae fuera de rango
  // (docs/config-schema.md seccion 7): media regla aplicada cortaria, o
  // dejaria de cortar, un experimento con numeros que nadie eligio.
  else if (strcmp(command, Commands::ConfigRule) == 0) {

    const char* sourceName = params["source"];
    const char* ruleName = params["rule"];
    SourceSettings* settings = _findSourceSettings(sourceName);

    if (settings == nullptr || ruleName == nullptr) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_RULE][ERROR] 'source' o 'rule' ausente o desconocido"));
      return;
    }

    Rule* target = nullptr;
    if (strcmp(ruleName, "critical") == 0)       target = &settings->config.critical;
    else if (strcmp(ruleName, "streak") == 0)    target = &settings->config.streak;
    else if (strcmp(ruleName, "frequency") == 0) target = &settings->config.frequency;

    if (target == nullptr) {
      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_RULE][ERROR] Regla desconocida: "));
      DEBUG_PRINTLN(DEBUG_ENGINE, ruleName);
      return;
    }

    Rule candidate = *target;
    _readRule(params, candidate);

    if (DetectorConfigBuilder::isValidRule(candidate, settings->config.bufferSize)) {
      target->threshold = candidate.threshold;
      target->cooldown = candidate.cooldown;
      target->maxEvents = candidate.maxEvents;

      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_RULE] "));
      DEBUG_PRINT(DEBUG_ENGINE, settings->name);
      DEBUG_PRINT(DEBUG_ENGINE, F("/"));
      DEBUG_PRINT(DEBUG_ENGINE, ruleName);
      DEBUG_PRINTLN(DEBUG_ENGINE, F(" aplicada"));
    } else {
      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_RULE] "));
      DEBUG_PRINT(DEBUG_ENGINE, settings->name);
      DEBUG_PRINT(DEBUG_ENGINE, F("/"));
      DEBUG_PRINT(DEBUG_ENGINE, ruleName);
      DEBUG_PRINTLN(DEBUG_ENGINE, F(" fuera de rango -- se descarta entera"));
    }

    JsonDocument doc;
    doc["command"] = Commands::ConfigRule;
    doc["source"] = settings->name;
    doc["rule"] = ruleName;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  // Un canal de corriente. Se matchea al slot por (address, channel) -- la
  // identidad fisica de la entrada -- porque cada objeto estatico esta ligado
  // de por vida a un ADS concreto y no puede cambiar de modulo. El `name` es
  // solo la etiqueta con la que sale en current_data.
  //
  // Igual que detector.sources, no se aplica al recibirse: registrar o sacar
  // sensores a mitad de experimento cambiaria lo que se esta midiendo en
  // plena corrida.
  else if (strcmp(command, Commands::ConfigCurrent) == 0) {

    if (!params["address"].is<uint8_t>() || !params["channel"].is<uint8_t>()) {
      DEBUG_PRINTLN(DEBUG_ENGINE, F("[CONFIG_CURRENT][ERROR] Falta 'address' o 'channel' -- sin ack"));
      return;
    }

    uint8_t address = params["address"].as<uint8_t>();
    uint8_t channel = params["channel"].as<uint8_t>();
    CurrentSensorSettings* settings = _findCurrentSensorSettings(address, channel);

    if (settings == nullptr) {
      DEBUG_PRINT(DEBUG_ENGINE, F("[CONFIG_CURRENT] Sin slot para 0x"));
      DEBUG_PRINT(DEBUG_ENGINE, address, HEX);
      DEBUG_PRINT(DEBUG_ENGINE, F("/"));
      DEBUG_PRINTLN(DEBUG_ENGINE, channel);
    } else {
      if (params["enabled"].is<bool>()) {
        settings->enabled = params["enabled"].as<bool>();
      }
      if (params["name"].is<const char*>()) {
        settings->config.name = params["name"].as<const char*>();
      }
      if (params["ratedCurrent"].is<float>()) {
        settings->config.ratedCurrent = params["ratedCurrent"].as<float>();
      }
      if (params["ratedVoltage"].is<float>()) {
        settings->config.ratedVoltage = params["ratedVoltage"].as<float>();
      }
      if (params["calibration"].is<float>()) {
        settings->config.calibration = params["calibration"].as<float>();
      }
      if (params["sampleRate"].is<uint16_t>()) {
        settings->config.sampleRate = params["sampleRate"].as<uint16_t>();
      }
      if (params["integrationTimeMs"].is<uint16_t>()) {
        settings->config.integrationTimeMs = params["integrationTimeMs"].as<uint16_t>();
      }

      // setConfig() copia el nombre a un buffer propio del sensor: hay que
      // llamarlo mientras `params` sigue vivo, porque config.name apunta
      // adentro del JsonDocument del frame recibido.
      settings->sensor->setConfig(settings->config);
      settings->config.name = nullptr;
    }

    JsonDocument doc;
    doc["command"] = Commands::ConfigCurrent;
    doc["address"] = address;
    doc["channel"] = channel;

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

  ResultData result;
  strncpy(result.reason, "stopped", sizeof(result.reason) - 1);
  strncpy(result.description, "Parada de emergencia fisica activada por el operador",
          sizeof(result.description) - 1);
  result.fromEmergency = true;
  _finish(result);
}

// Sample Sensor Callbacks
inline void Engine::onMagnetometerSample(IMagnetometer* magnetometer) {
  // Con "solo sensado" se mide y se reporta, pero no se toca el PWM: ver
  // _controlLoopEnabled.
  if (_controlLoopEnabled) {
    _fieldController.update(magnetometer->getMagneticField());
    float duty = _fieldController.getOutput();
    _coilChannels.writeAll(duty);

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

    ResultData result;
    strncpy(result.reason, "critical", sizeof(result.reason) - 1);
    snprintf(
        result.description, sizeof(result.description),
        "%s: limite de alertas %s alcanzado (%u/%u)",
        source->getName(), typeStr, event.count, event.limit
    );
    strncpy(result.source, source->getName(), sizeof(result.source) - 1);
    strncpy(result.type, typeStr, sizeof(result.type) - 1);
    result.count = event.count;
    result.limit = event.limit;
    _finish(result);
  }
}
