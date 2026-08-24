#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_BROKER = true;

/* ============================================================
 *  CONFIG
 * ============================================================ */
struct MqttConfig {
  const char* server = nullptr;
  uint16_t port = 8883;

  const char* user = nullptr;
  const char* password = nullptr;

  const char* clientId = nullptr;   // opcional
  const char* rootCA = nullptr;     // opcional (TLS)
};

/* ============================================================
 *  LISTENER (EVENTOS)
 * ============================================================ */
class BrokerListener {
  public:
    virtual void onBrokerConnected() = 0;
    virtual void onBrokerDisconnected() = 0;
    virtual void onMessageReceived(const char* topic, const char* payload) = 0;
    virtual ~BrokerListener() {}
};

/* ============================================================
 *  CLASS
 * ============================================================ */
struct PublishMessage {
    char topic[128];
    char payload[512];
};

class BrokerManager {
  public:
    BrokerManager();

    void begin(const MqttConfig& config);
    void loop();

    bool isConnected();
    bool publish(const char* topic, const char* message);
    bool subscribe(const char* topic);
    void processPublishQueue();

    void setListener(BrokerListener* listener);

  private:
    /* ---------------- CONFIG ---------------- */
    MqttConfig _config;
    bool _initialized = false;

    /* ---------------- MQTT CLIENT ---------------- */
    WiFiClientSecure _wifiSecureClient;
    PubSubClient _client;

    /* ---------------- LISTENER ---------------- */
    BrokerListener* _listener = nullptr;

    /* ---------------- RECONNECT ---------------- */
    unsigned long _lastReconnectAttempt = 0;
    unsigned long _reconnectInterval = 5000;
    bool _wasConnected = false;

    /* ---------------- COLA MESSAGES ---------------- */
    QueueHandle_t _publishQueue = nullptr;

    /* ---------------- TOPICS ---------------- */
    static constexpr int MAX_TOPICS = 10;
    static constexpr int MAX_TOPIC_LEN = 64;

    char _topics[MAX_TOPICS][MAX_TOPIC_LEN];
    int _topicCount = 0;

    /* ---------------- INTERNAL ---------------- */
    void configureTLS();
    void subscribeAll();
    bool reconnect();
    void generateClientId(char* buffer, size_t len);

    /* MQTT callback bridge */
    static void _mqttCallback(char* topic, byte* payload, unsigned int length);
    void handleMessage(char* topic, byte* payload, unsigned int length);

    static BrokerManager* _instance;
};

/* ============================================================
 *  STATIC INSTANCE
 * ============================================================ */
inline BrokerManager* BrokerManager::_instance = nullptr;

/* ============================================================
 *  CONSTRUCTOR
 * ============================================================ */
inline BrokerManager::BrokerManager()
    : _client(_wifiSecureClient) {
    _instance = this; // bridge para callback estático
}

/* ============================================================
 *  BEGIN
 * ============================================================ */
inline void BrokerManager::begin(const MqttConfig& config) {
    _publishQueue = xQueueCreate(10, sizeof(PublishMessage));
    if (_publishQueue == nullptr) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] ERROR creando publish queue");
    }
    else {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Publish queue creada");
    }
    _config = config;
    _initialized = true;
    configureTLS();
    if (_config.server) _client.setServer(_config.server, _config.port);
    _client.setCallback(_mqttCallback);
}

/* ============================================================
 *  LOOP
 * ============================================================ */
inline void BrokerManager::loop() {
    if (!_initialized)
        return;

    bool nowConnected = _client.connected();

    /* ================= ESTADO ================= */
    if (nowConnected && !_wasConnected) {
        _wasConnected = true;
        if (_listener)
            _listener->onBrokerConnected();
    }
    else if (!nowConnected && _wasConnected) {
        _wasConnected = false;
        if (_listener)
            _listener->onBrokerDisconnected();
    }

    /* ================= RECONEXIÓN ================= */
    if (!nowConnected) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= _reconnectInterval) {
            _lastReconnectAttempt = now;
            reconnect();
        }
        return;
    }

    /* ================= MQTT ================= */
    _client.loop();

    /* ================= PUBLICACIONES ================= */
    processPublishQueue();
}

/* ============================================================
 *  STATUS
 * ============================================================ */
inline bool BrokerManager::isConnected() {
    return _client.connected();
}

/* ============================================================
 *  RECONNECT
 * ============================================================ */
