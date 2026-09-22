#pragma once
#include "configurationoptions.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_SYSTEMDATA = true;


struct PrincipalData {
    // Valores sueltos, sin unidad: Principal y En curso muestran el numero
    // grande y la unidad chica en widgets separados.
    // Se formatean desde los valores numericos de la opcion (intensity,
    // freq, duration...) y NO parseando el label: con los menus viniendo
    // de la SD el label es texto libre del operador.
    char intensityValue[12];
    char frequencyValue[12];
    char durationHours[4];
    char durationMinutes[4];
    char toleranceValue[8];
    char normalTemperatureValue[12];
    char criticalTemperatureValue[12];

    // Modo de exposicion ("CAMPO X" / "CAMPO NULO") y el grupo experimental
    // que implica ("GRUPO TRATADO" / "GRUPO CONTROL").
    char fieldModeValue[16];
    char fieldModeGroup[16];
};

struct ConfigurationData {
  uint8_t targetFieldIntensityOption = 0;
  uint8_t targetFieldFrequencyOption = 0;
  uint8_t targetDurationOption = 0;
  uint8_t fieldIntensityToleranceOption = 0;
  uint8_t normalTemperatureRangeOption = 0;
  uint8_t criticalTemperatureRangeOption = 0;

  // Modo de experimento: indice en ConfigurationOptions::optionsFieldMode.
  // 0 = campo X. El default es X y no campo nulo a proposito, igual que en
  // el Mega: un campo nulo silencioso se ve exactamente igual que un
  // experimento normal, y contaminaria el grupo control sin sintoma.
  // Lo cambia el dropdown ui_ConfiguracionModoOpciones de la
  // pantalla Configuracion (ConfigurationController) y viaja como `mode`
  // en `start` (MySystem::_sendStart).
  uint8_t fieldModeOption = 0;
};

struct ProgressData {
  unsigned long startTime = 0;
  unsigned long duration = 0;
  char health[20] = "normal";
};

// Snapshot del resultado del experimento, tomado en el instante en que
// llega result_data. reason/description los arma el Mega (Engine::_finish);
// health/progressPercent/elapsed son un snapshot de los datos que el ESP32
// ya venia trackeando en vivo (SystemData::progress) -- se copian aca en
// vez de leerse en cada tick de RunningController/ResultadoController
// porque una vez terminado el experimento esos valores no deben seguir
// avanzando con el reloj real.
struct ResultData {
  char reason[16] = "";
  char description[64] = "";
  char health[20] = "";
  float progressPercent = 0.0f;
  char elapsed[9] = "00:00:00";
  // Lo mismo que `elapsed` pero en segundos: la pantalla Resultado nueva
  // muestra horas y minutos en widgets separados, y es mas simple partir
  // un numero que parsear el "hh:mm:ss".
  unsigned long elapsedSeconds = 0;
  // Modo de exposicion con el que corrio el experimento (label de
  // PrincipalData::fieldModeValue). Se congela aca, no se lee de
  // configuration: el operador puede cambiar el modo despues.
  char fieldMode[16] = "";
  // Promedio del campo medido durante el experimento (mT), calculado en
  // el ESP32 a partir de cada cem_data recibido -- el Mega no lo manda.
  float meanMagneticField = 0.0f;
  bool hasMeanMagneticField = false;
  // Alertas distintas activas al momento del corte (0..MAX_ALERTS).
  uint8_t alertCount = 0;

  // Datos crudos del corte, tal como los manda el Mega en result_data (ver
  // RuntimeState::ResultData alla). La pantalla redacta su texto con esto y
  // no parseando `description`.
  //
  // Con reason == "critical": fuente, tipo de regla y cuenta alcanzada.
  char source[16] = "";
  char type[12] = "";
  uint16_t count = 0;
  uint16_t limit = 0;
  // Con reason == "stopped": si vino del pulsador fisico de emergencia.
  bool fromEmergency = false;
};

// Bobinas que muestra la pantalla En curso, una fila por cada una. Es el
// maximo fisico del gabinete (ver CoilChannels en el Mega); la corriente
// de la bobina N llega como "SCT013-N" en current_data.
#define MAX_COILS 4

