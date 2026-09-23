#pragma once

#include "iscreen.hpp"

#define SCREEN_INTERVAL_MS 20

/* ============================================================
 *  MANAGER LISTENER
 * ============================================================ */
class ScreenManagerListener {
public:
    virtual void onScreenChanged(ScreenType from, ScreenType to) = 0;
    virtual ~ScreenManagerListener() {}
};

/* ============================================================
 *  CLASS
 * ============================================================ */
class ScreenManager {
private:
    IScreen** _screens = nullptr;
    int _count = 0;

    int _currentIndex = -1;
    int _previousIndex = -1;
    int _initialIndex = -1;

    unsigned long _lastRefresh = 0;

    IScreenListener* _screenListener = nullptr;
    ScreenManagerListener* _managerListener = nullptr;

public:
    ScreenManager();

    void begin(IScreen** screens, int count, int initialIndex);
    void init();

    void setScreenListener(IScreenListener* listener);
    void setManagerListener(ScreenManagerListener* listener);

    void show(ScreenType type);
    void update();

  private:
    // Que pantallas siguen vivas mientras se muestra `active`.
    //
    // Principal y Configuraciones son un par: se va y se vuelve entre las
    // dos todo el tiempo, y destruir una para recrearla dos segundos
    // despues solo agregaria latencia. Las demas viven solas: mientras
    // corre un experimento no se puede ir a ningun lado, y Resultado se
    // abandona una sola vez.
    static bool belongsToGroup(ScreenType active, ScreenType candidate);

    // NINGUNA manipulacion de pantallas ocurre dentro de show(): crear,
    // destruir y cargar son todas operaciones de LVGL, y show() suele venir
    // llamada desde el callback de un boton de la pantalla que se va a
    // destruir. Hacerlo ahi rompe de dos formas distintas:
    //
    //  - Destruir la pantalla activa deja disp->act_scr en NULL
    //    (obj_delete_core en lv_obj_tree.c) y el siguiente dibujo
    //    desreferencia nulo. Con LV_USE_ASSERT_OBJ en 0 nadie avisa.
    //  - Crear la nueva ANTES de liberar las viejas deja conviviendo los dos
    //    grupos, que es justo el pico de memoria que se queria evitar.
    //
    // Entonces show() solo ANOTA el pedido y update() lo ejecuta, ya fuera
    // de lv_timer_handler y en el orden correcto: liberar y despues crear.
    bool _pendingShow = false;
    ScreenType _pendingType = ScreenType::SPLASH;
    void _applyPendingShow();

    // Pantalla puente: un lv_obj vacio, de unos cientos de bytes, que se
    // carga mientras dura la transicion.
    //
    // Resuelve una contradiccion entre dos requisitos. Por memoria hay que
    // LIBERAR la pantalla vieja antes de crear la nueva -- crear En curso
    // con Principal y Configuraciones todavia vivas no entra en el heap que
    // queda con el TLS conectado, y LV_ASSERT_MALLOC aborta. Pero borrar la
    // pantalla que LVGL tiene como activa deja disp->act_scr en NULL
    // (obj_delete_core, lv_obj_tree.c) y el proximo dibujo desreferencia
    // nulo.
    //
    // Cargando esto primero, la activa deja de ser una de las nuestras y se
    // puede liberar todo antes de crear nada. Entre que se carga y que se
    // carga la definitiva no se llama a lv_timer_handler, asi que nunca se
    // llega a dibujar.
    lv_obj_t* _blank = nullptr;

  public:

    IScreen* getCurrent();
};

/* ============================================================ */

inline ScreenManager::ScreenManager() {
    _lastRefresh = millis();
}

/* ============================================================ */

inline void ScreenManager::begin(IScreen** screens, int count, int initialIndex) {
    _screens = screens;
    _count = count;
    _initialIndex = initialIndex;
}

/* ============================================================ */

inline void ScreenManager::init() {
    // Ya NO se llama init() de todas: init() cablea callbacks sobre objetos
    // LVGL, y esos objetos recien existen cuando la pantalla se crea. Ahora
    // cada pantalla se inicializa dentro de su propio create(), la primera
    // vez que se la muestra y cada vez que se la recrea.
    for (int i = 0; i < _count; i++) {
        if (_screens[i]) {
            _screens[i]->setScreenListener(_screenListener);
        }
    }

    if (_initialIndex >= 0 && _initialIndex < _count && _screens[_initialIndex]) {
        show(_screens[_initialIndex]->getType());
    }
}

