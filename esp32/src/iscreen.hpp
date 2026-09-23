#pragma once

#include "lvgl.h"

/* ============================================================
 *  SCREEN TYPES
 * ============================================================ */
enum class ScreenType {
    SPLASH,
    PRINCIPAL,
    RUNNING,
    CONFIG,
    RESULT,
    BUSY
};

/* ============================================================
 *  SCREEN EVENT
 * ============================================================ */
class ScreenEvent {
public:
    lv_event_t* source = nullptr;

    ScreenType type;
    const char* name = nullptr;

    const char* variable = nullptr;

    int value = 0;
    float fvalue = 0.0f;
    const char* svalue = nullptr;
};

/* ============================================================
 *  LISTENER
 * ============================================================ */
class IScreenListener {
public:
    virtual void onScreenEvent(ScreenEvent e) = 0;
    virtual ~IScreenListener() {}
};

/* ============================================================
 *  SCREEN INTERFACE
 * ============================================================ */
class IScreen {
public:
    virtual ~IScreen() {}
    // Crea los objetos LVGL de la pantalla (y la cablea) si todavia no
    // existen; destroy() los libera. Las pantallas se crean y se destruyen
    // segun se navega, no todas al arrancar: las seis juntas se llevan
    // ~176 KB de heap, el mismo del que sale el handshake TLS.
    virtual void create() = 0;
    virtual void destroy() = 0;
    virtual bool isCreated() const = 0;
    virtual void init() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void update() = 0;
    virtual lv_obj_t* getRoot() = 0;
    virtual ScreenType getType() const = 0;
    virtual const char* getName() const = 0;
    virtual void setScreenListener(IScreenListener* listener) = 0;
};