struct MeasuresData {
  float measureMagneticField = 0.0f;
  float measureTemperature = 0.0f;
  // coilCurrent[0] es SCT013-1. Las cuatro se publican por MQTT desde el
  // grupo `coils` de la telemetria (antes salia solo esta, como campo
  // "BOB1", porque toda la telemetria era un unico mensaje).
  float coilCurrent[MAX_COILS] = {0.0f, 0.0f, 0.0f, 0.0f};
  // Duty REALMENTE aplicado a cada bobina, en por ciento (0..100). Llega en
  // el mismo frame coil_data que la corriente; es el duty comun por el
  // factor de calibracion de esa bobina, no la consigna del FieldController.
  float coilDuty[MAX_COILS] = {0.0f, 0.0f, 0.0f, 0.0f};
  unsigned long latestMagneticFieldUpdate = 0;
  unsigned long latestTemperatureUpdate = 0;
  // Un solo sello por bobina: duty y corriente viajan juntos en coil_data.
  // Distinto de 0 significa "esta bobina existe del otro lado y reporto",
  // que es como En curso decide cuantas filas dibujar cuando no hay seccion
  // coils en la SD.
  unsigned long latestCoilUpdate[MAX_COILS] = {0, 0, 0, 0};
  // Acumulador para el campo medio del experimento (ResultData). Se
  // reinicia al entrar a Running y suma una muestra por cem_data.
  double magneticFieldSum = 0.0;
  unsigned long magneticFieldSamples = 0;
};

#define MAX_ALERTS 3

struct AlertData {
  bool active = false;
  char type[16] = "";
  char source[16] = "";
  int count = 0;
  int limit = 0;
  unsigned long lastUpdate = 0;
};

struct AlertsData {
  AlertData items[MAX_ALERTS];
};

// Cuantos eventos por fuente recuerda AlertHistory: son los 4 paneles que
// tiene cada medicion en la pantalla En curso.
#define MAX_ALERT_DOTS 4
// Fuentes con historial: CEM1 y TEMP1, las unicas que el Detector vigila
// (ver el .ino del Mega). SCT013-1 se mide y se muestra pero no es fuente.
#define MAX_ALERT_SOURCES 2

// Los ULTIMOS eventos de una fuente, del mas viejo al mas nuevo.
//
// Es un registro aparte de AlertsData y no un reemplazo: AlertsData dedupe
// por fuente+tipo (una entrada por tipo, con el contador acumulado) y es lo
// que alimenta la salud y la pantalla Resultado. Esto de aca NO deduplica:
// cada flag_data es un evento y ocupa su propio panel, porque lo que los 4
// paneles muestran es la secuencia reciente, no el estado agregado.
struct AlertHistory {
  char source[16] = "";
  // types[0] es el mas antiguo de los que se muestran (panel de la
  // izquierda) y types[count-1] el mas reciente (derecha).
  char types[MAX_ALERT_DOTS][16] = {};
  uint8_t count = 0;
};

struct AlertsHistoryData {
  AlertHistory sources[MAX_ALERT_SOURCES];
};

namespace StateData {
  constexpr const char* Idle      = "idle";
  constexpr const char* Ready     = "ready";
  constexpr const char* Starting  = "starting";
  constexpr const char* Running   = "running";
  constexpr const char* Stopping  = "stopping";
  constexpr const char* Finished  = "finished";
};

namespace HealthData {
  constexpr const char* Normal   = "normal";
  constexpr const char* Warning  = "warning";
  constexpr const char* Critical = "critical";
};

struct BusyData {
  const char* messageProcess = "Procesando...";
};

// PENDIENTE (decision de diseno, no hay bug que arreglar): estos tres flags
// se mantienen al dia y NO los lee ninguna pantalla. Hoy no hay forma de
// saber desde la HMI si el Mega esta conectado, si hay WiFi o si el broker
// respondio; el rediseno UiFinalParte1/2 no dejo widget para eso (los chips
// de "componentes" del diseno viejo desaparecieron).
//
// Quien esta al dia y quien no:
//  - serialOk  <- MySystem::onSerialConnected/onSerialDisconnected
//  - wifiOk    <- MySystem::onWiFiConnected/onWiFiDisconnected
//  - brokerOk  <- MySystem::onBrokerConnected/onBrokerDisconnected
//  - la SD NO tiene flag: la verdad esta en SdStorage::isReady(), que solo
//    refleja el montaje del arranque (no hay deteccion de insercion en
//    caliente). Para mostrarla habria que agregar un `sdOk` aca y llenarlo
//    en MySystem::begin().
//
// Donde mostrarlos, cuando se decida: los dos headers que ya existen son
// los candidatos naturales -- el de Principal (junto a ui_PrincipalTitle) y
// el de En curso (donde viven los chips de salud
// ui_EnCursoHeaderEstadoNormal/Advertencia/Critico, mismo patron de
// visibilidad excluyente). Configuraciones y Resultado no lo necesitan.
//
// Dos cosas a tener en cuenta al implementarlo:
//  - PrincipalController::update() hoy esta vacio a proposito: Principal
//    solo se refresca en show(). Un indicador de conexion cambia MIENTRAS
//    la pantalla esta a la vista, asi que habria que llenarlo.
//  - Con BENCH_SIN_RED en true (SoftEsp32.ino) no se levantan ni WiFi ni
//    broker, asi que esos dos indicadores quedarian permanentemente en
//    rojo. Es lo correcto, pero conviene no leerlo como una falla.
struct CommunicationData {
  bool serialOk = false;
  bool wifiOk = false;
  bool brokerOk = false;
};