inline bool ScreenManager::belongsToGroup(ScreenType active, ScreenType candidate) {
    if (active == candidate) return true;

    bool parDeConfiguracion =
        (active == ScreenType::PRINCIPAL || active == ScreenType::CONFIG) &&
        (candidate == ScreenType::PRINCIPAL || candidate == ScreenType::CONFIG);

    return parDeConfiguracion;
}

/* ============================================================ */

inline void ScreenManager::setScreenListener(IScreenListener* listener) {
    _screenListener = listener;

    for (int i = 0; i < _count; i++) {
        if (_screens[i]) {
            _screens[i]->setScreenListener(listener);
        }
    }
}

/* ============================================================ */

inline void ScreenManager::setManagerListener(ScreenManagerListener* listener) {
    _managerListener = listener;
}

/* ============================================================ */

inline void ScreenManager::show(ScreenType type) {
    if (_screens == nullptr) {
        return;
    }

    if (_count <= 0) {
        return;
    }

    // _currentIndex nunca deberia ser menor a -1 (-1 = "todavia no se
    // mostro ninguna pantalla") -- guarda defensiva de sanidad, no un caso
    // esperado en uso normal.
    if (_currentIndex < -1 || _currentIndex >= _count) {
        return;
    }

    // El indice se mueve YA, aunque la pantalla se cargue recien en el
    // proximo update(): getCurrent() lo usa la maquina de estados de
    // MySystem para decidir si tiene que navegar, y dejarla viendo la
    // pantalla anterior haria que un segundo state_data volviera a
    // procesar la misma transicion.
    for (int i = 0; i < _count; i++) {
        if (_screens[i] == nullptr) continue;
        if (_screens[i]->getType() != type) continue;

        _pendingShow = true;
        _pendingType = type;
        _previousIndex = _currentIndex;
        _currentIndex = i;
        return;
    }
}

inline void ScreenManager::_applyPendingShow() {
    _pendingShow = false;

    if (_currentIndex < 0 || _currentIndex >= _count) return;
    if (_screens[_currentIndex] == nullptr) return;

    ScreenType previousType = _pendingType;
    if (_previousIndex >= 0 && _previousIndex < _count && _screens[_previousIndex]) {
        previousType = _screens[_previousIndex]->getType();
        _screens[_previousIndex]->hide();
    }

    // Se crea una sola vez y se reusa toda la vida del equipo.
    if (_blank == nullptr) {
        _blank = lv_obj_create(NULL);
    }
    if (_blank != nullptr) {
        lv_disp_load_scr(_blank);
    }

    // 1) Liberar PRIMERO lo que no pertenece al grupo de la nueva. Con la
    //    puente cargada, ninguna de estas es la activa. Crear antes de
    //    liberar dejaria conviviendo los dos grupos, que es justamente el
    //    pico que no entra en el heap.
    for (int j = 0; j < _count; j++) {
        if (j == _currentIndex || _screens[j] == nullptr) continue;
        if (belongsToGroup(_pendingType, _screens[j]->getType())) continue;
        if (!_screens[j]->isCreated()) continue;
        _screens[j]->destroy();
    }

    // 2) Recien ahora crear la nueva y cargarla.
    _screens[_currentIndex]->create();
    _screens[_currentIndex]->show();

    if (_managerListener) {
        _managerListener->onScreenChanged(previousType, _pendingType);
    }
}

/* ============================================================ */

inline void ScreenManager::update() {
    // Antes que nada: ejecutar la transicion que haya quedado pendiente del
    // ultimo show(). Acá ya no estamos dentro de ningun callback de LVGL.
    if (_pendingShow) {
        _applyPendingShow();
    }

    unsigned long now = millis();
    if (now - _lastRefresh >= SCREEN_INTERVAL_MS) {
        if (_currentIndex >= 0 && _screens[_currentIndex]) {
            _screens[_currentIndex]->update();
        }
        _lastRefresh = now;
    }
}

/* ============================================================ */

inline IScreen* ScreenManager::getCurrent() {

    if (_currentIndex >= 0 && _currentIndex < _count) {
        return _screens[_currentIndex];
    }

    return nullptr;
}