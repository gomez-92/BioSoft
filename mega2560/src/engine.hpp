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
#include "controlmap.hpp"
#include "fieldsampler.hpp"
#include "mainpowerswitch.hpp"
#include "detector.hpp"
#include "emergencybutton.hpp"
#include "safetymargins.hpp"
#include "detectorconfigbuilder.hpp"
#include "sensorchoice.hpp"
#include "onewireaddress.hpp"
#include "scenariosensors.hpp"
#include "log.hpp"
#include "diag.hpp"


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

    // Mapa intensidad x frecuencia -> duty y balance (controlmap.hpp), que
    // llega por config_map. Sin tarjeta queda vacio: el campo X parte de duty
    // 0 como siempre y el campo nulo se rechaza.
    ControlMap _controlMap;
    // Tope operativo del |delta| de balance (`control.balanceMax` de la SD).
    float _balanceMax = 0.2f;
    // true en una corrida de campo nulo con el lazo activo: el duty es el del
    // mapa, fijo, y FieldController NO corre (el objetivo es 0, regular
    // hacia arriba no tiene sentido). Lo fija _start().
    bool _nullRun = false;

    // Interruptor general del Detector (`detector.enabled` de la SD, llega
    // en config_detector). Como las fuentes, se guarda al recibirlo y se
    // aplica en el proximo start (_applySourceSettings): apagar el detector
    // a mitad de una corrida porque el ESP32 se reconecto con otra tarjeta
    // dejaria sin vigilancia un experimento que arranco vigilado. Default
    // true: sin tarjeta, el equipo corta como siempre.
    bool _detectorEnabled = true;

    // Los dos termometros que pueden alimentar a TEMP1, segun
    // detector.sources[TEMP1].sensor. Punteros y no referencias porque el
    // DS18B20 se crea con new en setup() y llega por registerThermometers(),
    // despues de construido el Engine. nullptr = no registrado.
    IThermometer* _realThermometer = nullptr;
    IThermometer* _simThermometer = nullptr;
    MagnetometerScenario* _scenarioMagnetometer = nullptr;
    ThermometerScenario* _scenarioThermometer = nullptr;

    // true si CEM1 NO es el sensor real (sim o escenario) en la corrida
    // actual. Lo calcula _applySourceSettings() y lo usa _start(): con un
    // campo inventado, el lazo calcula y reporta el duty pero ni la etapa de
    // potencia ni los canales se habilitan.
    bool _syntheticField = false;

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
      SensorChoice sensor;       // que driver la alimenta (ver sensorchoice.hpp)
      ScenarioSignal scenario;   // la senal si sensor == Scenario (scenario.hpp)
      float criticalMultiplier;  // solo CEM1 (sus rangos son derivados)
      // Solo TEMP1 con el DS18B20: el ROM code que vino de la SD. Sin el,
      // queda el compilado en el .ino (el del sensor original).
      bool hasAddress = false;
      uint8_t address[OneWireAddress::Length] = {0};
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

    // Ultima lectura valida de CEM1, para el corte por magnetometro caido
    // con el lazo activo (_checkControlSilence). Vive aca y no en el
    // Detector porque tiene que cortar aunque CEM1 no este vigilada y aunque
    // el Detector este apagado: es seguridad del control, no deteccion.
    unsigned long _lastMagnetometerAliveMs = 0;
    // Numero de corrida que mando la ESP32 en el start (esp32/src/runcounter.hpp).
    // Se devuelve en state_data mientras corre: si la ESP32 se reinicia a
    // mitad de corrida, lo toma de aca en vez de asignar uno nuevo. 0 = sin
    // corrida (o ESP32 vieja).
    uint32_t _runNumber = 0;
    // Zona de cada medicion (0 normal, 1 advertencia, 2 critica) para
    // loguear por FLANCO cuando sale de normal y cuando vuelve, con el
    // ultimo valor (que tambien acompana a cada alerta). 9 bytes por fuente.
    struct ZoneWatch {
      uint8_t zone = 0;
      unsigned long since = 0;
      float last = NAN;
    };
    ZoneWatch _cemZone;
    ZoneWatch _tempZone;

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
    // Los dos drivers posibles de TEMP1 (DS18B20 y simulado). Se llama en
    // setup(); cual queda registrado lo decide _applySourceSettings().
    void registerThermometers(IThermometer* real, IThermometer* sim);
    // Los drivers de escenario de CEM1 y TEMP1 (scenariosensors.hpp). Se
    // llama en setup(); se usan cuando detector.sources[].sensor es
    // "scenario".
    void registerScenarioSensors(MagnetometerScenario* magnetometer, ThermometerScenario* thermometer);

  private:
    void _refuseStart(const char* cause, const char* description);
    static int _freeRam();
    void _start();
    void _stop();
    void _reset();
    void _finish(const char* reason, const char* description);
    // Termina con el resultado ya escrito en _runtimeState.draftResult().
    void _finish();
    // Aplica al Detector/magnetometros lo que trajo detector.sources. Se
    // llama al arrancar un experimento, NO al recibir la configuracion: ver
    // el comentario de su implementacion.
    void _applySourceSettings();
    Engine::SourceSettings* _findSourceSettings(const char* name);
    // Aplica un float de config_scenario solo si es >= 0; si no, lo deja
    // como estaba y lo dice en el log.
    void _applyNonNegative(JsonVariantConst value, float& target, const char* key);
    // Corte por silencio (tarjeta 25), llamado desde update() mientras hay
    // un experimento en curso.
    void _checkSilence();
    void _checkControlSilence(unsigned long nowMs);
    // Log de mediciones: rangos al arrancar y cambios de zona.
    void _logSourceRanges(const char* name, const char* unit);
    void _watchZone(ZoneWatch& watch, const char* name, float value, const char* unit);
    static ScenarioRanges _scenarioRangesOf(const SourceConfig& config);
    // Duty de partida del lazo en campo X: el del mapa, salvo con la planta
    // del escenario y sin punto mapeado. Ver su implementacion.
    float _scenarioStartDuty(float mappedDuty);
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
  // Default: el MLX90393 real. Hasta la tarjeta 22 era el sim de A0, pero
  // con un campo que no es real las bobinas quedan bloqueadas, y un Mega sin
  // tarjeta SD no podria excitar nunca. sim/scenario son elecciones de
  // banco que la tarjeta tiene que pedir explicitamente.
  _sourceSettings[0].sensor = SensorChoice::Real;
  // Escenario por defecto: "todo normal". Niveles relativos a los rangos de
  // la corrida (scenario.hpp). La planta alcanza el objetivo con duty 0.5,
  // en el medio del rango: el lazo tiene margen para los dos lados.
  _sourceSettings[0].scenario.setpointDuty = 0.5f;
  _sourceSettings[0].scenario.noise = 0.1f;
  _sourceSettings[0].criticalMultiplier = SafetyMargins::CemCriticalMultiplier;
  _sourceSettings[0].config = DetectorConfigBuilder::defaultConfig();

  _sourceSettings[1].name = "TEMP1";
  _sourceSettings[1].enabled = true;
  _sourceSettings[1].sensor = SensorChoice::Real;
  // Centro del rango normal elegido, con un ruido de 0.1 nivel.
  _sourceSettings[1].scenario.noise = 0.1f;
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
  CurrentSensorSct013::abortCheck = [](void* button) {
    return static_cast<EmergencyButton*>(button)->isPressed();
  };
  CurrentSensorSct013::abortContext = &_emergencyButton;
  _timer.start();
  _engineState.setState(State::Ready);
}