class StateListener {
  public:
    virtual void onStateChanged(const char* oldState, const char* newState) = 0;
};



class SystemData {
  public:
    SystemData();
    PrincipalData principal;
    ConfigurationData configuration;
    MeasuresData measures;
    AlertsData alerts;
  AlertsHistoryData alertsHistory;
    ProgressData progress;
    ResultData result;
    BusyData busy;
    CommunicationData communication;
  private:
    const char* _state = StateData::Idle;
    bool _initialized = false;
    StateListener* _listener;

  public:
    void updatePrincipalConfigurationLabels();
    void pushAlert(const char* type, const char* source, int count, int limit);
    const char* evaluateHealth() const;
    const char* latestUpdateStr(unsigned long latestUpdate);
    // Historial por fuente para los 4 paneles de alerta de En curso.
    void pushAlertEvent(const char* source, const char* type);
    const AlertHistory* alertHistory(const char* source) const;
    void clearAlertHistory();

    const char* elapsedTime();
    unsigned long elapsedSeconds();
    unsigned long remainingSeconds();
    float progressPercent();
    void addMagneticFieldSample(float value);
    float meanMagneticField() const;
    uint8_t activeAlertCount() const;
    const char* getState();
    void setState(const char* newState);
    void setStateListener(StateListener* listener);
    bool isInitialized();
    void setInitialized(bool initialized);
};

inline SystemData::SystemData() : _listener(nullptr) {
  configuration = {};
  measures = {};
  progress = {};
  busy = {};
  communication = {};
  updatePrincipalConfigurationLabels();
}

// Agrega un evento al historial de `source`, creando la entrada la primera
// vez. Con los 4 lugares ocupados corre todo a la izquierda y descarta el
// mas viejo: lo que se muestra son siempre los ULTIMOS 4.
inline void SystemData::pushAlertEvent(const char* source, const char* type) {
  AlertHistory* entry = nullptr;

  for (int i = 0; i < MAX_ALERT_SOURCES; i++) {
    AlertHistory& candidate = alertsHistory.sources[i];
    if (strcmp(candidate.source, source) == 0) { entry = &candidate; break; }
    if (candidate.source[0] == '\0') {
      snprintf(candidate.source, sizeof(candidate.source), "%s", source);
      entry = &candidate;
      break;
    }
  }

  // Mas fuentes distintas que lugares: se ignora en vez de pisar el
  // historial de otra. Hoy no puede pasar (el Detector vigila 2), y si
  // alguna vez vigila mas, esto es lo que hay que agrandar.
  if (entry == nullptr) return;

  if (entry->count < MAX_ALERT_DOTS) {
    snprintf(entry->types[entry->count], sizeof(entry->types[0]), "%s", type);
    entry->count++;
    return;
  }

  for (int i = 0; i < MAX_ALERT_DOTS - 1; i++) {
    snprintf(entry->types[i], sizeof(entry->types[0]), "%s", entry->types[i + 1]);
  }
  snprintf(entry->types[MAX_ALERT_DOTS - 1], sizeof(entry->types[0]), "%s", type);
}

inline const AlertHistory* SystemData::alertHistory(const char* source) const {
  for (int i = 0; i < MAX_ALERT_SOURCES; i++) {
    const AlertHistory& candidate = alertsHistory.sources[i];
    if (strcmp(candidate.source, source) == 0) return &candidate;
  }
  return nullptr;
}

inline void SystemData::clearAlertHistory() {
  alertsHistory = AlertsHistoryData();
}

