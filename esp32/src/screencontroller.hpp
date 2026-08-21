#pragma once

#include "eventsname.hpp"
#include "screenmanager.hpp"
#include "configurationoptions.hpp"
#include "systemdata.hpp"
#include "iscreen.hpp"
#include "ui.h"

static void btnPrincipalGoToRunningClick(lv_event_t * e);
static void btnPrincipalGoToConfigClick(lv_event_t * e);

static void dropdownConfigSelectedChanged(lv_event_t * e);
static void btnConfigSaveClick(lv_event_t * e);
static void btnConfigBackClick(lv_event_t * e);

static void btnRunningDetenerClick(lv_event_t * e);


template<typename T>
inline void buildDropdown(lv_obj_t* dropdown, const T* options, int count) {
  std::string items;
  for(int i = 0; i < count; i++) {
      items += options[i].label;
      if(i < count - 1) items += "\n";
  }
  lv_dropdown_set_options(dropdown, items.c_str());
}

/* ============================================================
 *  BASE CONTROLLER
 * ============================================================ */
class BaseScreenController : public IScreen {
  public:
    BaseScreenController(lv_obj_t* root, ScreenType type, const char* name) : _root(root), _type(type), _name(name) {}

    void init() override {}

    void show() override {
      if (_root) {
        lv_disp_load_scr(_root);
      }
    }

    void hide() override {}

    void update() override {}

    lv_obj_t* getRoot() override {
      return _root;
    }

    ScreenType getType() const override {
      return _type;
    }

    void setScreenListener(IScreenListener* listener) override {
      _listener = listener;
    }

    const char* getName() const {
      return _name;
    }

  protected:
    lv_obj_t* _root = nullptr;
    IScreenListener* _listener = nullptr;
    ScreenType _type;
    const char* _name;
};

/* ============================================================
 *  SPLASH
 * ============================================================ */
class SplashController : public BaseScreenController {
  public:
    SplashController();
    void show() override;
    void update() override;

  private:
    unsigned long _startTime = 0;
    bool _launched = false;
};

inline SplashController::SplashController() : BaseScreenController(ui_Splash, ScreenType::SPLASH, "splash") {}

inline void SplashController::show() {
  BaseScreenController::show();
  _startTime = millis();
  _launched = false;
}

inline void SplashController::update() {
  unsigned long now = millis();
  if(now - _startTime > 4000) {
    if(_listener == nullptr) return;
    if(_launched) return;
    _launched = true;
    ScreenEvent ev;
    ev.type = ScreenType::SPLASH;
    ev.name = EventName::Timeout;
    _listener->onScreenEvent(ev);
  }
}

/* ============================================================
 *  PRINCIPAL
 * ============================================================ */
class PrincipalController : public BaseScreenController {
  public:
    PrincipalController(SystemData& data);
    void init() override;
    void show() override;
    void update() override;
    void goToConfig();
    void goToRunning();
  private:
    SystemData& _data;
};

inline PrincipalController::PrincipalController(SystemData& data) : BaseScreenController(ui_Principal, ScreenType::PRINCIPAL, "principal"), _data(data) {}