// Cada Diag::mark() deja escrito en RAM no inicializada en que paso esta el
// loop: si el watchdog reinicia el Mega, el arranque siguiente lo lee y lo
// informa (diag.hpp). Escribir un byte por paso no cuesta nada.
inline void Engine::update() {
  Diag::mark(Diag::CrumbTimer);
  _timer.tick();
  Diag::mark(Diag::CrumbSerial);
  _serial.update();
  Diag::mark(Diag::CrumbButton);
  _emergencyButton.update();
  // Un paso acotado por sensor asincrono (a lo sumo una transaccion de bus):
  // el RMS del campo se arma de a muestras, sin bloquear nunca el loop (el
  // boton de emergencia se lee en esta misma vuelta).
  Diag::mark(Diag::CrumbMagnetometer);
  _magnetometerManager.pollAll();
  Diag::mark(Diag::CrumbThermometer);
  _thermometerManager.pollAll();
  Diag::mark(Diag::CrumbSilence);
  _checkSilence();
}

// El start ya fue aceptado (ack enviado) pero no se puede arrancar. Se le
// informa a la ESP32 como un experimento terminado antes de empezar
// (result_data con reason "refused" y la causa), que es lo que muestra la
// pantalla Resultado; despues el flujo sigue normal, Volver lleva a
// Principal. No pasa por _finish(): no hay nada que apagar ni un estado
// Finished que transitar, y el Engine nunca dejo de estar en Ready.
// RAM libre entre el tope del heap y el stack. El Mega queda al ~77 %
// estatico y cada trama reserva JsonDocuments en el heap: con menos de
// ~300 bytes ya se colgo (2026-10-03). -1 fuera del AVR (tests nativos).
inline int Engine::_freeRam() {
  return Diag::freeNow();
}

inline void Engine::_refuseStart(const char* cause, const char* description) {
  // El start SI se recibio: se confirma igual, para que la ESP32 corte el
  // reenvio. Sin el ack seguia mandando start cada pocos segundos y cada
  // uno se volvia a rechazar (en banco, 2026-10-03).
  {
    JsonDocument ack;
    ack["command"] = Commands::Start;
    _serial.sendCommand(Commands::Ack, ack);
  }

  LOG_W(CORRIDA, "start rechazado causa=%s: %s", cause, description);
  ResultData& data = _runtimeState.draftResult();
  strncpy(data.reason, "refused", sizeof(data.reason) - 1);
  strncpy(data.cause, cause, sizeof(data.cause) - 1);
  strncpy(data.description, description, sizeof(data.description) - 1);
  // publishResult() dispara onResult() -> _sendResultData() en el acto; el
  // reset posterior deja el resultado vacio para el proximo experimento.
  _runtimeState.publishResult();
  _runtimeState.reset();
}

// Un sensor que se cae no produce muestras, y el Detector solo evalua las
// que llegan: sin esto, un DS18B20 desconectado a mitad de corrida dejaba a
// los animales expuestos sin control de temperatura y la corrida terminaba
// "completed". Se chequea en cada vuelta de loop y no en una tarea del
// Timer porque es barato (una resta por fuente) y no depende de que el
// Timer siga sano.
inline void Engine::_checkSilence() {
  if (strcmp(_engineState.getState(), State::Running) != 0) return;
  unsigned long now = millis();
  _detector.checkSilence(now);
  // checkSilence() puede haber cortado (_finish -> Ready): no evaluar el
  // lazo de una corrida que ya termino.
  if (strcmp(_engineState.getState(), State::Running) != 0) return;
  _checkControlSilence(now);
}

// CEM1 es la realimentacion del lazo: sin lecturas, FieldController se
// queda con el ultimo duty y sigue excitando las bobinas a ciegas. Corta
// con el lazo activo aunque CEM1 no este vigilada y aunque el Detector este
// apagado. Mismo limite que la regla de silencio de CEM1.
inline void Engine::_checkControlSilence(unsigned long nowMs) {
  SourceSettings* cem = _findSourceSettings("CEM1");
  SourceConfig config = (cem != nullptr) ? cem->config : SourceConfig();
  config.sampleIntervalMs = Intervals::MeasureMagneticField;
  // En campo nulo el lazo no usa a CEM1 como realimentacion (duty fijo del
  // mapa): no hay "manejo a ciegas" que cortar por este camino; la
  // vigilancia de silencio de la fuente sigue por el Detector si esta vigilada.
  if (!controlSilenceExpired(_controlLoopEnabled && !_nullRun, _isSettlingTime, nowMs,
                             _lastMagnetometerAliveMs, config)) return;

  unsigned long silent = nowMs - _lastMagnetometerAliveMs;

  unsigned long missed = silent / config.sampleIntervalMs;
  if (missed < config.maxMissedSamples) missed = config.maxMissedSamples;
  if (missed > 65535UL) missed = 65535UL;


  // Mismo par flag_data + result_data que un corte del Detector, para que
  // la pantalla y el monitor lo muestren igual.
  JsonDocument flag;
  flag["source"] = "CEM1";
  flag["type"] = "silence";
  flag["count"] = (uint16_t)missed;
  flag["limit"] = config.maxMissedSamples;
  _serial.sendCommand(Commands::OneFlagsData, flag);

  ResultData& result = _runtimeState.draftResult();
  strncpy(result.reason, "critical", sizeof(result.reason) - 1);
  // PSTR: el formato queda en flash, no en los 8 KB de RAM.
  snprintf_P(result.description, sizeof(result.description),
             PSTR("CEM1: sin lecturas del magnetometro con el lazo activo (%u/%u)"),
             (unsigned)missed, (unsigned)config.maxMissedSamples);
  strncpy(result.source, "CEM1", sizeof(result.source) - 1);
  strncpy(result.type, "silence", sizeof(result.type) - 1);
  result.count = (uint16_t)missed;
  result.limit = config.maxMissedSamples;
  LOG_E(CORRIDA, "fin motivo=critical CEM1 sin lecturas con el lazo activo n=%u/%u",
        (unsigned)missed, (unsigned)config.maxMissedSamples);
  _finish();
}

