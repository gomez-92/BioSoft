#pragma once

#include <Arduino.h>
#include <string.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_TIMER = true;

// =========================
// Configuración
// =========================

// Sobrescribibles desde platformio.ini: el Mega los achica por RAM.
#ifndef MAX_TASKS
#define MAX_TASKS      20   // Cantidad máxima de tareas
#endif
#ifndef MAX_TASK_NAME
#define MAX_TASK_NAME  30   // Longitud máxima del nombre de tarea
#endif


// =========================
// Interfaz de callback
// =========================

// Interfaz que debe implementar quien quiera recibir eventos del Timer
class TimerListener {
public:
    virtual void onTimer(const char* name) = 0;
};


// =========================
// Clase Task (tarea individual)
// =========================

class Task {
private:
    char _name[MAX_TASK_NAME];     // Nombre identificador de la tarea
    unsigned long _intervalMs;     // Intervalo de ejecución (ms)
    unsigned long _lastExecution;  // Última ejecución (timestamp)
    bool _active;                 // Si la tarea está activa
    bool _used;                   // Si la posición está en uso

public:
    Task();

    // Inicializa la tarea
    void init(const char* name, unsigned long intervalMs);

    // Estado de uso
    bool isUsed() const;
    void clear();

    // Control de ejecución
    bool isTime();         // ¿Ya cumplió el intervalo?
    void execute();        // Marca nueva ejecución

    // Estado
    bool isActive() const;
    void setActive(bool active);

    // Info
    const char* getName() const;
};


// =========================
// Clase Timer (scheduler simple)
// =========================

class Timer {
private:
    bool _running;              // Indica si el timer está corriendo
    TimerListener* _listener;            // Callback
    Task _tasks[MAX_TASKS];     // Lista fija de tareas

public:
    Timer();

    // Control del timer
    void begin();   // Inicializa estructura
    void start();   // Comienza ejecución
    void stop();    // Detiene ejecución

    // Debe llamarse en loop()
    void tick();

    // Gestión de tareas
    bool addTask(const char* name, unsigned long intervalMs);
    bool removeTask(const char* name);
    bool includeTask(const char* name);

    // Callback
    void setTimerListener(TimerListener* timerListener);
};


// ======================================================
// ================= IMPLEMENTACIÓN ======================
// ======================================================


// ================= TASK =================

// Constructor: inicializa en estado vacío
inline Task::Task()
    : _intervalMs(0),
      _lastExecution(0),
      _active(true),
      _used(true)
{
    _name[0] = '\0';
}

// Inicializa la tarea con nombre e intervalo
inline void Task::init(const char* name, unsigned long intervalMs)
{
    // Copia segura del nombre
    strncpy(_name, name, MAX_TASK_NAME - 1);
    _name[MAX_TASK_NAME - 1] = '\0';

    _intervalMs = intervalMs;
    _lastExecution = millis();
    _active = true;
    _used = true;
}

// Indica si la tarea está ocupando un slot
inline bool Task::isUsed() const {
    return _used;
}

// Limpia la tarea (libera slot)
inline void Task::clear() {
    _used = false;
    _active = false;
    _name[0] = '\0';
}

// Verifica si ya pasó el tiempo necesario para ejecutar
inline bool Task::isTime() {
    unsigned long now = millis();
    return (now - _lastExecution) >= _intervalMs;
}

// Indica si la tarea está habilitada
inline bool Task::isActive() const {
    return _active;
}

// Activa/desactiva la tarea
inline void Task::setActive(bool active) {
    _active = active;
}

// Devuelve el nombre de la tarea
inline const char* Task::getName() const {
    return _name;
}

// Marca ejecución (resetea el contador de tiempo)
inline void Task::execute() {
    _lastExecution = millis();
}


// ================= TIMER =================

// Constructor
inline Timer::Timer()
    : _running(false),
      _listener(nullptr)
{}

// Inicializa todas las tareas
inline void Timer::begin() {
    for (int i = 0; i < MAX_TASKS; i++) {
        _tasks[i].clear();
    }
}

// Inicia el scheduler
inline void Timer::start() {
    _running = true;
}

// Detiene el scheduler
inline void Timer::stop() {
    _running = false;
}

// Loop del scheduler (llamar en loop() de Arduino)
inline void Timer::tick() {
    if (!_running) return;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (_tasks[i].isUsed() && _tasks[i].isActive()) {

            // Si llegó el momento de ejecutar
            if (_tasks[i].isTime()) {

                // Notifica al callback
                if (_listener) {
                    _listener->onTimer(_tasks[i].getName());
                }

                // Actualiza última ejecución
                _tasks[i].execute();
            }
        }
    }
}

// Agrega una nueva tarea (si hay lugar y no existe)
inline bool Timer::addTask(const char* name, unsigned long intervalMs) {
    // Evita duplicados
    if (includeTask(name))
        return false;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (!_tasks[i].isUsed()) {
            _tasks[i].init(name, intervalMs);
            return true;
        }
    }

    // No hay espacio
    DEBUG_PRINT(DEBUG_TIMER, F("[TIMER][ERROR] addTask: sin espacio para la tarea '"));
    DEBUG_PRINT(DEBUG_TIMER, name);
    DEBUG_PRINTLN(DEBUG_TIMER, F("' (MAX_TASKS alcanzado)"));
    return false;
}

// Elimina una tarea por nombre
inline bool Timer::removeTask(const char* name) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (_tasks[i].isUsed() &&
            strcmp(_tasks[i].getName(), name) == 0) {

            _tasks[i].clear();
            return true;
        }
    }
    return false;
}

// Verifica si una tarea ya existe
inline bool Timer::includeTask(const char* name) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (_tasks[i].isUsed() &&
            strcmp(_tasks[i].getName(), name) == 0) {
            return true;
        }
    }
    return false;
}

// Define el callback
inline void Timer::setTimerListener(TimerListener* listener) {
    _listener = listener;
}
