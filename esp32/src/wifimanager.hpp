#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_WIFI = true;

/* ============================================================
 *  DATA TYPES
 * ============================================================ */
struct WiFiNetwork {
    const char* ssid;
    const char* password;
};

/* ============================================================
 *  LISTENER (EVENTOS)
 * ============================================================ */
class WiFiListener {
public:
    virtual void onWiFiConnected(const char* ssid) = 0;
    virtual void onWiFiDisconnected() = 0;
    virtual ~WiFiListener() {};
};

/* ============================================================
 *  CONFIG
 * ============================================================ */
class WiFiConfig {
private:
    static const int MAX_NETWORKS = 5;
    WiFiNetwork _networks[MAX_NETWORKS];
    int _count;

public:
    WiFiConfig();

    void add(const char* ssid, const char* password);
    int totalNetworks() const;
    const WiFiNetwork& get(int index) const;
};

inline WiFiConfig::WiFiConfig() : _count(0) {}

inline void WiFiConfig::add(const char* ssid, const char* password) {
    if (_count < MAX_NETWORKS) {
        _networks[_count++] = { ssid, password };
    }
}

inline int WiFiConfig::totalNetworks() const {
    return _count;
}

inline const WiFiNetwork& WiFiConfig::get(int index) const {
    return _networks[index];
}

/* ============================================================
 *  MANAGER
 * ============================================================ */
class WiFiManager {
private:
    WiFiConfig _config;
    WiFiListener* _listener = nullptr;

    unsigned long _lastScan = 0;
    unsigned long _scanInterval = 10000;
    unsigned long _timeout = 5000;

    int _currentIndex = -1;
    bool _connected = false;

    /* -------- runtime por red -------- */
    struct Runtime {
        bool blocked = false;
        unsigned long lastFail = 0;
    };

    // 5 hardcodeado a mano para calzar con WiFiConfig::MAX_NETWORKS (privado
    // en esa clase, tambien 5) -- no hay forma de referenciarlo desde aca.
    // Si se cambia uno hay que cambiar el otro, si no connectTo()/
    // findBestNetwork() indexan _runtime[] fuera de sus limites.
    Runtime _runtime[5];

    /* -------- internals -------- */
    void scanAndConnect();
    int findBestNetwork(int found);
    bool connectTo(int index);
    void notifyConnected(const char* ssid);
    void notifyDisconnected();

public:
    WiFiManager();

    void begin(const WiFiConfig& config);
    void update();

    void setListener(WiFiListener* listener);
    bool isConnected() const;
};

/* ============================================================
 *  IMPLEMENTATION
 * ============================================================ */
inline WiFiManager::WiFiManager() {}

inline void WiFiManager::begin(const WiFiConfig& config) {
    _config = config;

    WiFi.mode(WIFI_STA);
    scanAndConnect();
}

inline void WiFiManager::update() {

    bool nowConnected = (WiFi.status() == WL_CONNECTED);

    /* --- detectar cambio de estado --- */
    if (nowConnected && !_connected) {
        _connected = true;
        notifyConnected(WiFi.SSID().c_str());
    }
    else if (!nowConnected && _connected) {
        _connected = false;
        notifyDisconnected();
    }

    /* --- intentar reconectar --- */
    if (!nowConnected) {
        scanAndConnect();
        return;
    }

    /* --- reevaluar prioridad --- */
    if (millis() - _lastScan > _scanInterval) {
        scanAndConnect();
    }
}

inline void WiFiManager::scanAndConnect() {

    _lastScan = millis();

    int n = WiFi.scanNetworks();
    if (n <= 0) return;

    int idx = findBestNetwork(n);

    if (idx >= 0 && idx != _currentIndex) {
        connectTo(idx);
    }

    WiFi.scanDelete();
}

inline int WiFiManager::findBestNetwork(int found) {

    unsigned long now = millis();

    for (int i = 0; i < _config.totalNetworks(); i++) {

        /* evitar redes bloqueadas */
        if (_runtime[i].blocked &&
            now - _runtime[i].lastFail < 30000) {
            continue;
        }

        for (int j = 0; j < found; j++) {
            if (WiFi.SSID(j) == _config.get(i).ssid) {
                return i; // prioridad por orden
            }
        }
    }

    return -1;
}

inline bool WiFiManager::connectTo(int index) {

    const WiFiNetwork& net = _config.get(index);

    DEBUG_PRINTF(DEBUG_WIFI, "[WiFi] Connecting to %s\n", net.ssid);

    WiFi.begin(net.ssid, net.password);

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < _timeout) {

        delay(200);
        DEBUG_PRINT(DEBUG_WIFI, ".");
    }

    DEBUG_PRINTLN(DEBUG_WIFI, );

    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTLN(DEBUG_WIFI, "[WiFi] Connected");
        notifyConnected(net.ssid);
        _runtime[index].blocked = false;
        _currentIndex = index;

        return true;
    }

    notifyDisconnected();
    _runtime[index].blocked = true;
    _runtime[index].lastFail = millis();

    return false;
}

inline void WiFiManager::notifyConnected(const char* ssid) {
    if (_listener) _listener->onWiFiConnected(ssid);
}

inline void WiFiManager::notifyDisconnected() {
    if (_listener) _listener->onWiFiDisconnected();
}

inline void WiFiManager::setListener(WiFiListener* listener) {
    _listener = listener;
}

inline bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}