inline void Engine::_start() {
  // Ver comentario de _isSettlingTime en la declaracion de la clase.
  _isSettlingTime = true;
  // t = 0 de los escenarios sinteticos (scenario.hpp).
  Scenario::restart(millis());
  // t = 0 del silencio (tarjeta 25): una fuente que no da NINGUNA lectura
  // desde el start corta al terminar la estabilizacion o al vencer su
  // limite, lo que ocurra despues. Va despues de los configureSource() del
  // handler de Start, que resetean las fuentes.
  _detector.resetSilence(millis());
  _lastMagnetometerAliveMs = millis();
  _cemZone = ZoneWatch();
  _tempZone = ZoneWatch();
  // Con un campo que no es el real, nada se energiza: ver _syntheticField.
  // Va ANTES de enableAll(), que es quien lo respeta.
  _coilChannels.setOutputsInhibited(_syntheticField);
  TargetData target = _runtimeState.target();
  _coilExcitation.start(target.frequencyTarget);
  _fieldController.reset();
  // El RMS se mide a la frecuencia que REALMENTE sale del AD9833 (cuantizada,
  // paso ~0.093 Hz), no a la pedida: a 1 Hz la diferencia es del 0.1 %.
  _magnetometerManager.beginSamplingAll(
      FieldSampling::ad9833ActualHz((float)target.frequencyTarget), Intervals::MeasureMagneticField);

  // COMO se excita lo decide el mapa de calibracion (controlmap.hpp). El
  // handler de Start ya rechazo los arranques de campo nulo sin punto
  // mapeado, asi que acá el plan solo puede dar un duty de partida.
  StartPlan plan = planStart(_controlMap, _coilExcitation.mode(),
                             target.cemTarget, (float)target.frequencyTarget, _balanceMax);
  _nullRun = _controlLoopEnabled && plan.openLoop;
  _coilChannels.setBalance(plan.openLoop ? plan.balance : 0.0f);

  // Con el lazo deshabilitado (control.enabled == false, "solo sensado")
  // el PWM se deja apagado -- ver _controlLoopEnabled.
  if (_controlLoopEnabled) {
    if (plan.openLoop) {
      // Campo nulo: duty fijo del mapa, el mismo estres que en campo X; el
      // objetivo medido es 0 y no hay lazo que lo persiga.
      _coilChannels.writeAll(plan.initialDuty);
      LOG_I(CONTROL, "campo nulo: duty fijo del mapa=%.3f balance=%.3f", plan.initialDuty, plan.balance);
    } else {
      float startDuty = _scenarioStartDuty(plan.initialDuty);
      _fieldController.setSetpoint(target.cemTarget);
      _fieldController.preload(startDuty);
      _coilChannels.writeAll(_fieldController.getOutput());
      LOG_I(CONTROL, "lazo desde duty=%.3f origen=%S", startDuty,
            plan.initialDuty > 0.0f ? PSTR("mapa") : startDuty > 0.0f ? PSTR("escenario") : PSTR("cero"));
    }
    _coilChannels.enableAll();
  } else {
    LOG_W(CONTROL, "lazo deshabilitado (control.enabled=false): se mide sin excitar");
  }
  if (!_syntheticField) {
    _mainPowerSwitch.enable();
  } else {
    LOG_W(CONTROL, "CEM1 no es el sensor real: potencia y bobinas bloqueadas");
  }
  _engineState.setState(State::Running);
}

inline void Engine::_stop() {
  _magnetometerManager.endSamplingAll();
  _mainPowerSwitch.disable();
  _coilExcitation.stop();
  _coilChannels.disableAll();
  _fieldController.reset();
  _nullRun = false;
  _engineState.setState(State::Finished);
}

inline void Engine::_reset() {
  _magnetometerManager.endSamplingAll();
  _mainPowerSwitch.disable();
  _coilExcitation.stop();
  _coilChannels.disableAll();
  _fieldController.reset();
  _nullRun = false;
  _detector.reset();
  _engineState.setState(State::Ready);
  _runtimeState.reset();
  _runNumber = 0;
}

// Bring-up temporal INT-001: ver comentario de _controlLoopEnabled en la
// declaracion de la clase y testmoderesolver.hpp para la tabla completa.
inline void Engine::registerThermometers(IThermometer* real, IThermometer* sim) {
  _realThermometer = real;
  _simThermometer = sim;
}

// Con CEM1 en escenario y planta (setpointDuty > 0) pero sin punto del mapa,
// el lazo arrancaria en duty 0: campo 0, muy por debajo del critico, y con
// maxStep 0.05 cada 2 s tarda ~20 s en llegar -- mas que la estabilizacion
// (10 s). Todo escenario que vigila CEM1 terminaba cortando por "critical" a
// los ~16 s, que es lo que probaba el arranque y no el escenario. Se parte
// del duty de equilibrio de la planta, que es lo que el mapa daria si el
// punto estuviera medido.
//
// OJO, esto NO arregla nada del equipo real: con bobinas reales y sin punto
// mapeado el lazo arranca igual de 0 y tiene el mismo problema. Por eso el
// mapa (controlmap.hpp) existe y por eso aca solo se toca el escenario.
inline float Engine::_scenarioStartDuty(float mappedDuty) {
  if (mappedDuty > 0.0f) return mappedDuty;
  SourceSettings* cem = _findSourceSettings("CEM1");
  if (cem == nullptr || cem->sensor != SensorChoice::Scenario) return mappedDuty;
  if (cem->scenario.setpointDuty <= 0.0f) return mappedDuty;
  return cem->scenario.setpointDuty;
}

inline ScenarioRanges Engine::_scenarioRangesOf(const SourceConfig& config) {
  ScenarioRanges ranges;
  ranges.normalMin = config.normalMin;
  ranges.normalMax = config.normalMax;
  ranges.criticalMin = config.criticalMin;
  ranges.criticalMax = config.criticalMax;
  return ranges;
}

inline void Engine::_applyNonNegative(JsonVariantConst value, float& target, const char* key) {
  if (!value.is<float>()) return;
  float v = value.as<float>();
  if (v >= 0.0f) {
    target = v;
    return;
  }
  LOG_W(CONFIG, "config_scenario %s=%.3f negativo: se ignora", key, v);
}

// Los rangos con los que el Detector va a juzgar la corrida, una linea por
// fuente vigilada. Son los que derivan de lo que eligio el operador (CEM1)
// o los que mando tal cual (TEMP1), mas las reglas de la SD.
inline void Engine::_logSourceRanges(const char* name, const char* unit) {
  Source* source = _detector.findSource(name);
  if (source == nullptr) return;
  const SourceConfig& c = source->getConfig();
  LOG_I(DETECTOR, "%s normal=%.3f..%.3f critico=%.3f..%.3f %S ventana=%u reglas critical=%u/%u racha=%u/%u frecuencia=%u/%u",
        name, c.normalMin, c.normalMax, c.criticalMin, c.criticalMax, unit, (unsigned)c.bufferSize,
        c.critical.threshold, c.critical.maxEvents, c.streak.threshold, c.streak.maxEvents,
        c.frequency.threshold, c.frequency.maxEvents);
}

// Una linea cuando la medicion SALE de normal (o empeora de zona) y otra
// cuando vuelve, con cuanto estuvo afuera. Solo despues de la
// estabilizacion, igual que el Detector. Va ANTES de addSample() en los
// handlers: el log usa stack, y asi no se suma al camino del corte.
inline void Engine::_watchZone(ZoneWatch& watch, const char* name, float value, const char* unit) {
  watch.last = value;
  if (_isSettlingTime) return;
  Source* source = _detector.findSource(name);
  if (source == nullptr) return;
  const SourceConfig& c = source->getConfig();
  uint8_t zone = (value < c.criticalMin || value > c.criticalMax) ? 2
               : (value < c.normalMin || value > c.normalMax)     ? 1
                                                                   : 0;
  if (zone == watch.zone) return;
  unsigned long now = millis();
  if (zone == 0) {
    LOG_I(DETECTOR, "%s vuelve a normal valor=%.3f %S tras %lu s", name, value, unit, (now - watch.since) / 1000UL);
  } else if (zone > watch.zone) {
    LOG_W(DETECTOR, "%s sale de normal valor=%.3f %S zona=%S", name, value, unit,
          zone == 2 ? PSTR("critica") : PSTR("advertencia"));
  } else {
    LOG_I(DETECTOR, "%s vuelve de critica a advertencia valor=%.3f %S", name, value, unit);
  }
  if (watch.zone == 0) watch.since = now;
  watch.zone = zone;
}