inline bool BrokerManager::reconnect() {

    if (!_initialized)
        return false;

    if (_client.connected())
        return true;

    if (WiFi.status() != WL_CONNECTED)
        return false;

    /* --- clientId --- */
    const char* clientId = _config.clientId;
    char generatedId[32];

    if (!clientId) {
        generateClientId(generatedId, sizeof(generatedId));
        clientId = generatedId;
    }

    bool connected;

    if (_config.user && strlen(_config.user) > 0) {
        connected = _client.connect(clientId, _config.user, _config.password);
    } else {
        connected = _client.connect(clientId);
    }

    if (connected) {
        subscribeAll();
        return true;
    }

    return false;
}

/* ============================================================
 *  TLS
 * ============================================================ */
inline void BrokerManager::configureTLS() {
    if (_config.rootCA) {
        _wifiSecureClient.setCACert(_config.rootCA);
    }
}

/* ============================================================
 *  CLIENT ID
 * ============================================================ */
inline void BrokerManager::generateClientId(char* buffer, size_t len) {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(buffer, len, "ESP32-%04X%08X",
             (uint16_t)(chipId >> 32),
             (uint32_t)chipId);
}

/* ============================================================
 *  PUBLISH
 * ============================================================ */

inline void BrokerManager::processPublishQueue() {
    if (_publishQueue == nullptr)
        return;
    if (!_client.connected())
        return;
    PublishMessage msg;
    while (xQueuePeek(_publishQueue, &msg, 0) == pdTRUE) {
        DEBUG_PRINTF(DEBUG_BROKER, "[BROKER] Publishing topic=%s\n", msg.topic);

        bool success = _client.publish(msg.topic, msg.payload);
        if (!success) {
            DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Publish fallo, mensaje queda en cola");
            break;
        }

        // Solamente ahora lo eliminamos
        PublishMessage sent;
        xQueueReceive(_publishQueue, &sent, 0);
    }
}

inline bool BrokerManager::publish(const char* topic, const char* message) {
    if (_publishQueue == nullptr) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Queue no inicializada");
        return false;
    }

    PublishMessage msg{};
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.payload, message, sizeof(msg.payload) - 1);

    // Intento normal
    if (xQueueSend(_publishQueue, &msg, 0) == pdTRUE) {
        return true;
    }

    // Cola llena → eliminar el mensaje más viejo
    PublishMessage discarded;

    if (xQueueReceive(_publishQueue, &discarded, 0) == pdTRUE) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Queue llena -> descartando mensaje mas antiguo");
        // Intentar nuevamente
        if (xQueueSend(_publishQueue, &msg, 0) == pdTRUE) {
            return true;
        }
    }

    DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] ERROR: no se pudo agregar mensaje");
    return false;
}

/* ============================================================
 *  SUBSCRIBE
 * ============================================================ */
inline bool BrokerManager::subscribe(const char* topic) {

    if (_topicCount >= MAX_TOPICS)
        return false;

    /* evitar duplicados */
    for (int i = 0; i < _topicCount; i++) {
        if (strcmp(_topics[i], topic) == 0)
            return true;
    }

    strncpy(_topics[_topicCount], topic, MAX_TOPIC_LEN - 1);
    _topics[_topicCount][MAX_TOPIC_LEN - 1] = '\0';

    _topicCount++;

    if (_client.connected()) {
        _client.subscribe(topic);
    }

    return true;
}

/* ============================================================
 *  SUBSCRIBE ALL
 * ============================================================ */
inline void BrokerManager::subscribeAll() {
    for (int i = 0; i < _topicCount; i++) {
        _client.subscribe(_topics[i]);
    }
}



/* ============================================================
 *  LISTENER
 * ============================================================ */
inline void BrokerManager::setListener(BrokerListener* listener) {
    _listener = listener;
}

/* ============================================================
 *  MQTT CALLBACK (STATIC)
 * ============================================================ */
inline void BrokerManager::_mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->handleMessage(topic, payload, length);
    }
}

/* ============================================================
 *  HANDLE MESSAGE
 * ============================================================ */
inline void BrokerManager::handleMessage(char* topic, byte* payload, unsigned int length) {

    static char msg[128];

    length = min(length, sizeof(msg) - 1);

    memcpy(msg, payload, length);
    msg[length] = '\0';

    if (_listener) {
        _listener->onMessageReceived(topic, msg);
    }
}