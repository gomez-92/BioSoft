#pragma once
#include "configurationoptions.hpp"


struct PrincipalData {
    char targetFieldIntensity[20];
    char targetFieldFrequency[20];
    char targetDuration[20];
    char fieldIntensityTolerance[20];
    char normalTemperatureRange[20];
    char criticalTemperatureRange[20];
};

struct ConfigurationData {
  uint8_t targetFieldIntensityOption = 0;
  uint8_t targetFieldFrequencyOption = 0;
  uint8_t targetDurationOption = 0;
  uint8_t fieldIntensityToleranceOption = 0;
  uint8_t normalTemperatureRangeOption = 0;
  uint8_t criticalTemperatureRangeOption = 0;
};

struct ProgressData {
  unsigned long startTime = 0;
  unsigned long duration = 0;
  char health[20];
};

struct MeasuresData {
  float measureMagneticField = 0.0f;
  float measureTemperature = 0.0f;
  float measureCurrent = 0.0f;
  unsigned long latestMagneticFieldUpdate = 0;
  unsigned long latestTemperatureUpdate = 0;
  unsigned long latestCurrentUpdate = 0;
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

namespace StateData {
  constexpr const char* Idle      = "idle";
  constexpr const char* Ready     = "ready";
  constexpr const char* Starting  = "starting";
  constexpr const char* Running   = "running";
  constexpr const char* Stopping  = "stopping";
  constexpr const char* Finished  = "finished";
};

struct BusyData {
  const char* messageProcess = "Procesando...";
};

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
    ProgressData progress;
    BusyData busy;
    CommunicationData communication;
  private:
    const char* _state = StateData::Idle;
    bool _initialized = false;
    StateListener* _listener;

  public:
    void updatePrincipalConfigurationLabels();
    void pushAlert(const char* type, const char* source, int count, int limit);
    const char* latestUpdateStr(unsigned long latestUpdate);
    const char* elapsedTime();
    float progressPercent();
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

inline void SystemData::updatePrincipalConfigurationLabels() {
  snprintf(
      principal.targetFieldIntensity,
      sizeof(principal.targetFieldIntensity),
      "%s",
      ConfigurationOptions::optionsFieldIntensity[configuration.targetFieldIntensityOption].label
  );

  snprintf(
      principal.targetFieldFrequency,
      sizeof(principal.targetFieldFrequency),
      "%s",
      ConfigurationOptions::optionsFrequency[configuration.targetFieldFrequencyOption].label
  );

  snprintf(
      principal.targetDuration,
      sizeof(principal.targetDuration),
      "%s",
      ConfigurationOptions::optionsDuration[configuration.targetDurationOption].label
  );

  snprintf(
      principal.fieldIntensityTolerance,
      sizeof(principal.fieldIntensityTolerance),
      "%s",
      ConfigurationOptions::optionsTolFieldIntensity[configuration.fieldIntensityToleranceOption].label
  );

  snprintf(
      principal.normalTemperatureRange,
      sizeof(principal.normalTemperatureRange),
      "%s",
      ConfigurationOptions::optionsRangeNormalTemperature[configuration.normalTemperatureRangeOption].label
  );

  snprintf(
      principal.criticalTemperatureRange,
      sizeof(principal.criticalTemperatureRange),
      "%s",
      ConfigurationOptions::optionsRangeCriticalTemperature[configuration.criticalTemperatureRangeOption].label
  );
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
  unsigned long elapsed = (millis() - progress.startTime) / 1000;
  uint32_t hours   = elapsed / 3600;
  uint32_t minutes = (elapsed % 3600) / 60;
  uint32_t seconds = elapsed % 60;
  snprintf(result, sizeof(result), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return result;
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
  Serial.print("initialized: ");
  Serial.println(initialized);
  _initialized = initialized;
}