inline void Engine::registerScenarioSensors(MagnetometerScenario* magnetometer, ThermometerScenario* thermometer) {
  _scenarioMagnetometer = magnetometer;
  _scenarioThermometer = thermometer;
}

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

    // Solo se borra o se crea cuando CAMBIA si la fuente se vigila. Antes
    // se hacia removeSource()+addSource() en cada start (delete + new del
    // Source con la trama del start todavia viva en el heap), y eso
    // fragmentaba el heap corrida tras corrida (ver diag.hpp). Una fuente
    // que se conserva queda igual que una recien creada: el handler de Start
    // la reconfigura siempre (configureSource -> setConfig -> reset).
    bool present = _detector.findSource(settings.name) != nullptr;
    if (present && !settings.enabled) {
      _detector.removeSource(settings.name);
    }
    else if (!present && settings.enabled) {
      _detector.addSource(settings.name);
    }

  }

  SourceSettings* cem = _findSourceSettings("CEM1");
  SensorChoice cemSensor = (cem != nullptr) ? cem->sensor : SensorChoice::Real;
  IMagnetometer* magnetometer = &_realMagnetometer;
  if (cemSensor == SensorChoice::Sim) {
    magnetometer = &_simMagnetometer;
  }
  else if (cemSensor == SensorChoice::Scenario && _scenarioMagnetometer != nullptr) {
    _scenarioMagnetometer->setSignal(cem->scenario);
    magnetometer = _scenarioMagnetometer;
  }
  _magnetometerManager.addMagnetometer(magnetometer);
  _syntheticField = (cemSensor != SensorChoice::Real);


  // TEMP1: "none" no registra ningun termometro. Es lo que hay que usar sin
  // DS18B20 cableado: registrado, cada medicion dispara una conversion y
  // lee un scratchpad vacio (~10 ms de bus) aunque no haya nadie.
  _thermometerManager.clearThermometers();
  SourceSettings* temp = _findSourceSettings("TEMP1");
  SensorChoice tempSensor = (temp != nullptr) ? temp->sensor : SensorChoice::Real;
  IThermometer* thermometer = nullptr;
  if (tempSensor == SensorChoice::Real) {
    thermometer = _realThermometer;
    if (thermometer != nullptr && temp != nullptr && temp->hasAddress) thermometer->setAddress(temp->address);
  }
  else if (tempSensor == SensorChoice::Sim) thermometer = _simThermometer;
  else if (tempSensor == SensorChoice::Scenario && _scenarioThermometer != nullptr) {
    _scenarioThermometer->setSignal(temp->scenario);
    thermometer = _scenarioThermometer;
  }
  if (thermometer != nullptr) _thermometerManager.addThermometer(thermometer);

  _detector.setEnabled(_detectorEnabled);

  LOG_I(DETECTOR, "fuentes CEM1=%s%S TEMP1=%s%S detector=%S",
        SensorChoices::name(cemSensor), (cem != nullptr && cem->enabled) ? PSTR("/vigilada") : PSTR(""),
        SensorChoices::name(tempSensor), (temp != nullptr && temp->enabled) ? PSTR("/vigilada") : PSTR(""),
        _detectorEnabled ? PSTR("on") : PSTR("off"));
  if (!_detectorEnabled) {
    LOG_W(DETECTOR, "detector apagado: ninguna fuente puede cortar la corrida");
  }
  // Vigilar TEMP1 sin termometro no falla, pero nunca corta por temperatura.
  if (thermometer == nullptr && temp != nullptr && temp->enabled) {
    LOG_W(DETECTOR, "TEMP1 vigilada sin termometro: nunca va a cortar por temperatura");
  }
}

// Punto unico donde termina un experimento (tiempo cumplido, stop manual o
// corte de seguridad por flag). _reset() borra el resultado de
// RuntimeState (via _runtimeState.reset()) y _stop()/_reset() dejan el
// Engine en Ready antes de que el ESP32 pueda ver "finished" en state_data
// -- por eso el resultado se publica ANTES de _reset(), asi onResult()
// dispara el envio de result_data mientras el dato todavia es valido.
inline void Engine::_finish(const char* reason, const char* description) {
  // draftResult() deja todo en cero: el -1 conserva el terminador.
  ResultData& data = _runtimeState.draftResult();
  strncpy(data.reason, reason, sizeof(data.reason) - 1);
  strncpy(data.description, description, sizeof(data.description) - 1);
  _finish();
}

// Sigue siendo el UNICO lugar donde termina un experimento: cada corte
// escribe su resultado en _runtimeState.draftResult() (en el lugar, sin
// copias de 116 bytes en el stack) y entra por aca.
//
// Orden: primero se apaga (_stop), despues se informa (publishResult ->
// onResult -> _sendResultData) y recien despues _reset(), que es lo unico
// que borra el resultado. Antes se informaba primero: con la cola serie
// llena, _sendResultData() puede bloquear ~0.4 s vaciandola, y eso no puede
// demorar el corte de las bobinas.
inline void Engine::_finish() {
  Diag::mark(Diag::CrumbFinish);
  _stop();
  Diag::mark(Diag::CrumbResult);
  _runtimeState.publishResult();
  Diag::mark(Diag::CrumbReset);
  _reset();
}

// Ademas del estado viaja el diagnostico del Mega (diag.hpp), que la ESP32
// loguea y usa para notar un reinicio a mitad de corrida:
//   up  segundos desde el arranque (si baja, el Mega se reinicio)
//   rst MCUSR al arrancar (0 = salto a 0, 8 = watchdog, 1 = encendido...)
//   stg etapa en la que estaba al morir el arranque anterior (0 = ninguna)
//   gap bytes que quedaron, en el peor momento, entre heap y stack
//   ram bytes libres ahora
//   run numero de la corrida en curso (solo corriendo)
// ~90 bytes: entra holgado en el frame (test_statedata_fits_with_run).
inline void Engine::_sendState() {
  JsonDocument doc;
  doc["status"] = _engineState.getState();
  doc["up"] = millis() / 1000UL;
  doc["rst"] = Diag::resetFlags;
  doc["stg"] = Diag::previousCrumb;
  doc["gap"] = Diag::minGap();
  doc["ram"] = Diag::freeNow();
  // Solo con una corrida en marcha (_reset() lo pone en 0).
  if (_runNumber > 0) doc["run"] = _runNumber;
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
  // Referencia y no copia: son 116 bytes de stack en el camino mas profundo
  // del firmware (el corte por alerta llega aca desde dentro del callback
  // del sensor).
  const ResultData& data = _runtimeState.result();
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
  else if (strcmp(data.reason, "refused") == 0) {
    doc["cause"] = data.cause;
  }

  // Sin heap, ArduinoJson deja los textos en null EN SILENCIO y el frame
  // sale igual, con motivo vacio.
  if (doc.overflowed()) {
    LOG_E(ENLACE, "result_data sin memoria ram_libre=%d B", Diag::freeNow());
  }

  // Es el UNICO mensaje que no se reintenta y sin el la ESP32 no sabe que
  // la corrida termino (ni el monitor la cierra). Si la cola no tiene lugar
  // -- un corte por alerta encola justo antes su flag_data, y puede haber
  // coil_data/cem_data esperando -- se vacia bloqueando y se reintenta. Las
  // bobinas ya estan apagadas (ver _finish).
  if (!_serial.sendCommand(Commands::ResultData, doc)) {
    LOG_W(ENLACE, "cola llena: se vacia para no perder result_data");
    _serial.flush();
    if (!_serial.sendCommand(Commands::ResultData, doc)) {
      LOG_E(ENLACE, "result_data no se pudo enviar");
    }
  }
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
    LOG_W(CONFIG, "config_intervals %s=0 invalido: se ignora", key);
    return;
  }
  target = value;
  LOG_D(CONFIG, "config_intervals %s=%lu ms", key, value);
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
  }
  else if(strcmp(name, Tasks::MeasureCurrent) == 0) {
    _currentSensorsManager.updateAll();

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
    LOG_D(DETECTOR, "fin de la estabilizacion: el detector evalua");
    // Desde aca el silencio corta, igual que el resto de las reglas.
    _detector.armSilence();
    _timer.removeTask(Tasks::SettlingTime);
  }
  else if(strcmp(name, Tasks::Finish) == 0) {
    LOG_I(CORRIDA, "fin motivo=completed duracion cumplida");
    _finish("completed", "Duracion completa alcanzada");
  }
}