inline void SystemData::updatePrincipalConfigurationLabels() {
  const auto& intensity = ConfigurationOptions::optionsFieldIntensity[configuration.targetFieldIntensityOption];
  const auto& frequency = ConfigurationOptions::optionsFrequency[configuration.targetFieldFrequencyOption];
  const auto& duration = ConfigurationOptions::optionsDuration[configuration.targetDurationOption];
  const auto& tolerance = ConfigurationOptions::optionsTolFieldIntensity[configuration.fieldIntensityToleranceOption];
  const auto& normalTemp = ConfigurationOptions::optionsRangeNormalTemperature[configuration.normalTemperatureRangeOption];
  const auto& criticalTemp = ConfigurationOptions::optionsRangeCriticalTemperature[configuration.criticalTemperatureRangeOption];

  snprintf(principal.intensityValue, sizeof(principal.intensityValue), "%.1f", intensity.intensity);
  snprintf(principal.frequencyValue, sizeof(principal.frequencyValue), "%d", frequency.freq);
  unsigned long totalMinutes = duration.duration / 60000UL;
  snprintf(principal.durationHours, sizeof(principal.durationHours), "%lu", totalMinutes / 60);
  snprintf(principal.durationMinutes, sizeof(principal.durationMinutes), "%02lu", totalMinutes % 60);
  snprintf(principal.toleranceValue, sizeof(principal.toleranceValue), "%d", tolerance.tol);
  snprintf(principal.normalTemperatureValue, sizeof(principal.normalTemperatureValue), "%.0f~%.0f", normalTemp.tmin, normalTemp.tmax);
  snprintf(principal.criticalTemperatureValue, sizeof(principal.criticalTemperatureValue), "%.0f~%.0f", criticalTemp.tmin, criticalTemp.tmax);

  // El modo es una tabla fija de 2 entradas (ver configurationoptions.hpp):
  // indice 0 = campo X = animales tratados, 1 = campo nulo = grupo control.
  bool isNullField = configuration.fieldModeOption == 1;
  snprintf(principal.fieldModeValue, sizeof(principal.fieldModeValue), "%s", isNullField ? "CAMPO NULO" : "CAMPO X");
  snprintf(principal.fieldModeGroup, sizeof(principal.fieldModeGroup), "%s", isNullField ? "GRUPO CONTROL" : "GRUPO TRATADO");
}

// Mantiene las ultimas MAX_ALERTS alertas distintas (por source+type), mas
// recientes primero. Si ya existe una entrada para el mismo source+type se
// actualiza en lugar de duplicarla, para que siempre se vea el acumulado
// (count/limit) mas reciente de esa alerta.
inline void SystemData::pushAlert(const char* type, const char* source, int count, int limit) {
  int foundIndex = -1;
  for (int i = 0; i < MAX_ALERTS; i++) {
    if (alerts.items[i].active &&
        strcmp(alerts.items[i].type, type) == 0 &&
        strcmp(alerts.items[i].source, source) == 0) {
      foundIndex = i;
      break;
    }
  }

  AlertData updated;
  updated.active = true;
  snprintf(updated.type, sizeof(updated.type), "%s", type);
  snprintf(updated.source, sizeof(updated.source), "%s", source);
  updated.count = count;
  updated.limit = limit;
  updated.lastUpdate = millis();

  int shiftFrom = (foundIndex >= 0) ? foundIndex : (MAX_ALERTS - 1);
  for (int i = shiftFrom; i > 0; i--) {
    alerts.items[i] = alerts.items[i - 1];
  }
  alerts.items[0] = updated;
}

// Salud general del experimento a partir de las alertas activas: sin
// alertas -> normal; con alguna alerta "critical" -> critical (aunque
// tambien haya streak/frequency activas); solo streak/frequency -> warning.
inline const char* SystemData::evaluateHealth() const {
  bool hasWarning = false;

  for (int i = 0; i < MAX_ALERTS; i++) {
    if (!alerts.items[i].active) continue;
    if (strcmp(alerts.items[i].type, "critical") == 0) {
      return HealthData::Critical;
    }
    hasWarning = true;
  }

  return hasWarning ? HealthData::Warning : HealthData::Normal;
}

inline const char* SystemData::latestUpdateStr(unsigned long latestUpdate) {
  static char result[20];
  uint32_t seconds = (millis() - latestUpdate) / 1000;

  if (seconds < 5) {
    strcpy(result, "ahora");
  } 
  else if (seconds < 60) {
    snprintf(result, sizeof(result), "hace %lus", seconds);
  } 
  else if (seconds < 3600) {
    snprintf(result, sizeof(result), "hace %lum", seconds / 60);
  } 
  else if (seconds < 86400) {
    snprintf(result, sizeof(result), "hace %luh", seconds / 3600);
  } 
  else {
    snprintf(result, sizeof(result), "hace %lud", seconds / 86400);
  }

  return result;
}

