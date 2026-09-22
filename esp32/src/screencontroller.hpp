#pragma once

#include "eventsname.hpp"
#include "screenmanager.hpp"
#include "configurationoptions.hpp"
#include "configloader.hpp"
#include "intervals.hpp"
#include "systemdata.hpp"
#include "iscreen.hpp"
#include "ui.h"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_SCREENCONTROLLER = true;

static void btnPrincipalGoToRunningClick(lv_event_t * e);
static void btnPrincipalGoToConfigClick(lv_event_t * e);

static void dropdownConfigSelectedChanged(lv_event_t * e);
static void btnConfigSaveClick(lv_event_t * e);
static void btnConfigBackClick(lv_event_t * e);

static void btnRunningDetenerClick(lv_event_t * e);

static void btnResultadoVolverClick(lv_event_t * e);
static void btnResultadoRepetirClick(lv_event_t * e);


// "count" es el contador real de cada menu (ConfigurationOptions::countXxx),
// no un literal escrito a mano en el call site ni sizeof(options)/sizeof(...).
// Antes era un literal y ya se desincronizo una vez: la tolerancia de CEM
// quedo en 3 despues de sacarle una opcion y buildDropdown leia fuera del
// arreglo (Trello MOD-025). Con las listas viniendo de la SD un literal
// directamente no puede funcionar: el largo recien se conoce en runtime.
template<typename T>
inline void buildDropdown(lv_obj_t* dropdown, const T* options, int count) {
  std::string items;
  for(int i = 0; i < count; i++) {
      items += options[i].label;
      if(i < count - 1) items += "\n";
  }
  lv_dropdown_set_options(dropdown, items.c_str());
}