// Serial Callbacks
inline void Engine::onCommand(const char* command, JsonVariantConst params) {

  if (strcmp(command, Commands::Ping) == 0) {

    if (!params["value"].is<long>()) {
      return;
    }

    long value = params["value"].as<long>();
    // Testigo de que la linea ESP32 -> Mega funciona y del margen de RAM:
    // con <~300 bytes el Mega ya se colgo al procesar tramas.
    LOG_D(ENLACE, "ping ram_libre=%d B gap=%d B", Diag::freeNow(), Diag::minGap());
    _serial.sendPong(value);
  }

  else if (strcmp(command, Commands::Pong) == 0) {

    if (!params["value"].is<long>()) {
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


    // ========================================================
    // CONFIGURANDO TARGETS
    // ========================================================


    // --- CEM ---

    if (!params["cem"].is<float>()) {
      LOG_W(CORRIDA, "start rechazado: parametro cem invalido o ausente");
      return;
    }

    float cemTarget = params["cem"].as<float>();

    // --- FRECUENCIA ---

    if (!params["freq"].is<int>()) {
      LOG_W(CORRIDA, "start rechazado: parametro freq invalido o ausente");
      return;
    }

    int freqTarget = params["freq"].as<int>();

    // --- DURACION ---

    if (!params["dur"].is<unsigned long>()) {
      LOG_W(CORRIDA, "start rechazado: parametro dur invalido o ausente");
      return;
    }

    unsigned long durTarget = params["dur"].as<unsigned long>();

    // --- GUARDAR TARGETS ---

    _runtimeState.setTarget(
      cemTarget,
      freqTarget,
      durTarget
    );

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
    }

    // Campo nulo sin punto mapeado para esta intensidad y frecuencia (o con
    // un balance fuera del tope): se rechaza el start entero. No hay duty que
    // aplicar, y excitar "a ojo" daria un grupo control de mentira. Mismo
    // criterio que un parametro invalido: no se arranca, y la ESP32 lo
    // muestra en la pantalla Resultado como "no inicio" (_refuseStart).
    StartPlan startPlan = planStart(_controlMap, mode, cemTarget, (float)freqTarget, _balanceMax);
    if (startPlan.refusal != StartRefusal::None) {
      if (startPlan.refusal == StartRefusal::NoMappedPoint) {
        _refuseStart("nomap", "Campo nulo sin punto en el mapa");
      } else {
        _refuseStart("balance", "Balance fuera de tope");
      }
      return;
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

    _applySourceSettings();
    _applyCurrentSensorSettings();


    // ========================================================
    // CONFIGURACION SOURCE CEM
    // ========================================================

    if (!params["tol"].is<int>()) {
      LOG_W(CORRIDA, "start rechazado: parametro tol invalido o ausente");
      return;
    }

    int cemTol = params["tol"].as<int>();

    // Copia de la plantilla (bufferSize + reglas, ya sea los defaults
    // compilados o lo que pisó la SD) mas los rangos derivados de lo que
    // eligio el operador. Se trabaja sobre una copia para que la plantilla
    // siga intacta para el proximo experimento.
    SourceSettings* cemSettings = _findSourceSettings("CEM1");
    SourceConfig configCem = cemSettings->config;
    // Cadencia real de medicion, para el corte por silencio: la cantidad de
    // lecturas perdidas se traduce a tiempo con esto (Source::silenceLimitMs).
    configCem.sampleIntervalMs = Intervals::MeasureMagneticField;
    // Campo nulo: la banda se centra en 0 y mantiene la tolerancia del
    // operador sobre la intensidad elegida; en campo X se deriva del target.
    if (mode == FieldMode::Null) {
      DetectorConfigBuilder::applyCemNullRanges(
        configCem, cemTarget, cemTol, cemSettings->criticalMultiplier);
    } else {
      DetectorConfigBuilder::applyCemRanges(
        configCem, cemTarget, cemTol, cemSettings->criticalMultiplier);
    }

    _detector.configureSource("CEM1", configCem);
    // Los niveles del escenario se miden contra ESTOS rangos (scenario.hpp):
    // se pasan aunque CEM1 no use el escenario, es barato y deja al sensor
    // listo si una corrida posterior lo elige.
    if (_scenarioMagnetometer != nullptr) {
      _scenarioMagnetometer->setRanges(_scenarioRangesOf(configCem));
    }

    // ========================================================
    // CONFIGURACION SOURCE TEMP
    // ========================================================

    // --- TEMP NORMAL MIN ---

    if (!params["tnmin"].is<float>()) {
      LOG_W(CORRIDA, "start rechazado: parametro tnmin invalido o ausente");
      return;
    }

    float tempNormalMin = params["tnmin"].as<float>();

    // --- TEMP NORMAL MAX ---

    if (!params["tnmax"].is<float>()) {
      LOG_W(CORRIDA, "start rechazado: parametro tnmax invalido o ausente");
      return;
    }

    float tempNormalMax = params["tnmax"].as<float>();

    // --- TEMP CRITICAL MIN ---

    if (!params["tcmin"].is<float>()) {
      LOG_W(CORRIDA, "start rechazado: parametro tcmin invalido o ausente");
      return;
    }

    float tempCriticalMin = params["tcmin"].as<float>();

    // --- TEMP CRITICAL MAX ---

    if (!params["tcmax"].is<float>()) {
      LOG_W(CORRIDA, "start rechazado: parametro tcmax invalido o ausente");
      return;
    }

    float tempCriticalMax = params["tcmax"].as<float>();

    SourceConfig configTemp = _findSourceSettings("TEMP1")->config;
    configTemp.sampleIntervalMs = Intervals::MeasureTemperature;
    DetectorConfigBuilder::applyTempRanges(
      configTemp, tempNormalMin, tempNormalMax, tempCriticalMin, tempCriticalMax);


    _detector.configureSource("TEMP1", configTemp);
    if (_scenarioThermometer != nullptr) {
      _scenarioThermometer->setRanges(_scenarioRangesOf(configTemp));
    }
    _logSourceRanges("CEM1", PSTR("mT"));
    _logSourceRanges("TEMP1", PSTR("C"));

    // ========================================================
    // ACK
    // ========================================================


    JsonDocument doc;

    doc["command"] = Commands::Start;


    _serial.sendCommand(
      Commands::Ack,
      doc
    );

    // ========================================================
    // INICIO
    // ========================================================

    // `run` es el numero de corrida de la ESP32 (runcounter.hpp): solo para
    // este log, asi la linea del Mega se busca por el mismo #N que el monitor
    // y el log de la ESP32. Ausente (ESP32 vieja) sale #0.
    _runNumber = params["run"] | 0UL;
    LOG_I(CORRIDA, "inicio corrida=#%lu cem=%.3f mT freq=%d Hz dur=%lu s tol=%d %% modo=%S",
          (unsigned long)_runNumber, cemTarget, freqTarget, durTarget / 1000UL, cemTol,
          mode == FieldMode::Null ? PSTR("null") : PSTR("x"));
    _start();


  }

  else if (strcmp(command, Commands::Stop) == 0) {

    LOG_I(CORRIDA, "fin motivo=stopped detenido por el operador");
    _finish("stopped", "Detenido manualmente por el operador");

    JsonDocument doc;
    doc["command"] = Commands::Stop;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  else if (strcmp(command, Commands::Reset) == 0) {

    LOG_D(CORRIDA, "reset");
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
      LOG_W(CONFIG, "config_control kp=%.4f invalido: se ignora", params["kp"].as<float>());
    }
    if (params["maxStep"].is<float>() && !_fieldController.setMaxStep(params["maxStep"].as<float>())) {
      LOG_W(CONFIG, "config_control maxStep=%.4f invalido: se ignora", params["maxStep"].as<float>());
    }
    if (params["deadBand"].is<float>() && !_fieldController.setDeadBand(params["deadBand"].as<float>())) {
      LOG_W(CONFIG, "config_control deadBand=%.4f invalido: se ignora", params["deadBand"].as<float>());
    }
    // Tope del balance del campo nulo: se rechaza entero si esta fuera de
    // 0..MaxBalance (CoilChannels), igual que los demas valores de control.
    if (params["balanceMax"].is<float>()) {
      float balanceMax = params["balanceMax"].as<float>();
      if (balanceMax >= 0.0f && balanceMax <= CoilChannels::MaxBalance) {
        _balanceMax = balanceMax;
      } else {
        LOG_W(CONFIG, "config_control balanceMax=%.3f fuera de 0..%.2f: se ignora", balanceMax,
              CoilChannels::MaxBalance);
      }
    }
    // En false el lazo mide pero no toca el PWM (lo que antes eran los
    // testMode 1/4, "solo sensado").
    if (params["enabled"].is<bool>()) {
      _controlLoopEnabled = params["enabled"].as<bool>();
    }
    LOG_D(CONFIG, "config_control kp=%.4f maxStep=%.4f deadBand=%.4f balanceMax=%.3f lazo=%d",
          _fieldController.kp(), _fieldController.maxStep(), _fieldController.deadBand(), _balanceMax,
          _controlLoopEnabled);

    JsonDocument doc;
    doc["command"] = Commands::ConfigControl;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  // Un punto del mapa de calibracion (controlmap.hpp). `k` es el indice, `n`
  // cuantos puntos trae la tarjeta (los lugares >= n se vacian, para que un
  // punto que la tarjeta nueva ya no tiene no sobreviva de un arranque
  // anterior), `i`/`f` intensidad y frecuencia, `d` duty comun, `b` balance.
  // Se aplica al recibirlo: el mapa solo se consulta en el start, nunca en
  // caliente. Como los otros config_*, acusa recibo aunque el valor se
  // rechace -- el rechazo solo se loguea y ese punto queda vacio.
  else if (strcmp(command, Commands::ConfigMap) == 0) {

    if (!params["k"].is<int>() || !params["n"].is<int>()) {
      LOG_W(CONFIG, "config_map sin k o n: sin ack");
      return;
    }
    int k = params["k"].as<int>();
    int n = params["n"].as<int>();

    if (n == 0 && k == 0) {
      // Tarjeta sin mapa: se vacia el que el Mega tuviera de una conexion
      // anterior, o el campo nulo correria con puntos que esta tarjeta no dice.
      _controlMap.clear();
      LOG_D(CONFIG, "config_map vacio: se borra el mapa");
    } else if (n > 0 && n <= ControlMap::MaxPoints && k >= 0 && k < n) {
      _controlMap.setCount((uint8_t)n);
      float balance = params["b"].is<float>() ? params["b"].as<float>() : 0.0f;
      bool stored = params["i"].is<float>() && params["f"].is<float>() && params["d"].is<float>() &&
                    _controlMap.setPoint((uint8_t)k, params["i"].as<float>(), params["f"].as<float>(),
                                         params["d"].as<float>(), balance);
      if (stored) {
        LOG_D(CONFIG, "config_map punto %d/%d i=%.3f f=%.1f d=%.3f b=%.3f", k + 1, n, params["i"].as<float>(),
              params["f"].as<float>(), params["d"].as<float>(), balance);
      } else {
        LOG_W(CONFIG, "config_map punto %d/%d invalido: queda vacio", k + 1, n);
      }
    } else {
      LOG_W(CONFIG, "config_map indice k=%d n=%d fuera de rango: se ignora", k, n);
    }

    JsonDocument doc;
    doc["command"] = Commands::ConfigMap;
    doc["k"] = k;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  else if (strcmp(command, Commands::ConfigCoil) == 0) {

    const char* coilName = params["name"];
    if (coilName == nullptr) {
      LOG_W(CONFIG, "config_coil sin name: sin ack");
      return;
    }

    CoilChannel* channel = _coilChannels.findByName(coilName);
    if (channel == nullptr) {
      // Una bobina que la tarjeta declara deshabilitada y el Mega no tiene
      // registrada es lo esperado (se monta despues): solo en detalle.
      if (params["enabled"].is<bool>() && !params["enabled"].as<bool>()) {
        LOG_D(CONFIG, "config_coil %s deshabilitada y no registrada: nada que hacer", coilName);
      } else {
        LOG_W(CONFIG, "config_coil bobina %s habilitada en la tarjeta pero no registrada en el Mega: se ignora",
              coilName);
      }
    } else {
      if (params["enabled"].is<bool>()) {
        if (params["enabled"].as<bool>()) channel->enable();
        else channel->disable();
      }
      if (params["calibrationFactor"].is<float>() &&
          !channel->setCalibrationFactor(params["calibrationFactor"].as<float>())) {
        LOG_W(CONFIG, "config_coil %s calibrationFactor=%.3f fuera de 0.1..5.0: se ignora", coilName,
              params["calibrationFactor"].as<float>());
      }
      LOG_D(CONFIG, "config_coil %s enabled=%d factor=%.3f", coilName, channel->isEnabled(),
            channel->calibrationFactor());
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
      LOG_W(CONFIG, "config_source fuente desconocida %s: sin ack", sourceName ? sourceName : "-");
      return;
    }

    if (params["enabled"].is<bool>()) {
      settings->enabled = params["enabled"].as<bool>();
    }

    // Un valor que no vale para esa fuente se rechaza y queda el anterior
    // (ver SensorChoices::parse).
    if (params["sensor"].is<const char*>()) {
      const char* value = params["sensor"].as<const char*>();
      if (!SensorChoices::parse(settings->name, value, settings->sensor)) {
        LOG_W(CONFIG, "config_source %s sensor=%s no vale para esta fuente: se ignora", settings->name, value);
      }
    }

    if (params["bufferSize"].is<size_t>()) {
      size_t bufferSize = params["bufferSize"].as<size_t>();
      if (DetectorConfigBuilder::isValidBufferSize(bufferSize)) {
        settings->config.bufferSize = bufferSize;
      } else {
        LOG_W(CONFIG, "config_source %s bufferSize=%u fuera de rango: se ignora", settings->name,
              (unsigned)bufferSize);
      }
    }

    if (params["maxMissedSamples"].is<unsigned long>()) {
      unsigned long missed = params["maxMissedSamples"].as<unsigned long>();
      if (DetectorConfigBuilder::isValidMaxMissedSamples(missed)) {
        settings->config.maxMissedSamples = (uint16_t)missed;
      } else {
        LOG_W(CONFIG, "config_source %s maxMissedSamples=%lu fuera de 1..20: se ignora", settings->name, missed);
      }
    }

    // La direccion del DS18B20 (solo TEMP1). Se valida entera -- familia y
    // CRC -- y una mal copiada se rechaza: quedaria apuntando a un sensor
    // que no existe, leyendo -127 y cortando por silencio. A diferencia del
    // resto de la plantilla, fuera de un experimento se aplica YA, para que
    // la temperatura aparezca en pantalla sin tener que arrancar una
    // corrida; con una corrida en curso espera al proximo start, como todo.
    if (params["address"].is<const char*>()) {
      const char* text = params["address"].as<const char*>();
      uint8_t address[OneWireAddress::Length];
      OneWireAddress::ParseResult result = OneWireAddress::parse(text, address);
      if (strcmp(settings->name, "TEMP1") != 0) {
        LOG_W(CONFIG, "config_source address solo vale para TEMP1: se ignora");
      } else if (result != OneWireAddress::ParseResult::Ok) {
        LOG_W(CONFIG, "config_source TEMP1 address rechazada (%S): queda la anterior",
              result == OneWireAddress::ParseResult::BadFormat    ? PSTR("no son 16 digitos hex")
              : result == OneWireAddress::ParseResult::NotDs18b20 ? PSTR("no empieza con 28")
                                                                   : PSTR("CRC no coincide"));
      } else {
        memcpy(settings->address, address, OneWireAddress::Length);
        settings->hasAddress = true;
        LOG_I(CONFIG, "config_source TEMP1 address=%s aplicada", text);
        if (strcmp(_engineState.getState(), State::Running) != 0 && _realThermometer != nullptr) {
          _realThermometer->setAddress(address);
        }
      }
    }

    if (params["criticalMultiplier"].is<float>()) {
      float multiplier = params["criticalMultiplier"].as<float>();
      if (DetectorConfigBuilder::isValidCriticalMultiplier(multiplier)) {
        settings->criticalMultiplier = multiplier;
      } else {
        LOG_W(CONFIG, "config_source %s criticalMultiplier=%.3f fuera de rango: se ignora", settings->name,
              multiplier);
      }
    }
    LOG_D(CONFIG, "config_source %s enabled=%d sensor=%s bufferSize=%u", settings->name, settings->enabled,
          SensorChoices::name(settings->sensor), (unsigned)settings->config.bufferSize);

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
      LOG_W(CONFIG, "config_rule sin source o rule validos: sin ack");
      return;
    }

    Rule* target = nullptr;
    if (strcmp(ruleName, "critical") == 0)       target = &settings->config.critical;
    else if (strcmp(ruleName, "streak") == 0)    target = &settings->config.streak;
    else if (strcmp(ruleName, "frequency") == 0) target = &settings->config.frequency;

    if (target == nullptr) {
      LOG_W(CONFIG, "config_rule %s regla desconocida %s: sin ack", settings->name, ruleName);
      return;
    }

    Rule candidate = *target;
    _readRule(params, candidate);

    if (DetectorConfigBuilder::isValidRule(candidate, settings->config.bufferSize)) {
      target->threshold = candidate.threshold;
      target->cooldown = candidate.cooldown;
      target->maxEvents = candidate.maxEvents;
      LOG_D(CONFIG, "config_rule %s/%s threshold=%u cooldown=%u maxEvents=%u", settings->name, ruleName,
            (unsigned)target->threshold, (unsigned)target->cooldown, (unsigned)target->maxEvents);
    } else {
      LOG_W(CONFIG, "config_rule %s/%s fuera de rango: queda la anterior", settings->name, ruleName);
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
      LOG_W(CONFIG, "config_current sin address o channel: sin ack");
      return;
    }

    uint8_t address = params["address"].as<uint8_t>();
    uint8_t channel = params["channel"].as<uint8_t>();
    CurrentSensorSettings* settings = _findCurrentSensorSettings(address, channel);

    if (settings != nullptr) {
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
      LOG_D(CONFIG, "config_current 0x%02X/%u enabled=%d", address, channel, settings->enabled);
    } else {
      LOG_W(CONFIG, "config_current sin slot para 0x%02X/%u: se ignora", address, channel);
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

  // Senal de escenario de una fuente (scenario.hpp), en niveles relativos a
  // los rangos de la corrida. Claves cortas porque el frame tiene 256 bytes:
  // n=noise oa=oscAmp op=oscPeriod da=dropAt d=setpointDuty. La forma
  // (puntos de control) viene aparte, en config_scenario_pts. Se aplica
  // clave por clave sobre la
  // plantilla y vale desde el proximo start, como el resto de la fuente. Un
  // valor fuera de rango deja esa clave como estaba.
  else if (strcmp(command, Commands::ConfigScenario) == 0) {
    const char* sourceName = params["name"];
    SourceSettings* settings = _findSourceSettings(sourceName);
    if (settings == nullptr) {
      LOG_W(CONFIG, "config_scenario fuente desconocida %s: sin ack", sourceName ? sourceName : "-");
      return;
    }

    ScenarioSignal& signal = settings->scenario;
    if (params["oa"].is<float>()) signal.oscAmp = params["oa"].as<float>();
    // Los que no pueden ser negativos: el ruido es una amplitud y los otros
    // dos son tiempos.
    _applyNonNegative(params["n"], signal.noise, "noise");
    _applyNonNegative(params["op"], signal.oscPeriod, "oscPeriod");
    _applyNonNegative(params["da"], signal.dropAt, "dropAt");
    // Es un duty, asi que ademas no puede pasar de 1.0.
    if (params["d"].is<float>()) {
      float v = params["d"].as<float>();
      if (v >= 0.0f && v <= 1.0f) signal.setpointDuty = v;
      else LOG_W(CONFIG, "config_scenario setpointDuty=%.3f fuera de 0..1: se ignora", v);
    }
    LOG_D(CONFIG, "config_scenario %s noise=%.3f oscAmp=%.3f oscPeriod=%.1f dropAt=%.1f duty=%.3f", settings->name,
          signal.noise, signal.oscAmp, signal.oscPeriod, signal.dropAt, signal.setpointDuty);


    JsonDocument doc;
    doc["command"] = Commands::ConfigScenario;
    doc["name"] = settings->name;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }

  // Puntos de control del escenario de una fuente: {name, p: [t0, n0, t1,
  // n1, ...]} -- lista PLANA, no de pares, porque cada subarreglo le cuesta
  // al parseo un slot mas de ArduinoJson en un Mega con poca RAM. Reemplaza
  // TODOS los puntos o ninguno (Scenario::applyPoints); `p: []` los borra
  // (nivel 0). Siempre se acusa recibo una vez parseado el frame: un
  // rechazo queda en el log y no se reintenta, como en el resto de config_*.
  else if (strcmp(command, Commands::ConfigScenarioPoints) == 0) {
    const char* sourceName = params["name"];
    SourceSettings* settings = _findSourceSettings(sourceName);
    if (settings == nullptr) {
      LOG_W(CONFIG, "config_scenario_pts fuente desconocida %s: sin ack", sourceName ? sourceName : "-");
      return;
    }

    using Scenario::PointsRefusal;
    JsonArrayConst list = params["p"];
    PointsRefusal refusal = PointsRefusal::None;
    if (list.isNull()) refusal = PointsRefusal::Missing;
    else if (list.size() > 2 * Scenario::MaxPoints) refusal = PointsRefusal::TooMany;
    else {
      float flat[2 * Scenario::MaxPoints];
      uint8_t length = 0;
      for (JsonVariantConst v : list) {
        if (!v.is<float>()) { refusal = PointsRefusal::NotNumeric; break; }
        flat[length++] = v.as<float>();
      }
      if (refusal == PointsRefusal::None) refusal = Scenario::applyPoints(settings->scenario, flat, length);
    }
    if (refusal == PointsRefusal::None) {
      LOG_D(CONFIG, "config_scenario_pts %s puntos=%u", settings->name, (unsigned)(list.size() / 2));
    } else {
      // Textos en flash: como literales costaban ~150 bytes de RAM.
      LOG_W(CONFIG, "config_scenario_pts %s rechazados (%S): quedan los anteriores", settings->name,
            refusal == PointsRefusal::OddLength    ? PSTR("cantidad impar de valores")
            : refusal == PointsRefusal::TooMany    ? PSTR("mas de 8 puntos")
            : refusal == PointsRefusal::BadTime    ? PSTR("segundo no entero o fuera de 0..65535")
            : refusal == PointsRefusal::BadLevel   ? PSTR("nivel fuera de -10..10")
            : refusal == PointsRefusal::Unordered  ? PSTR("segundos no ordenados")
            : refusal == PointsRefusal::NotNumeric ? PSTR("valor no numerico")
                                                   : PSTR("falta p"));
    }


    JsonDocument doc;
    doc["command"] = Commands::ConfigScenarioPoints;
    doc["name"] = settings->name;
    _serial.sendCommand(Commands::Ack, doc);
  }

  // Interruptor general del Detector. Se guarda y se aplica en el proximo
  // start, como config_source (ver _detectorEnabled).
  else if (strcmp(command, Commands::ConfigDetector) == 0) {
    if (params["enabled"].is<bool>()) {
      _detectorEnabled = params["enabled"].as<bool>();
      LOG_D(CONFIG, "config_detector enabled=%d (vale desde el proximo start)", _detectorEnabled);
    } else {
      LOG_W(CONFIG, "config_detector sin enabled booleano: se ignora");
    }

    JsonDocument doc;
    doc["command"] = Commands::ConfigDetector;

    _serial.sendCommand(
      Commands::Ack,
      doc
    );
  }
}

inline void Engine::onSerialConnected() {
  LOG_I(ENLACE, "conectado con la ESP32");
}

inline void Engine::onSerialDisconnected() {
  LOG_W(ENLACE, "enlace con la ESP32 perdido");
}

inline void Engine::onEmergencyButtonPressed() {

  // Si no hay experimento corriendo, no hay nada que detener -- evita
  // mandarle a la ESP32 un result_data "stopped" para un experimento que
  // nunca arranco (la llevaria a la pantalla Resultado sin motivo).
  if (strcmp(_engineState.getState(), State::Running) != 0) {
    LOG_D(CORRIDA, "parada de emergencia sin corrida: se ignora");
    return;
  }

  LOG_W(CORRIDA, "fin motivo=stopped parada de emergencia");
  ResultData& result = _runtimeState.draftResult();
  strncpy(result.reason, "stopped", sizeof(result.reason) - 1);
  strncpy_P(result.description, PSTR("Parada de emergencia fisica activada por el operador"),
            sizeof(result.description) - 1);
  result.fromEmergency = true;
  _finish();
}

// Sample Sensor Callbacks
inline void Engine::onMagnetometerSample(IMagnetometer* magnetometer) {
  // Antes del gate de estabilizacion: el silencio cuenta desde el start.
  unsigned long aliveAt = millis();
  _lastMagnetometerAliveMs = aliveAt;
  _detector.noteAlive(magnetometer->getName(), aliveAt);

  // Con "solo sensado" se mide y se reporta, pero no se toca el PWM: ver
  // _controlLoopEnabled.
  // En campo nulo (_nullRun) el duty es el fijo del mapa: no se regula.
  if (_controlLoopEnabled && !_nullRun) {
    _fieldController.update(magnetometer->getMagneticField());
    float duty = _fieldController.getOutput();
    _coilChannels.writeAll(duty);
    LOG_D(CONTROL, "campo=%.4f mT consigna=%.4f mT duty=%.3f", magnetometer->getMagneticField(),
          _runtimeState.target().cemTarget, duty);

  }

  _watchZone(_cemZone, magnetometer->getName(), magnetometer->getMagneticField(), PSTR("mT"));
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
  _detector.noteAlive(thermometer->getName(), millis());

  _watchZone(_tempZone, thermometer->getName(), thermometer->getTemperature(), PSTR("C"));
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
  else if(event.type == EventType::Silence) {
    typeStr = "silence";
  }
  sourceData["type"] = typeStr;


  _serial.sendCommand(Commands::OneFlagsData, sourceData);
  bool cut = event.limit > 0 && event.count >= event.limit;
  // Ultimo valor de la fuente: dice con que medicion se disparo la regla.
  float last = strcmp(source->getName(), "CEM1") == 0 ? _cemZone.last
             : strcmp(source->getName(), "TEMP1") == 0 ? _tempZone.last : NAN;
  if (cut) {
    LOG_E(CORRIDA, "fin motivo=critical %s tipo=%s n=%u/%u ultimo=%.3f", source->getName(), typeStr, event.count,
          event.limit, last);
  } else {
    LOG_W(DETECTOR, "%s alerta tipo=%s n=%u/%u ultimo=%.3f", source->getName(), typeStr, event.count, event.limit,
          last);
  }

  // Corta el experimento cuando CUALQUIERA de los 3 tipos de regla llega a
  // su propio maxEvents -- no es exclusivo del tipo "critical". Una racha
  // (streak) o una inestabilidad sostenida (frequency) que se repitan
  // suficientes veces cortan igual, aunque la muestra individual nunca haya
  // sido "critica".
  if (cut) {

    ResultData& result = _runtimeState.draftResult();
    strncpy(result.reason, "critical", sizeof(result.reason) - 1);
    // PSTR: el formato queda en flash, no en RAM.
    snprintf_P(
        result.description, sizeof(result.description),
        PSTR("%s: limite de alertas %s alcanzado (%u/%u)"),
        source->getName(), typeStr, event.count, event.limit
    );
    strncpy(result.source, source->getName(), sizeof(result.source) - 1);
    strncpy(result.type, typeStr, sizeof(result.type) - 1);
    result.count = event.count;
    result.limit = event.limit;
    _finish();
  }
}