inline void PrincipalController::init() {
  lv_obj_add_event_cb(ui_PrincipalBtnIniciar, btnPrincipalGoToRunningClick, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(ui_PrincipalBtnConfiguracion, btnPrincipalGoToConfigClick, LV_EVENT_CLICKED, this);
}

inline void PrincipalController::show() {
  BaseScreenController::show();
  lv_label_set_text(ui_PrincipalObjetivoCampoValue, _data.principal.targetFieldIntensity);
  lv_label_set_text(ui_PrincipalObjetivoFrecuenciaValue, _data.principal.targetFieldFrequency);
  lv_label_set_text(ui_PrincipalObjetivoDuracionValue, _data.principal.targetDuration);
  lv_label_set_text(ui_PrincipalTolCampoValue, _data.principal.fieldIntensityTolerance);
  lv_label_set_text(ui_PrincipalRangoTempNormalValue, _data.principal.normalTemperatureRange);
  lv_label_set_text(ui_PrincipalRangoTempCritValue, _data.principal.criticalTemperatureRange);
}

inline void PrincipalController::update() {}

inline void PrincipalController::goToConfig() {
  if(_listener == nullptr) return;
  ScreenEvent ev;
  ev.type = ScreenType::PRINCIPAL;
  ev.name = EventName::GoToConfig;
  _listener->onScreenEvent(ev);
}

inline void PrincipalController::goToRunning() {
  if(_listener == nullptr) return;
  ScreenEvent ev;
  ev.type = ScreenType::PRINCIPAL;
  ev.name = EventName::Start;
  _listener->onScreenEvent(ev);
}

inline void btnPrincipalGoToRunningClick(lv_event_t * e) {
    PrincipalController * self = (PrincipalController *) lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("BtnIniciar clicked!");
        self->goToRunning();
    }
}

inline void btnPrincipalGoToConfigClick(lv_event_t * e) {
    PrincipalController * self = (PrincipalController *) lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        Serial.println("BtnConfiguracion clicked!");
        self->goToConfig();
    }
}

/* ============================================================
 *  CONFIGURACION
 * ============================================================ */
class ConfigurationController : public BaseScreenController {
  public:
    ConfigurationController(SystemData& data);
    void show() override;
    void update() override;
    void init() override;
    void onDropdownChanged(lv_event_t * e);
    void onSave();
    void onBack();
  private:
    SystemData& _data;
    
    uint8_t _lastOptionFieldIntensity = 0;
    uint8_t _lastOptionFrequency = 0;
    uint8_t _lastOptionDuration = 0;
    uint8_t _lastOptionTolFieldIntensity = 0;
    uint8_t _lastOptionRangeNormalTemperature = 0;
    uint8_t _lastOptionRangeCriticalTemperature = 0;    
  
  private:
    void _applyState();
};

inline ConfigurationController::ConfigurationController(SystemData& data) : BaseScreenController(ui_Configuracion, ScreenType::CONFIG, "configuration"), _data(data) {}

inline void ConfigurationController::show() {
  BaseScreenController::show();
  _applyState();
}

inline void ConfigurationController::update() {}