inline const char* SystemData::elapsedTime() {
  static char result[9]; // "hh:mm:ss"
  if (progress.startTime == 0) {
    strcpy(result, "00:00:00");
    return result;
  }
  // Mismo tope que elapsedSeconds(): las dos formas del tiempo transcurrido
  // (segundos para la pantalla, "hh:mm:ss" para Resultado y la telemetria)
  // tienen que contar lo mismo.
  unsigned long elapsed = elapsedSeconds();
  uint32_t hours   = elapsed / 3600;
  uint32_t minutes = (elapsed % 3600) / 60;
  uint32_t seconds = elapsed % 60;
  snprintf(result, sizeof(result), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return result;
}

// Acotado a la duracion configurada, igual que progressPercent() se acota a
// 100: pasado ese punto el experimento ya termino aunque el Mega todavia no
// haya avisado, y un contador que sigue corriendo al lado de un 100% de
// progreso se lee como que falta algo.
inline unsigned long SystemData::elapsedSeconds() {
  if (progress.startTime == 0) return 0;
  unsigned long elapsedMs = millis() - progress.startTime;
  if (progress.duration > 0 && elapsedMs >= progress.duration) {
    return progress.duration / 1000;
  }
  return elapsedMs / 1000;
}

// Lo que falta para que el Mega dispare Tasks::Finish, en segundos. Vale 0
// tanto sin experimento como una vez vencida la duracion (el Mega puede
// tardar un ciclo de timer en cortar): no cuenta hacia negativo.
inline unsigned long SystemData::remainingSeconds() {
  if (progress.startTime == 0 || progress.duration == 0) return 0;
  unsigned long elapsedMs = millis() - progress.startTime;
  if (elapsedMs >= progress.duration) return 0;
  return (progress.duration - elapsedMs) / 1000;
}

inline void SystemData::addMagneticFieldSample(float value) {
  measures.magneticFieldSum += value;
  measures.magneticFieldSamples++;
}

inline float SystemData::meanMagneticField() const {
  if (measures.magneticFieldSamples == 0) return 0.0f;
  return (float)(measures.magneticFieldSum / measures.magneticFieldSamples);
}

inline uint8_t SystemData::activeAlertCount() const {
  uint8_t count = 0;
  for (int i = 0; i < MAX_ALERTS; i++) {
    if (alerts.items[i].active) count++;
  }
  return count;
}

inline float SystemData::progressPercent() {
  if (progress.duration == 0) return 0.0f;
  if (progress.startTime == 0) return 0.0f;
  unsigned long elapsed = millis() - progress.startTime;
  if (elapsed >= progress.duration) return 100.0f;
  return (100.0f * elapsed) / progress.duration;
}

inline const char* SystemData::getState() {
  return _state;
}

inline void SystemData::setState(const char* newState) {
  if(strcmp(_state, newState) == 0) return;
  if(strcmp(newState, StateData::Idle) == 0) {
    if(_listener != nullptr) {
      _listener->onStateChanged(_state, newState);
    }
    _state = newState;
  }
  else if(strcmp(newState, StateData::Ready) == 0) {
    if(_listener != nullptr) {
      _listener->onStateChanged(_state, newState);
    }
    _state = newState;
  }
  else if(strcmp(newState, StateData::Starting) == 0) {
    if(_listener != nullptr) {
      _listener->onStateChanged(_state, newState);
    }
    _state = newState;
  }
  else if(strcmp(newState, StateData::Running) == 0) {
    if(_listener != nullptr) {
      _listener->onStateChanged(_state, newState);
    }
    _state = newState;
  }
  else if(strcmp(newState, StateData::Stopping) == 0) {
    if(_listener != nullptr) {
      _listener->onStateChanged(_state, newState);
    }
    _state = newState;
  }
  else if(strcmp(newState, StateData::Finished) == 0) {
    if(_listener != nullptr) {
      _listener->onStateChanged(_state, newState);
    }
    _state = newState;
  }
}

inline void SystemData::setStateListener(StateListener* listener) {
  _listener = listener;
}

inline bool SystemData::isInitialized() {
  return _initialized;
}

inline void SystemData::setInitialized(bool initialized) {
  DEBUG_PRINT(DEBUG_SYSTEMDATA, "initialized: ");
  DEBUG_PRINTLN(DEBUG_SYSTEMDATA, initialized);
  _initialized = initialized;
}