// Cada indicador son DOS iconos superpuestos en el mismo lugar, uno para
// cada estado: se muestra el que corresponde y se oculta el otro. No hay un
// tercer estado "no se sabe" -- el export no tiene icono para eso, y mientras
// la placa esta encendida los cuatro valores siempre estan definidos.
inline void applyStatusIcon(lv_obj_t* okIcon, lv_obj_t* failIcon, bool ok) {
  if (ok) {
    lv_obj_remove_flag(okIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(failIcon, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(okIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(failIcon, LV_OBJ_FLAG_HIDDEN);
  }
}

// Los cuatro valores tal como se aplicaron la ultima vez, para no tocar ocho
// banderas de visibilidad en cada tick de 50 ms. `applied` arranca en false
// para que la primera pasada escriba si o si: el export deja los cuatro
// iconos "Ok" ocultos, o sea los cuatro indicadores en rojo, y sin esa
// primera pasada la pantalla mentiria hasta el primer cambio de estado.
struct StatusIconsCache {
  bool applied = false;
  bool serialOk = false;
  bool wifiOk = false;
  bool brokerOk = false;
  bool sdOk = false;

  bool matches(const CommunicationData& data) const {
    return applied
        && serialOk == data.serialOk
        && wifiOk == data.wifiOk
        && brokerOk == data.brokerOk
        && sdOk == data.sdOk;
  }

  void remember(const CommunicationData& data) {
    applied = true;
    serialOk = data.serialOk;
    wifiOk = data.wifiOk;
    brokerOk = data.brokerOk;
    sdOk = data.sdOk;
  }
};

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

inline SplashController::SplashController() : BaseScreenController(ui_SplashScreen, ScreenType::SPLASH, "splash") {}

inline void SplashController::show() {
  BaseScreenController::show();
  _startTime = millis();
  _launched = false;
}

inline void SplashController::update() {
  unsigned long now = millis();
  if(now - _startTime > Intervals::SplashTimeout) {
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
    StatusIconsCache _statusIcons;
    void _updateStatusIcons();
};

inline PrincipalController::PrincipalController(SystemData& data) : BaseScreenController(ui_PrincipalScreen, ScreenType::PRINCIPAL, "principal"), _data(data) {}

inline void PrincipalController::init() {
  lv_obj_add_event_cb(ui_ButtonPrincipalIniciar, btnPrincipalGoToRunningClick, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(ui_ButtonPrincipalConfigurar, btnPrincipalGoToConfigClick, LV_EVENT_CLICKED, this);
}

inline void PrincipalController::show() {
  BaseScreenController::show();
  // El diseno muestra el numero grande y la unidad chica en widgets
  // separados; la unidad ya viene fija del export (mT, Hz, h/m, %, C).
  lv_label_set_text(ui_PrincipalIntensidadCampoValor, _data.principal.intensityValue);
  lv_label_set_text(ui_PrincipalFrecuenciaValor, _data.principal.frequencyValue);
  lv_label_set_text(ui_PrincipalDuracionHorasValor, _data.principal.durationHours);
  lv_label_set_text(ui_PrincipalDuracionMinutosValor, _data.principal.durationMinutes);
  lv_label_set_text(ui_PrincipalTolIntensidadValor, _data.principal.toleranceValue);
  lv_label_set_text(ui_PrincipalTempNormalValor, _data.principal.normalTemperatureValue);
  lv_label_set_text(ui_PrincipalTempCriticaValor, _data.principal.criticalTemperatureValue);
  // El diseno UiFinalParte1 muestra solo el modo ("CAMPO X"/"CAMPO NULO");
  // el grupo (tratado/control) y los chips de componentes del diseno
  // anterior ya no tienen widget.
  lv_label_set_text(ui_PrincipalModoValor, _data.principal.fieldModeValue);

  _updateStatusIcons();
}

// La configuracion de esta pantalla solo cambia en Configuraciones, que
// vuelve pasando por show(); los indicadores de conexion, en cambio, cambian
// MIENTRAS la pantalla esta a la vista. Por eso update() dejo de estar vacio.
inline void PrincipalController::update() {
  _updateStatusIcons();
}

inline void PrincipalController::_updateStatusIcons() {
  if (_statusIcons.matches(_data.communication)) return;
  _statusIcons.remember(_data.communication);

  applyStatusIcon(ui_PrincipalMegaOk, ui_PrincipalMegaNo, _data.communication.serialOk);
  applyStatusIcon(ui_PrincipalWifiOk, ui_PrincipalWifiNo, _data.communication.wifiOk);
  applyStatusIcon(ui_PrincipalBrokerOk, ui_PrincipalBrokerNo, _data.communication.brokerOk);
  applyStatusIcon(ui_PrincipalSdOk, ui_PrincipalSdNo, _data.communication.sdOk);
}

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
        DEBUG_PRINTLN(DEBUG_SCREENCONTROLLER, "BtnIniciar clicked!");
        self->goToRunning();
    }
}

inline void btnPrincipalGoToConfigClick(lv_event_t * e) {
    PrincipalController * self = (PrincipalController *) lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        DEBUG_PRINTLN(DEBUG_SCREENCONTROLLER, "BtnConfiguracion clicked!");
        self->goToConfig();
    }
}

/* ============================================================
 *  CONFIGURACION
 * ============================================================ */
// Los _lastOptionXxx son un staging area: cambiar un dropdown
// (onDropdownChanged) solo actualiza estos campos, NO _data.configuration.
// El valor elegido recien se aplica a _data.configuration en onSave() --
// si el operador entra a Configuraciones, cambia algo y sale por
// onBack() (Volver) en vez de Guardar, el cambio se descarta -- por
// decision de diseno, sin avisar: el diseno UiFinalParte1 no trae el
// cartel "N CAMBIOS SIN GUARDAR" del anterior y no se quiso reponerlo.
// onBack() descarta explicitamente (resincroniza el staging) en vez de
// confiar en que el proximo show() lo haga.
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

    uint8_t _lastOptionFieldMode = 0;
    uint8_t _lastOptionFieldIntensity = 0;
    uint8_t _lastOptionFrequency = 0;
    uint8_t _lastOptionDuration = 0;
    uint8_t _lastOptionTolFieldIntensity = 0;
    uint8_t _lastOptionRangeNormalTemperature = 0;
    uint8_t _lastOptionRangeCriticalTemperature = 0;

    // Los dos combos de temperatura se rellenan filtrados (ver
    // _rebuildNormalOptions), asi que la posicion dentro del combo YA NO es
    // el indice de la opcion. Estos mapas traducen posicion filtrada ->
    // indice absoluto en ConfigurationOptions, que es lo unico que se guarda
    // en _data.configuration y lo unico que entiende el resto del sistema
    // (_sendStart, SelectionStore, updatePrincipalConfigurationLabels).
    uint8_t _normalMap[ConfigurationOptions::MaxOptions];
    uint8_t _normalCount = 0;
    uint8_t _criticalMap[ConfigurationOptions::MaxOptions];
    uint8_t _criticalCount = 0;

    // "ninguna opcion elegida": el combo quedo sin seleccion porque lo que
    // estaba elegido no es compatible con lo que se eligio del otro lado.
    static constexpr uint8_t NoSelection = 0xFF;

  private:
    void _applyState();
    void _syncStagingFromData();
    void _rebuildNormalOptions();
    void _rebuildCriticalOptions();
    uint8_t _applySelection(lv_obj_t* dropdown, const uint8_t* map, uint8_t count, uint8_t absolute);
    static bool _isNested(const ConfigurationOptions::OptionRangeTemperature& normal,
                          const ConfigurationOptions::OptionRangeTemperature& critical);
};

inline ConfigurationController::ConfigurationController(SystemData& data) : BaseScreenController(ui_ConfiguracionesScreen, ScreenType::CONFIG, "configuration"), _data(data) {}

inline void ConfigurationController::show() {
  BaseScreenController::show();
  _applyState();
}

inline void ConfigurationController::update() {}

inline void ConfigurationController::init() {
  BaseScreenController::init();

  // PENDIENTE (ajuste visual, se corrige en SquareLine y se re-exporta):
  // el texto de los 7 dropdowns desborda. Los 7 salen del export con
  // lv_font_montserrat_20 mientras sus titulos usan montserrat_10, y dos
  // de ellos tienen ancho fijo -- Modo 118 px e Intensidad 90 px -- contra
  // lv_pct(100) en los otros cinco. A 20 px, "Campo nulo" (la etiqueta mas
  // larga de las compiladas) ya no entra en esos anchos.
  // Dos cosas a tener en cuenta cuando se ajuste:
  //  - No alcanza con achicar la fuente en LV_PART_MAIN: eso es el boton
  //    cerrado. La lista desplegada es otro objeto (lv_dropdown_get_list),
  //    que SquareLine no estiliza, asi que hay que tocar las dos partes o
  //    el problema se muda al desplegable.
  //  - El ancho tiene que tolerar etiquetas mas largas que las compiladas:
  //    los menus vienen de la SD y el `label` es texto libre del operador
  //    (ver ConfigLoader y docs/config-schema.md seccion 3).

  // El de modo pisa el "NORMAL/NULO" que trae el export: la lista real es
  // ConfigurationOptions::optionsFieldMode, fija en 2 entradas por diseno.
  buildDropdown(ui_ConfiguracionModoOpciones, ConfigurationOptions::optionsFieldMode, ConfigurationOptions::countFieldMode);
  buildDropdown(ui_ConfiguracionDuracionOpciones, ConfigurationOptions::optionsDuration, ConfigurationOptions::countDuration);
  buildDropdown(ui_ConfiguracionIntensidadCampoOpciones, ConfigurationOptions::optionsFieldIntensity, ConfigurationOptions::countFieldIntensity);
  buildDropdown(ui_ConfiguracionFrecuenciaOpciones, ConfigurationOptions::optionsFrequency, ConfigurationOptions::countFrequency);
  buildDropdown(ui_ConfiguracionTolIntensidadOpciones, ConfigurationOptions::optionsTolFieldIntensity, ConfigurationOptions::countTolFieldIntensity);
  // Los dos de temperatura NO se llenan aca: su contenido depende de lo que
  // este elegido en el otro, asi que los arma _applyState() en cada show().

  // Callbacks dropdown (wrapper estatico)
  lv_obj_add_event_cb(ui_ConfiguracionModoOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ConfiguracionDuracionOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ConfiguracionIntensidadCampoOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ConfiguracionFrecuenciaOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ConfiguracionTolIntensidadOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ConfiguracionTempNormalOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(ui_ConfiguracionTempCriticaOpciones, dropdownConfigSelectedChanged, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_add_event_cb(ui_ButtonConfiguracionGuardar, btnConfigSaveClick, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(ui_ButtonConfiguracionVolver, btnConfigBackClick, LV_EVENT_CLICKED, this);
}

inline void ConfigurationController::_applyState() {
  // Aplicar a UI
  lv_dropdown_set_selected(ui_ConfiguracionModoOpciones, _data.configuration.fieldModeOption);
  lv_dropdown_set_selected(ui_ConfiguracionDuracionOpciones, _data.configuration.targetDurationOption);
  lv_dropdown_set_selected(ui_ConfiguracionIntensidadCampoOpciones, _data.configuration.targetFieldIntensityOption);
  lv_dropdown_set_selected(ui_ConfiguracionFrecuenciaOpciones, _data.configuration.targetFieldFrequencyOption);
  lv_dropdown_set_selected(ui_ConfiguracionTolIntensidadOpciones, _data.configuration.fieldIntensityToleranceOption);
  _syncStagingFromData();

  // Los dos de temperatura van despues del sync porque cada uno se filtra
  // con lo que esta elegido en el otro. Un par guardado siempre es
  // compatible (onSave no deja guardar otra cosa), asi que ninguno de los
  // dos se limpia al entrar.
  _rebuildNormalOptions();
  _rebuildCriticalOptions();
}

// Deja el staging igual a lo guardado: lo que muestran los dropdowns y lo
// que se va a mandar en el proximo start vuelven a coincidir.
inline void ConfigurationController::_syncStagingFromData() {
  _lastOptionFieldMode = _data.configuration.fieldModeOption;
  _lastOptionFieldIntensity = _data.configuration.targetFieldIntensityOption;
  _lastOptionFrequency = _data.configuration.targetFieldFrequencyOption;
  _lastOptionDuration = _data.configuration.targetDurationOption;
  _lastOptionTolFieldIntensity = _data.configuration.fieldIntensityToleranceOption;
  _lastOptionRangeNormalTemperature = _data.configuration.normalTemperatureRangeOption;
  _lastOptionRangeCriticalTemperature = _data.configuration.criticalTemperatureRangeOption;
}

// El Detector del Mega da por sentado que los dos rangos estan ANIDADOS:
// criticalMin <= normalMin < normalMax <= criticalMax (ver el comentario de
// Source::addSample en mega2560/src/detector.hpp). Con esa forma "fuera de
// normal" es un superconjunto de "critico". Un par que no la cumpla -- normal
// mas ancho que critico -- haria que una lectura perfectamente normal caiga
// fuera del rango critico y dispare la alerta que corta el experimento.
//
// En vez de dejar armar un par invalido y rechazarlo recien al guardar, cada
// combo se rellena con las opciones compatibles con lo elegido en el otro: lo
// invalido no llega a estar en la lista.
inline bool ConfigurationController::_isNested(const ConfigurationOptions::OptionRangeTemperature& normal,
                                               const ConfigurationOptions::OptionRangeTemperature& critical) {
  return critical.tmin <= normal.tmin
      && normal.tmin < normal.tmax
      && normal.tmax <= critical.tmax;
}

// Deja elegida la opcion de indice ABSOLUTO `absolute` si sobrevivio al
// filtro. Si no sobrevivio, limpia la seleccion y devuelve NoSelection: lo
// que estaba elegido dejo de ser compatible, y seguir mostrandolo seria
// mentir sobre lo que se va a guardar.
inline uint8_t ConfigurationController::_applySelection(lv_obj_t* dropdown, const uint8_t* map, uint8_t count, uint8_t absolute) {
  for (uint8_t i = 0; i < count; i++) {
    if (map[i] != absolute) continue;
    // NULL = volver a mostrar la opcion elegida en vez del placeholder.
    lv_dropdown_set_text(dropdown, NULL);
    lv_dropdown_set_selected(dropdown, i);
    return absolute;
  }

  // Literal a proposito: lv_dropdown_set_text guarda el puntero, no copia.
  lv_dropdown_set_text(dropdown, "--");
  return NoSelection;
}

// Rellena el combo de rango NORMAL con los que entran dentro del rango
// CRITICO elegido (sin critico elegido, con todos).
inline void ConfigurationController::_rebuildNormalOptions() {
  std::string items;
  _normalCount = 0;
  bool hasCritical = _lastOptionRangeCriticalTemperature != NoSelection;

  for (uint8_t i = 0; i < ConfigurationOptions::countRangeNormalTemperature; i++) {
    const auto& normal = ConfigurationOptions::optionsRangeNormalTemperature[i];
    if (hasCritical &&
        !_isNested(normal, ConfigurationOptions::optionsRangeCriticalTemperature[_lastOptionRangeCriticalTemperature])) {
      continue;
    }
    if (_normalCount > 0) items += "\n";
    items += normal.label;
    _normalMap[_normalCount] = i;
    _normalCount++;
  }

  lv_dropdown_set_options(ui_ConfiguracionTempNormalOpciones, items.c_str());
  _lastOptionRangeNormalTemperature =
      _applySelection(ui_ConfiguracionTempNormalOpciones, _normalMap, _normalCount, _lastOptionRangeNormalTemperature);
}

// El espejo del anterior: los CRITICOS que contienen al NORMAL elegido.
inline void ConfigurationController::_rebuildCriticalOptions() {
  std::string items;
  _criticalCount = 0;
  bool hasNormal = _lastOptionRangeNormalTemperature != NoSelection;

  for (uint8_t i = 0; i < ConfigurationOptions::countRangeCriticalTemperature; i++) {
    const auto& critical = ConfigurationOptions::optionsRangeCriticalTemperature[i];
    if (hasNormal &&
        !_isNested(ConfigurationOptions::optionsRangeNormalTemperature[_lastOptionRangeNormalTemperature], critical)) {
      continue;
    }
    if (_criticalCount > 0) items += "\n";
    items += critical.label;
    _criticalMap[_criticalCount] = i;
    _criticalCount++;
  }

  lv_dropdown_set_options(ui_ConfiguracionTempCriticaOpciones, items.c_str());
  _lastOptionRangeCriticalTemperature =
      _applySelection(ui_ConfiguracionTempCriticaOpciones, _criticalMap, _criticalCount, _lastOptionRangeCriticalTemperature);
}

inline void ConfigurationController::onDropdownChanged(lv_event_t * e) {
  lv_obj_t* widget = static_cast<lv_obj_t*>(lv_event_get_target(e));
  uint16_t optionSelectedIndex = lv_dropdown_get_selected(widget);
  if(widget == ui_ConfiguracionModoOpciones) {
    _lastOptionFieldMode = optionSelectedIndex;
  }
  else if(widget == ui_ConfiguracionDuracionOpciones) {
    _lastOptionDuration = optionSelectedIndex;
  }
  else if(widget == ui_ConfiguracionIntensidadCampoOpciones) {
    _lastOptionFieldIntensity = optionSelectedIndex;
  }
  else if(widget == ui_ConfiguracionFrecuenciaOpciones) {
    _lastOptionFrequency = optionSelectedIndex;
  }
  else if(widget == ui_ConfiguracionTolIntensidadOpciones) {
    _lastOptionTolFieldIntensity = optionSelectedIndex;
  }
  // Los dos de temperatura se traducen por el mapa (su lista esta filtrada)
  // y rellenan el otro combo: elegir de un lado redefine que es compatible
  // del otro.
  else if(widget == ui_ConfiguracionTempNormalOpciones) {
    if (optionSelectedIndex >= _normalCount) return;
    lv_dropdown_set_text(widget, NULL);
    _lastOptionRangeNormalTemperature = _normalMap[optionSelectedIndex];
    _rebuildCriticalOptions();
  }
  else if(widget == ui_ConfiguracionTempCriticaOpciones) {
    if (optionSelectedIndex >= _criticalCount) return;
    lv_dropdown_set_text(widget, NULL);
    _lastOptionRangeCriticalTemperature = _criticalMap[optionSelectedIndex];
    _rebuildNormalOptions();
  }
}

inline void ConfigurationController::onSave() {
  // Con el filtrado un par incompatible ya no se puede armar; lo que si
  // puede pasar es que uno de los dos combos haya quedado SIN seleccion,
  // porque lo elegido del otro lado dejo afuera lo que estaba. Guardar eso
  // escribiria un indice que no eligio nadie.
  if (_lastOptionRangeNormalTemperature == NoSelection ||
      _lastOptionRangeCriticalTemperature == NoSelection) {
    // Silencioso a proposito: no se guarda ni se navega, y el unico rastro
    // queda en el log serie. El layout no tiene renglon de error propio y no
    // se quiso reusar otro widget para esto; lo que ve el operador es que
    // sigue en la pantalla con un combo en "--".
    DEBUG_PRINTLN(DEBUG_SCREENCONTROLLER, "[CONFIG] falta elegir un rango de temperatura: no se guarda");
    return;
  }

  _data.configuration.fieldModeOption = _lastOptionFieldMode;
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
  // Descarte explicito: lo elegido en los dropdowns y no guardado se
  // pierde aca, no en el proximo show().
  _syncStagingFromData();

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
 *  RUNNING (pantalla "En curso", ui_EnCursoScreen)
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
    StatusIconsCache _statusIcons;

    // Cache de lo ya escrito en los dos labels de medicion, para no
    // reescribirlos (y repintarlos) en cada tick de 50 ms. Es miembro y no
    // static de funcion: un static sobrevive entre experimentos y haria que
    // el segundo no refresque si arranca con el mismo valor con que termino
    // el primero. show() los vuelve a NAN, que nunca compara igual, asi que
    // al entrar a la pantalla siempre se escribe.
    float _shownMagneticField = NAN;
    float _shownTemperature = NAN;

    // 0xFF = todavia no se aplico ninguna distribucion de filas. No sirve
    // arrancar en 0: el export trae las 4 filas visibles, asi que "0 filas"
    // tambien hay que aplicarlo una primera vez.
    uint8_t _visibleCoils = 0xFF;

    void _loadTargetsData();
    void _updateMeasuresData();
    void _updateCoilsData();
    void _updateProgressData();
    void _updateAlertsData();
    void _updateHealthData();
    void _applyAlertDots(const char* source, lv_obj_t* const dots[MAX_ALERT_DOTS]);
    void _updateStatusIcons();
    uint8_t _resolveVisibleCoils() const;
    void _applyCoilVisibility();
};

inline RunningController::RunningController(SystemData& data) : BaseScreenController(ui_EnCursoScreen, ScreenType::RUNNING, "running"), _data(data) {}

inline void RunningController::init() {
  BaseScreenController::init();
  lv_obj_add_event_cb(ui_ButtonDetener, btnRunningDetenerClick, LV_EVENT_CLICKED, this);

  // Los 3 chips de salud se exportan visibles uno al lado del otro; arrancan
  // en NORMAL, _updateHealthData los corrige apenas se muestra la pantalla.
  lv_obj_add_flag(ui_EnCursoHeaderEstadoAdvertencia, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_EnCursoHeaderEstadoCritico, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(ui_EnCursoHeaderEstadoNormal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* const bars[MAX_COILS] = { ui_EnCursoMedicionesBobina1ControlBar, ui_EnCursoMedicionesBobina2ControlBar, ui_EnCursoMedicionesBobina3ControlBar, ui_EnCursoMedicionesBobina4ControlBar };
  for (uint8_t i = 0; i < MAX_COILS; i++) {
    lv_bar_set_range(bars[i], 0, 100);
  }
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

  // NAN nunca compara igual, asi que _updateMeasuresData escribe si o si:
  // al entrar a la pantalla los valores mostrados arrancan en 0 (o en la
  // ultima muestra recibida), nunca en el texto de maqueta del export.
  _shownMagneticField = NAN;
  _shownTemperature = NAN;

  // Cuantas bobinas hay se decide al entrar, no en cada tick: la respuesta
  // no cambia durante un experimento.
  _applyCoilVisibility();

  // tab objetivos
  _loadTargetsData();
  // barra inferior (avance, restante)
  _updateProgressData();
  _lastProgressUpdate = millis();
  // tab mediciones
  _updateMeasuresData();
  _updateCoilsData();
  _updateAlertsData();
  // estado de salud
  _updateHealthData();
  // conexiones (serie con el Mega, WiFi, broker, SD)
  _updateStatusIcons();
}

// Mismos cuatro indicadores que Principal y Resultado, con los widgets de
// esta pantalla. Se refrescan en cada tick porque la conexion puede caerse
// en medio del experimento, que es justo cuando mas importa verlo.
inline void RunningController::_updateStatusIcons() {
  if (_statusIcons.matches(_data.communication)) return;
  _statusIcons.remember(_data.communication);

  applyStatusIcon(ui_EnCursoMegaOk, ui_EnCursoMegaNo, _data.communication.serialOk);
  applyStatusIcon(ui_EnCursoWifiOk, ui_EnCursoWifiNo, _data.communication.wifiOk);
  applyStatusIcon(ui_EnCursoBrokerOk, ui_EnCursoBrokerNo, _data.communication.brokerOk);
  applyStatusIcon(ui_EnCursoSdOk, ui_EnCursoSdNo, _data.communication.sdOk);
}

// Mismos valores sueltos que muestra Principal (numero grande + unidad chica
// en widgets separados), formateados desde los campos numericos de la opcion.
inline void RunningController::_loadTargetsData() {
  lv_label_set_text(ui_EnCursoObjetivosIntensidadValor, _data.principal.intensityValue);
  lv_label_set_text(ui_EnCursoObjetivosFrecuenciaValor, _data.principal.frequencyValue);
  lv_label_set_text(ui_EnCursoObjetivosDuracionHorasValor, _data.principal.durationHours);
  lv_label_set_text(ui_EnCursoObjetivosDuracionMinutosValor, _data.principal.durationMinutes);
  lv_label_set_text(ui_EnCursoObjetivosTempNormalValor, _data.principal.normalTemperatureValue);
  lv_label_set_text(ui_EnCursoObjetivosTempCriticaValor, _data.principal.criticalTemperatureValue);
  lv_label_set_text(ui_EnCursoObjetivosTolIntensidadValor, _data.principal.toleranceValue);
  lv_label_set_text(ui_EnCursoObjetivosModoValor, _data.principal.fieldModeValue);
}

inline void RunningController::_updateMeasuresData() {
  if(_shownMagneticField != _data.measures.measureMagneticField) {
    _shownMagneticField = _data.measures.measureMagneticField;
    char magneticFieldStr[16];
    snprintf(magneticFieldStr, sizeof(magneticFieldStr), "%.2f", _data.measures.measureMagneticField);
    lv_label_set_text(ui_EnCursoMedicionesIntensidadValor, magneticFieldStr);
  }

  if(_shownTemperature != _data.measures.measureTemperature) {
    _shownTemperature = _data.measures.measureTemperature;
    char temperatureStr[16];
    snprintf(temperatureStr, sizeof(temperatureStr), "%.1f", _data.measures.measureTemperature);
    lv_label_set_text(ui_EnCursoMedicionesTempValor, temperatureStr);
  }
}

// Cuantas bobinas dibujar. Manda la seccion `coils` de la SD -- es la que
// describe el gabinete real -- contando solo las habilitadas: una bobina con
// enabled:false no recibe PWM y no hay nada que mostrarle.
//
// Sin tarjeta o sin esa seccion, la ESP32 no tiene forma propia de saberlo y
// cae en lo que reporte el Mega: una fila por bobina que haya llegado en un
// frame coil_data. Sin SD y sin Mega no se dibuja ninguna, que es la
// respuesta honesta -- mejor que inventar 2 porque es lo que suele registrar
// el .ino del Mega, dato que ademas vive del otro lado del cable.
inline uint8_t RunningController::_resolveVisibleCoils() const {
  uint8_t configured = 0;
  const ConfigLoader::CoilConfig* coils = ConfigLoader::coilConfigs();
  for (uint8_t i = 0; i < ConfigLoader::coilConfigCount() && i < MAX_COILS; i++) {
    if (coils[i].enabled) configured++;
  }
  if (configured > 0) return configured;

  uint8_t reported = 0;
  for (uint8_t i = 0; i < MAX_COILS; i++) {
    if (_data.measures.latestCoilUpdate[i] != 0) reported++;
  }
  return reported;
}

// Ademas de mostrar u ocultar filas escribe los NOMBRES, y por eso sale
// temprano si la cuenta no cambio: los nombres salen de la SD o son fijos,
// no cambian hasta que se reinicie el sistema, asi que reescribirlos en cada
// tick seria repintar cuatro labels por segundo para nada.
inline void RunningController::_applyCoilVisibility() {
  uint8_t resolved = _resolveVisibleCoils();
  if (resolved == _visibleCoils) return;
  _visibleCoils = resolved;

  lv_obj_t* const rows[MAX_COILS] = {
    ui_EnCursoMedicionesBobina1, ui_EnCursoMedicionesBobina2,
    ui_EnCursoMedicionesBobina3, ui_EnCursoMedicionesBobina4
  };
  lv_obj_t* const nameLabels[MAX_COILS] = {
    ui_EnCursoMedicionesBobina1Nombre, ui_EnCursoMedicionesBobina2Nombre,
    ui_EnCursoMedicionesBobina3Nombre, ui_EnCursoMedicionesBobina4Nombre
  };

  const ConfigLoader::CoilConfig* coils = ConfigLoader::coilConfigs();
  uint8_t named = ConfigLoader::coilConfigCount();

  for (uint8_t i = 0; i < MAX_COILS; i++) {
    if (i >= _visibleCoils) {
      lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    // El nombre sale de la SD si esta (el mismo con el que el Mega resuelve
    // config_coil); si no, el generico "B1".."B4". El export trae "B3" en
    // la fila 4.
    if (i < named && coils[i].name[0] != '\0') {
      lv_label_set_text(nameLabels[i], coils[i].name);
    }
    else {
      char str[8];
      snprintf(str, sizeof(str), "B%u", i + 1);
      lv_label_set_text(nameLabels[i], str);
    }

    lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
  }
}

// Una fila por bobina visible: duty aplicado y corriente, las dos del frame
// coil_data del Mega. Las dos arrancan en 0 y ahi se quedan hasta que
// llegue el primer frame -- no se muestran guiones, porque 0 es lo que
// efectivamente esta saliendo por el PWM antes de que el lazo arranque.
inline void RunningController::_updateCoilsData() {
  lv_obj_t* const currentLabels[MAX_COILS] = { ui_EnCursoMedicionesBobina1Corriente, ui_EnCursoMedicionesBobina2Corriente, ui_EnCursoMedicionesBobina3Corriente, ui_EnCursoMedicionesBobina4Corriente };
  lv_obj_t* const dutyBars[MAX_COILS] = { ui_EnCursoMedicionesBobina1ControlBar, ui_EnCursoMedicionesBobina2ControlBar, ui_EnCursoMedicionesBobina3ControlBar, ui_EnCursoMedicionesBobina4ControlBar };
  lv_obj_t* const dutyLabels[MAX_COILS] = { ui_EnCursoMedicionesBobina1ControlValor, ui_EnCursoMedicionesBobina2ControlValor, ui_EnCursoMedicionesBobina3ControlValor, ui_EnCursoMedicionesBobina4ControlValor };

  // Los nombres NO se tocan aca: los escribe _applyCoilVisibility() cuando
  // cambia la cantidad de filas, porque no cambian durante la ejecucion.
  for (uint8_t i = 0; i < _visibleCoils; i++) {
    char str[16];

    snprintf(str, sizeof(str), "%.2f A", _data.measures.coilCurrent[i]);
    lv_label_set_text(currentLabels[i], str);

    float duty = _data.measures.coilDuty[i];
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 100.0f) duty = 100.0f;
    lv_bar_set_value(dutyBars[i], (int32_t) (duty + 0.5f), LV_ANIM_OFF);
    snprintf(str, sizeof(str), "%.1f%%", duty);
    lv_label_set_text(dutyLabels[i], str);
  }
}

inline void RunningController::_updateProgressData() {
  char str[8];
  snprintf(str, sizeof(str), "%.1f%%", _data.progressPercent());
  lv_label_set_text(ui_Label1, str);   // ui_Label1 es el % de PROGRESO (sin renombrar en SquareLine)

  // TRANSCURRIDO, como lo titula el diseno UiFInalParte2 (el anterior
  // mostraba RESTANTE); es el mismo dato que congela Resultado.
  unsigned long elapsed = _data.elapsedSeconds();
  snprintf(str, sizeof(str), "%lu", elapsed / 3600);
  lv_label_set_text(ui_EnCursoTranscurridoHorasValor, str);
  snprintf(str, sizeof(str), "%02lu", (elapsed % 3600) / 60);
  lv_label_set_text(ui_EnCursoTranscurridoMinutosValor, str);
}

// Los 4 paneles que cada medicion tiene al lado del titulo son los ULTIMOS
// 4 eventos de esa fuente: el mas antiguo a la izquierda, el mas reciente a
// la derecha. Se muestran tantos como eventos haya (ninguno con 0, los 4 mas
// nuevos a partir del cuarto) y el resto se ocultan -- un panel apagado no
// se distinguiria de uno "sin alerta todavia".
//
// El color es el del PROPIO evento, no el del conjunto: rojo si ese evento
// fue critico, ambar si fue streak o frequency. Son los mismos dos colores
// que el export usa en los chips de salud del header.
inline void RunningController::_applyAlertDots(const char* source, lv_obj_t* const dots[MAX_ALERT_DOTS]) {
  const AlertHistory* history = _data.alertHistory(source);
  uint8_t count = (history != nullptr) ? history->count : 0;

  for (uint8_t i = 0; i < MAX_ALERT_DOTS; i++) {
    if (i >= count) {
      lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    bool critical = strcmp(history->types[i], "critical") == 0;
    lv_obj_set_style_bg_color(dots[i],
                              critical ? lv_color_hex(0xA30000) : lv_color_hex(0xC47F08),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
  }
}

inline void RunningController::_updateAlertsData() {
  // En UiFInalParte2 los puntos ya estan dentro del panel que nombran
  // (el export anterior los tenia cruzados).
  lv_obj_t* const cemDots[MAX_ALERT_DOTS] = { ui_EnCursoMedicionesIntensidadAlertas1, ui_EnCursoMedicionesIntensidadAlertas2, ui_EnCursoMedicionesIntensidadAlertas3, ui_EnCursoMedicionesIntensidadAlertas4 };
  lv_obj_t* const tempDots[MAX_ALERT_DOTS] = { ui_EnCursoMedicionesTempAlertas1, ui_EnCursoMedicionesTempAlertas2, ui_EnCursoMedicionesTempAlertas3, ui_EnCursoMedicionesTempAlertas4 };
  _applyAlertDots("CEM1", cemDots);
  _applyAlertDots("TEMP1", tempDots);
}

inline void RunningController::_updateHealthData() {
  const char* health = _data.progress.health;

  bool isCritical = strcmp(health, HealthData::Critical) == 0;
  bool isWarning  = strcmp(health, HealthData::Warning) == 0;

  // Un solo chip visible a la vez: los tres comparten el flex row del header.
  if (isCritical) {
    lv_obj_remove_flag(ui_EnCursoHeaderEstadoCritico, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(ui_EnCursoHeaderEstadoCritico, LV_OBJ_FLAG_HIDDEN);
  }

  if (isWarning) {
    lv_obj_remove_flag(ui_EnCursoHeaderEstadoAdvertencia, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(ui_EnCursoHeaderEstadoAdvertencia, LV_OBJ_FLAG_HIDDEN);
  }

  if (!isCritical && !isWarning) {
    lv_obj_remove_flag(ui_EnCursoHeaderEstadoNormal, LV_OBJ_FLAG_HIDDEN);
  }
  else {
    lv_obj_add_flag(ui_EnCursoHeaderEstadoNormal, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void RunningController::update() {
  _updateMeasuresData();
  _updateAlertsData();
  _updateHealthData();
  _updateStatusIcons();

  // El progreso (%, restante) y las corrientes no necesitan refrescarse en
  // cada tick de pantalla (50ms) -- un experimento se mide en minutos, no en
  // decimas de segundo. Se throttlea a 1s, igual que los envios por Serial
  // del resto del proyecto.
  unsigned long now = millis();
  if (now - _lastProgressUpdate >= 1000) {
    _lastProgressUpdate = now;
    _updateProgressData();
    // Se recalcula junto con los datos y no solo en show() por el caso sin
    // SD: ahi la cuenta sale de los frames coil_data del Mega, que todavia
    // no habian llegado al entrar a la pantalla. Es idempotente y son 4
    // banderas, asi que no vale la pena condicionarlo.
    _applyCoilVisibility();
    _updateCoilsData();
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
    ResultadoController(SystemData& data);
    void show() override;
    void update() override;
    void init() override;
    void onVolver();
    void onRepetir();

  private:
    SystemData& _data;
    StatusIconsCache _statusIcons;
    void _updateStatusIcons();
    void _applyResult();
    void _applyPanel(lv_obj_t* panel, bool visible, lv_obj_t* modeLabel, lv_obj_t* detailLabel, const char* detail);
    void _buildAlertsClause(char* out, size_t size) const;
    void _buildCriticalTexts(char* headline, size_t headlineSize, char* detail, size_t detailSize) const;
};

inline ResultadoController::ResultadoController(SystemData& data) : BaseScreenController(ui_ResultadoScreen, ScreenType::RESULT, "result"), _data(data) {}

inline void ResultadoController::init() {
  BaseScreenController::init();
  lv_obj_add_event_cb(ui_ButtonResultadoPrincipal, btnResultadoVolverClick, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(ui_ButtonResultadoRepetir, btnResultadoRepetirClick, LV_EVENT_CLICKED, this);

  // Los 3 paneles (Completado/Falla/Detenido) ocupan el mismo lugar. El
  // export deja Completado visible y los otros dos ocultos; se ocultan los
  // 3 aca para no depender de ese default, _applyResult() muestra el que
  // corresponde a _data.result.reason.
  lv_obj_add_flag(ui_ResultadoCompletado, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_ResultadoFalla, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_ResultadoDetenido, LV_OBJ_FLAG_HIDDEN);
}

inline void ResultadoController::onVolver() {
  if(_listener == nullptr) return;
  ScreenEvent ev;
  ev.type = ScreenType::RESULT;
  ev.name = EventName::Back;
  _listener->onScreenEvent(ev);
}

inline void ResultadoController::onRepetir() {
  if(_listener == nullptr) return;
  ScreenEvent ev;
  ev.type = ScreenType::RESULT;
  ev.name = EventName::Repeat;
  _listener->onScreenEvent(ev);
}

inline void ResultadoController::show() {
  BaseScreenController::show();
  _applyResult();
  _updateStatusIcons();
}

// Resultado muestra un snapshot congelado del experimento, pero los
// indicadores de conexion NO son parte de ese snapshot: son el estado de
// ahora. Si el Mega se desconecta mientras el operador lee el resultado,
// tiene que verse.
inline void ResultadoController::_updateStatusIcons() {
  if (_statusIcons.matches(_data.communication)) return;
  _statusIcons.remember(_data.communication);

  applyStatusIcon(ui_ResultadoMegaOk, ui_ResultadoMegaNo, _data.communication.serialOk);
  applyStatusIcon(ui_ResultadoWifiOk, ui_ResultadoWifiNo, _data.communication.wifiOk);
  applyStatusIcon(ui_ResultadoBrokerOk, ui_ResultadoBrokerNo, _data.communication.brokerOk);
  applyStatusIcon(ui_ResultadoSdOk, ui_ResultadoSdNo, _data.communication.sdOk);
}

// "sin alertas activas" / "1 alerta activa" / "N alertas activas". Sale del
// contador congelado al momento del corte, no de un texto fijo: un
// experimento puede completarse o ser detenido con alertas activas, y el
// texto de maqueta del export ("sin alertas activas al detener") daba por
// sentado que no.
inline void ResultadoController::_buildAlertsClause(char* out, size_t size) const {
  uint8_t count = _data.result.alertCount;
  if (count == 0) {
    snprintf(out, size, "sin alertas activas");
    return;
  }
  snprintf(out, size, "%u %s", count, count == 1 ? "alerta activa" : "alertas activas");
}

// Titular y detalle del panel de Falla, armados con los campos crudos del
// corte. El titular del export estaba clavado en "TEMP1 ALCANZO EL LIMITE
// CRITICO", que es falso en cuanto corta otra fuente (CEM1 es habilitable
// desde la SD) o corta otro tipo de regla: el limite de streak o el de
// frequency cortan igual que el de critical, cada uno por su maxEvents.
inline void ResultadoController::_buildCriticalTexts(char* headline, size_t headlineSize,
                                                     char* detail, size_t detailSize) const {
  const char* source = _data.result.source[0] != '\0' ? _data.result.source : "UNA FUENTE";

  const char* rule = "CRITICO";
  if (strcmp(_data.result.type, "streak") == 0) rule = "DE RACHA";
  else if (strcmp(_data.result.type, "frequency") == 0) rule = "DE FRECUENCIA";

  snprintf(headline, headlineSize, "%s ALCANZO EL LIMITE %s", source, rule);

  // Sin los campos crudos (Mega viejo, o frame sin ellos) queda la
  // descripcion que armo el Mega, que dice lo mismo en prosa.
  if (_data.result.limit == 0) {
    snprintf(detail, detailSize, "%s", _data.result.description);
    return;
  }

  char alerts[32];
  _buildAlertsClause(alerts, sizeof(alerts));
  snprintf(detail, detailSize, "%u/%u ocurrencias - %s",
           _data.result.count, _data.result.limit, alerts);
}

inline void ResultadoController::_applyPanel(lv_obj_t* panel, bool visible, lv_obj_t* modeLabel, lv_obj_t* detailLabel, const char* detail) {
  if (!visible) {
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(modeLabel, _data.result.fieldMode);
  lv_label_set_text(detailLabel, detail);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

inline void ResultadoController::_applyResult() {
  bool isCompleted = strcmp(_data.result.reason, "completed") == 0;
  bool isCritical  = strcmp(_data.result.reason, "critical") == 0;
  bool isStopped   = strcmp(_data.result.reason, "stopped") == 0;

  char alerts[32];
  _buildAlertsClause(alerts, sizeof(alerts));

  // Completado: el titular fijo del export ("LA EXPOSICION LLEGO A SU FIN")
  // ya dice todo, el detalle aporta como quedaron las alertas.
  char completedDetail[64];
  snprintf(completedDetail, sizeof(completedDetail), "%s", alerts);

  // Detenido: de donde vino la orden. Para el experimento es lo mismo, pero
  // no para quien lee el resultado despues.
  char stoppedDetail[64];
  snprintf(stoppedDetail, sizeof(stoppedDetail), "%s - %s",
           _data.result.fromEmergency ? "pulsador de emergencia" : "boton de la pantalla",
           alerts);

  char criticalHeadline[48];
  char criticalDetail[64];
  _buildCriticalTexts(criticalHeadline, sizeof(criticalHeadline), criticalDetail, sizeof(criticalDetail));

  _applyPanel(ui_ResultadoCompletado, isCompleted, ui_ResultadoMotivoCompletadoTipoCampo, ui_ResultadoMotivoCompletadoMotivoDetalle, completedDetail);
  _applyPanel(ui_ResultadoDetenido, isStopped, ui_ResultadoMotivoDetenidoTipoCampo, ui_ResultadoMotivoDetenidoMotivoDetalle, stoppedDetail);
  _applyPanel(ui_ResultadoFalla, isCritical, ui_ResultadoMotivoFallaTipoCampo, ui_ResultadoMotivoFallaMotivoDetalle, criticalDetail);

  if (isCritical) {
    lv_label_set_text(ui_ResultadoMotivoFallaMotivo, criticalHeadline);
  }

  // REPETIR se oculta cuando corto por falla: repetir el mismo experimento
  // con la misma configuracion que acaba de disparar un limite lo mas
  // probable es que vuelva a cortar igual. Hay que revisar la causa (o la
  // configuracion) antes, y para eso hay que salir de esta pantalla.
  if (isCritical) lv_obj_add_flag(ui_ButtonResultadoRepetir, LV_OBJ_FLAG_HIDDEN);
  else            lv_obj_remove_flag(ui_ButtonResultadoRepetir, LV_OBJ_FLAG_HIDDEN);

  // Progreso, tiempo y campo medio: snapshot congelado en SystemData::result
  // al llegar result_data (ver systemdata.hpp), no los getters en vivo.
  char text[16];
  snprintf(text, sizeof(text), "%.0f%%", _data.result.progressPercent);
  lv_label_set_text(ui_ResultadoProgresoValor, text);

  unsigned long totalMinutes = _data.result.elapsedSeconds / 60;
  snprintf(text, sizeof(text), "%lu", totalMinutes / 60);
  lv_label_set_text(ui_ResultadoProgresoHorasValor, text);
  snprintf(text, sizeof(text), "%02lu", totalMinutes % 60);
  lv_label_set_text(ui_ResultadoProgresoMinutosValor, text);

  if (_data.result.hasMeanMagneticField) {
    snprintf(text, sizeof(text), "%.2f", _data.result.meanMagneticField);
  }
  else {
    // Sin ninguna muestra de CEM1 durante el experimento: no hay promedio
    // que mostrar, y un "0.00" se leeria como campo nulo medido.
    snprintf(text, sizeof(text), "--");
  }
  lv_label_set_text(ui_ResultadoCampoMedioUnidadValor, text);
}

inline void ResultadoController::update() {
  _updateStatusIcons();
}

static void btnResultadoVolverClick(lv_event_t * e) {
  ResultadoController * self = (ResultadoController *) lv_event_get_user_data(e);
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    self->onVolver();
  }
}

static void btnResultadoRepetirClick(lv_event_t * e) {
  ResultadoController * self = (ResultadoController *) lv_event_get_user_data(e);
  if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
    DEBUG_PRINTLN(DEBUG_SCREENCONTROLLER, "BtnRepetir clicked!");
    self->onRepetir();
  }
}

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

inline BusyController::BusyController(SystemData& data) : BaseScreenController(ui_EsperandoScreen, ScreenType::BUSY, "busy"), _data(data) {}

inline void BusyController::show() {
  lv_label_set_text(ui_EsperandoMensaje, _data.busy.messageProcess);
  BaseScreenController::show();
  _startTime = millis();
  _launched = false;
}

inline void BusyController::update() {
  unsigned long now = millis();
  if(now - _startTime > Intervals::BusyTimeout) {
    if(_listener == nullptr) return;
    if(_launched) return;
    ScreenEvent ev;
    ev.type = ScreenType::BUSY;
    ev.name = EventName::Timeout;
    _launched = true;
    _listener->onScreenEvent(ev);
  }
}