inline void ConfigurationController::init() {
  BaseScreenController::init();
  buildDropdown(ui_DuracionOpciones, ConfigurationOptions::optionsDuration, 6);
  buildDropdown(ui_CampoOpciones, ConfigurationOptions::optionsFieldIntensity, 2);
  buildDropdown(ui_FrecuenciaOpciones, ConfigurationOptions::optionsFrequency, 2);
  buildDropdown(ui_ToleranciaCampoOpciones, ConfigurationOptions::optionsTolFieldIntensity, 3);
  buildDropdown(ui_RangoTempNormalOpciones, ConfigurationOptions::optionsRangeNormalTemperature, 3);
  buildDropdown(ui_RangoTempCritOpciones, ConfigurationOptions::optionsRangeCriticalTemperature, 3);

  // Callbacks dropdown (wrapper estático)
  lv_obj_add_event_cb(ui_DuracionOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_CampoOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_FrecuenciaOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ToleranciaCampoOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_RangoTempNormalOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_RangoTempCritOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_add_event_cb(ui_ConfiguracionGuardarBtn, btnConfigSaveClick, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(ui_ConfiguracionVolverBtn, btnConfigBackClick, LV_EVENT_CLICKED, this);

}

inline void ConfigurationController::_applyState() {
  // Aplicar a UI
  lv_dropdown_set_selected(ui_DuracionOpciones, _data.configuration.targetDurationOption);
  lv_dropdown_set_selected(ui_CampoOpciones, _data.configuration.targetFieldIntensityOption);  
  lv_dropdown_set_selected(ui_FrecuenciaOpciones, _data.configuration.targetFieldFrequencyOption);
  lv_dropdown_set_selected(ui_ToleranciaCampoOpciones, _data.configuration.fieldIntensityToleranceOption);
  lv_dropdown_set_selected(ui_RangoTempNormalOpciones, _data.configuration.normalTemperatureRangeOption);
  lv_dropdown_set_selected(ui_RangoTempCritOpciones, _data.configuration.criticalTemperatureRangeOption);

  // Sincronizar cache local
  _lastOptionFieldIntensity = _data.configuration.targetFieldIntensityOption;
  _lastOptionFrequency = _data.configuration.targetFieldFrequencyOption;
  _lastOptionDuration = _data.configuration.targetDurationOption;
  _lastOptionTolFieldIntensity = _data.configuration.fieldIntensityToleranceOption;
  _lastOptionRangeNormalTemperature = _data.configuration.normalTemperatureRangeOption;
  _lastOptionRangeCriticalTemperature = _data.configuration.criticalTemperatureRangeOption;
  
}

inline void ConfigurationController::onDropdownChanged(lv_event_t * e) {
  lv_obj_t* widget = static_cast<lv_obj_t*>(lv_event_get_target(e));
  uint16_t optionSelectedIndex = lv_dropdown_get_selected(widget);
  if(widget == ui_DuracionOpciones) {
    _lastOptionDuration = optionSelectedIndex;
  }
  else if(widget == ui_CampoOpciones) {
    _lastOptionFieldIntensity = optionSelectedIndex;
  }
  else if(widget == ui_FrecuenciaOpciones) {
    _lastOptionFrequency = optionSelectedIndex;
  }
  else if(widget == ui_ToleranciaCampoOpciones) {
    _lastOptionTolFieldIntensity = optionSelectedIndex;
  }
  else if(widget == ui_RangoTempNormalOpciones) {
    _lastOptionRangeNormalTemperature = optionSelectedIndex;
  }
  else if(widget == ui_RangoTempCritOpciones) {
    _lastOptionRangeCriticalTemperature = optionSelectedIndex;
  }

}

inline void ConfigurationController::onSave() {
  _data.configuration.targetFieldIntensityOption = _lastOptionFieldIntensity;
  _data.configuration.targetFieldFrequencyOption = _lastOptionFrequency;
  _data.configuration.targetDurationOption = _lastOptionDuration;
  _data.configuration.fieldIntensityToleranceOption = _lastOptionTolFieldIntensity;
  _data.configuration.normalTemperatureRangeOption = _lastOptionRangeNormalTemperature;
  _data.configuration.criticalTemperatureRangeOption = _lastOptionRangeCriticalTemperature;
  _data.updatePrincipalConfigurationLabels();

  if (_listener == nullptr)
    return;

  ScreenEvent ev;
  ev.type = ScreenType::CONFIG;
  ev.name = EventName::Save;
  _listener->onScreenEvent(ev);
}

inline void ConfigurationController::onBack() {
  if(_listener == nullptr) return;
  ScreenEvent ev;
  ev.type = ScreenType::CONFIG;
  ev.name = EventName::Back;
  _listener->onScreenEvent(ev);
}

static void dropdownConfigSelectedChanged(lv_event_t * e) {
  ConfigurationController * self = (ConfigurationController *) lv_event_get_user_data(e);
  if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    self->onDropdownChanged(e);
  }
}

static void btnConfigSaveClick(lv_event_t * e) {
    ConfigurationController * self = (ConfigurationController *) lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
      self->onSave();
    }
}

static void btnConfigBackClick(lv_event_t * e) {
  ConfigurationController * self = (ConfigurationController *) lv_event_get_user_data(e);
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    self->onBack();  
  }
}


/* ============================================================
 *  RUNNING
 * ============================================================ */
