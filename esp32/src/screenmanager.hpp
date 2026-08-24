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

    for (int i = 0; i < _count; i++) {
        if (_screens[i]) {
            _screens[i]->init();
            _screens[i]->setScreenListener(_screenListener);
        }
    }

    if (_initialIndex >= 0 && _initialIndex < _count && _screens[_initialIndex]) {
        show(_screens[_initialIndex]->getType());
    }
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

    // Default a "type" (el destino) para el primer show() de todos, cuando
    // no hay pantalla previa (_currentIndex == -1) -- en ese caso
    // onScreenChanged() notifica from==to en vez de un "previous" invalido.
    ScreenType previousType = type;

    for (int i = 0; i < _count; i++) {
        if (_screens[i] == nullptr) {
            continue;
        }

        ScreenType screenType = _screens[i]->getType();
        if (screenType == type) {
            if (_currentIndex >= 0 && _screens[_currentIndex]) {
                previousType = _screens[_currentIndex]->getType();
                _screens[_currentIndex]->hide();
            }
            _currentIndex = i;
            _screens[i]->show();
            if (_managerListener) {
                _managerListener->onScreenChanged(
                    previousType,
                    type
                );
            }
            return;
        }
    }
}

/* ============================================================ */

inline void ScreenManager::update() {
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