class RunningController : public BaseScreenController {
  public:
    RunningController(SystemData& data);
    void show() override;
    void update() override;
    void init() override;
    void onStop();
  private:
    SystemData& _data;
    unsigned long _lastProgressUpdate = 0;
    void _loadTargetsData();
    void _updateMeasuresData();
    void _updateProgressData();
    void _updateAlertsData();
    void _updateHealthData();
    void _applyAlertPanel(
        const AlertData& alert,
        lv_obj_t* panel,
        lv_obj_t* critImg,
        lv_obj_t* warningImg,
        lv_obj_t* typeText,
        lv_obj_t* sourceText,
        lv_obj_t* countText,
        lv_obj_t* maxText,
        lv_obj_t* lastUpdateText
    );
};

inline RunningController::RunningController(SystemData& data) : BaseScreenController(ui_Running, ScreenType::RUNNING, "running"), _data(data) {}

inline void RunningController::init() {
  BaseScreenController::init();
  lv_bar_set_range(ui_TotalProgressBar, 0, 100);
  lv_obj_add_event_cb(ui_TabPageProgressBtnDetener, btnRunningDetenerClick, LV_EVENT_CLICKED, this);

  // Los 3 iconos de salud se crean sin flag de visibilidad (quedarian
  // superpuestos) -- arrancan en NORMAL por defecto, _updateHealthData
  // corrige al vuelo con el dato real apenas se muestra la pantalla.
  lv_obj_add_flag(ui_HealthWarningImg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_HealthCriticalImg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(ui_HealthNormalImg, LV_OBJ_FLAG_HIDDEN);

  // Paneles de alertas: arrancan ocultos, solo se muestran (de a uno) cuando
  // hay una flag real que dibujar en ese slot -- ver _applyAlertPanel.
  lv_obj_add_flag(ui_Alert1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Alert2, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Alert3, LV_OBJ_FLAG_HIDDEN);
}

inline void RunningController::onStop() {
  if(_listener == nullptr) return;
  ScreenEvent ev;
  ev.type = ScreenType::RUNNING;
  ev.name = EventName::Stop;
  _listener->onScreenEvent(ev);
}

inline void RunningController::show() {
  BaseScreenController::show();
  // tab objetivos
  _loadTargetsData();
  // tab progreso
  _updateProgressData();
  _lastProgressUpdate = millis();
  // tab measures
  _updateMeasuresData();
  // tab alertas
  _updateAlertsData();
  // estado de salud
  _updateHealthData();
}

inline void RunningController::_loadTargetsData() {
  lv_label_set_text(ui_RunningObjetivoIntensidadValue, _data.principal.targetFieldIntensity);
  lv_label_set_text(ui_RunningObjetivoFreqValue, _data.principal.targetFieldFrequency);
  lv_label_set_text(ui_RunningObjetivoDuracionValue, _data.principal.targetDuration);
  lv_label_set_text(ui_RunningRangoIntensidadValue, _data.principal.fieldIntensityTolerance);
  lv_label_set_text(ui_RunningRangoTempNormalValue, _data.principal.normalTemperatureRange);
  lv_label_set_text(ui_RunningRangoTempCriticaValue, _data.principal.criticalTemperatureRange);
}

inline void RunningController::_updateMeasuresData() {
  static float measureMagneticField = 0.0f;
  static float measureTemperature = 0.0f;
  static float measureCurrent = 0.0f;

  if(measureMagneticField != _data.measures.measureMagneticField) {
    measureMagneticField = _data.measures.measureMagneticField;
    char magneticFieldStr[16];
    snprintf(magneticFieldStr, sizeof(magneticFieldStr), "%.2f", _data.measures.measureMagneticField);
    lv_label_set_text(ui_MedicionCampoMagneticoValue, magneticFieldStr);
  }
  
  if(measureTemperature != _data.measures.measureTemperature) {
    measureTemperature = _data.measures.measureTemperature;
    char temperatureStr[16];
    snprintf(temperatureStr, sizeof(temperatureStr), "%.1f", _data.measures.measureTemperature);
    lv_label_set_text(ui_MedicionTempValue, temperatureStr);
  }
  
  if(measureCurrent != _data.measures.measureCurrent) {
    measureCurrent = _data.measures.measureCurrent;
    char currentStr[16];
    snprintf(currentStr, sizeof(currentStr), "%.1f", _data.measures.measureCurrent);
    lv_label_set_text(ui_MedicionCorrienteValue, currentStr);
  }

  if(measureMagneticField != 0.0f) {
    lv_label_set_text(ui_MedicionCampoMagneticoLastUpdate, _data.latestUpdateStr(_data.measures.latestMagneticFieldUpdate));
  }
  else {
    lv_label_set_text(ui_MedicionCampoMagneticoLastUpdate, "Sin datos");
  }

  if(measureTemperature != 0.0f) {
    lv_label_set_text(ui_MedicionTempLastUpdate, _data.latestUpdateStr(_data.measures.latestTemperatureUpdate));
  }
  else {
    lv_label_set_text(ui_MedicionTempLastUpdate, "Sin datos");
  }
  
  if(measureCurrent != 0.0f) {
    lv_label_set_text(ui_MedicionCorrienteLastUpdate, _data.latestUpdateStr(_data.measures.latestCurrentUpdate));
  }
  else {
    lv_label_set_text(ui_MedicionCorrienteLastUpdate, "Sin datos");
  }
  
}

inline void RunningController::_updateProgressData() {
  float percent = _data.progressPercent();
  char progressPercentStr[8];
  snprintf(progressPercentStr, sizeof(progressPercentStr), "%.1f%%", percent);
  lv_label_set_text(ui_TotalProgressValue, progressPercentStr);
  lv_label_set_text(ui_ElapsedTimeValue, _data.elapsedTime());
  lv_bar_set_value(ui_TotalProgressBar, (int)percent, LV_ANIM_OFF);
  lv_arc_set_value(ui_MedicionTemperaturaArc, (int)_data.measures.measureTemperature);
  lv_arc_set_value(ui_MedicionCorrienteArc, (int)(_data.measures.measureCurrent * 10));
  lv_arc_set_value(ui_MedicionCampoMagneticoArc, (int)(_data.measures.measureMagneticField) * 10);
}

inline void RunningController::_applyAlertPanel(
    const AlertData& alert,
    lv_obj_t* panel,
    lv_obj_t* critImg,
    lv_obj_t* warningImg,
    lv_obj_t* typeText,
    lv_obj_t* sourceText,
    lv_obj_t* countText,
    lv_obj_t* maxText,
    lv_obj_t* lastUpdateText
) {
  if (!alert.active) {
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);

  bool isCritical = strcmp(alert.type, "critical") == 0;
  if (isCritical) {
    lv_obj_remove_flag(critImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(warningImg, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(critImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(warningImg, LV_OBJ_FLAG_HIDDEN);
  }

  const char* typeLabel = "?";
  if (strcmp(alert.type, "critical") == 0) {
    typeLabel = "CRITICO";
  }
  else if (strcmp(alert.type, "streak") == 0) {
    typeLabel = "RACHA";
  }
  else if (strcmp(alert.type, "frequency") == 0) {
    typeLabel = "FRECUENCIA";
  }
  lv_label_set_text(typeText, typeLabel);
  lv_label_set_text(sourceText, alert.source);

  char countStr[8];
  snprintf(countStr, sizeof(countStr), "%d", alert.count);
  lv_label_set_text(countText, countStr);

  char maxStr[8];
  snprintf(maxStr, sizeof(maxStr), "%d", alert.limit);
  lv_label_set_text(maxText, maxStr);

  lv_label_set_text(lastUpdateText, _data.latestUpdateStr(alert.lastUpdate));
}

inline void RunningController::_updateAlertsData() {
  _applyAlertPanel(
      _data.alerts.items[0],
      ui_Alert1,
      ui_Alert1CritImg, ui_Alert1WarningImg,
      ui_Alert1TypeAlertText, ui_Alert1SourceText,
      ui_Alert1StatsCountText, ui_Alert1StatsMaxText,
      ui_Alert1LastUpdateText
  );
  _applyAlertPanel(
      _data.alerts.items[1],
      ui_Alert2,
      ui_Alert2CritImg, ui_Alert2WarningImg,
      ui_Alert2TypeAlertText, ui_Alert2SourceText,
      ui_Alert2StatsCountText, ui_Alert2StatsMaxText,
      ui_Alert2LastUpdateText
  );
  _applyAlertPanel(
      _data.alerts.items[2],
      ui_Alert3,
      ui_Alert3CritImg, ui_Alert2WarningImg1,
      ui_Alert3TypeAlertText, ui_Alert3SourceText,
      ui_Alert3StatsCountText, ui_Alert3StatsMaxText,
      ui_Alert3LastUpdateText
  );
}

inline void RunningController::_updateHealthData() {
  const char* health = _data.progress.health;

  bool isCritical = strcmp(health, HealthData::Critical) == 0;
  bool isWarning  = strcmp(health, HealthData::Warning) == 0;

  const char* label = "NORMAL";
  if (isCritical) {
    label = "CRITICO";
  }
  else if (isWarning) {
    label = "ADVERTENCIA";
  }
  lv_label_set_text(ui_HealthEstado, label);

  if (isCritical) {
    lv_obj_remove_flag(ui_HealthCriticalImg, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(ui_HealthCriticalImg, LV_OBJ_FLAG_HIDDEN);
  }

  if (isWarning) {
    lv_obj_remove_flag(ui_HealthWarningImg, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(ui_HealthWarningImg, LV_OBJ_FLAG_HIDDEN);
  }

  if (!isCritical && !isWarning) {
    lv_obj_remove_flag(ui_HealthNormalImg, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(ui_HealthNormalImg, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void RunningController::update() {
  _updateMeasuresData();
  _updateAlertsData();
  _updateHealthData();

  // El progreso (%, elapsed) no necesita refrescarse en cada tick de
  // pantalla (50ms) -- un experimento se mide en minutos, no en decimas de
  // segundo. Se throttlea a 1s, igual que los envios por Serial del resto
  // del proyecto.
  unsigned long now = millis();
  if (now - _lastProgressUpdate >= 1000) {
    _lastProgressUpdate = now;
    _updateProgressData();
  }
}

static void btnRunningDetenerClick(lv_event_t * e) {
  RunningController * self = (RunningController *) lv_event_get_user_data(e);
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    self->onStop();
  }
}

/* ============================================================
 *  RESULTADO
 * ============================================================ */
class ResultadoController : public BaseScreenController {
  public:
    ResultadoController();
    void show() override;
    void update() override;

  private:

};

inline ResultadoController::ResultadoController() : BaseScreenController(ui_Resultado, ScreenType::RESULT, "result") {}

inline void ResultadoController::show() {
  BaseScreenController::show();
}

inline void ResultadoController::update() {}

/* ============================================================
 * BUSY
 * ============================================================ */
class BusyController : public BaseScreenController {
  public:
    BusyController(SystemData& data);
    void show() override;
    void update() override;
  private:
    unsigned long _startTime = 0;
    bool _launched = false;
    SystemData& _data;
};

inline BusyController::BusyController(SystemData& data) : BaseScreenController(ui_Busy, ScreenType::BUSY, "busy"), _data(data) {}

inline void BusyController::show() {
  lv_label_set_text(ui_BusyMensaje, _data.busy.messageProcess);
  BaseScreenController::show();
  _startTime = millis();
  _launched = false;
}

inline void BusyController::update() {
  unsigned long now = millis();
  if(now - _startTime > 6000) {
    if(_listener == nullptr) return;
    if(_launched) return;
    ScreenEvent ev;
    ev.type = ScreenType::BUSY;
    ev.name = EventName::Timeout;
    _launched = true;
    _listener->onScreenEvent(ev);
  }
}
