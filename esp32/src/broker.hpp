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
    bool retain;
};

// Tope del paquete MQTT completo (topic + cabecera + payload) que PubSubClient
// arma en un solo buffer. Su default es 256, que alcanzaba cuando toda la
// telemetria era un unico mensaje de 128 bytes; con la division en grupos el
// de `result` lleva la descripcion del corte y no entra. Sin agrandarlo,
// publish() devolveria false y -- como processPublishQueue() deja el mensaje
// encolado y corta -- ese mensaje bloquearia para siempre a todos los que
// vinieran atras.
constexpr uint16_t BrokerBufferSize = 512;

// Keepalive de MQTT, en segundos. El default de PubSubClient es 15: si pasan
// 15 s sin recibir nada manda un PINGREQ, y si en el loop() siguiente sigue
// sin respuesta CORTA la conexion (MQTT_CONNECTION_TIMEOUT). Con la tarea de
// comunicaciones demorada unos segundos (un escaneo WiFi, un reintento TLS),
// una sola respuesta tarde alcanzaba para tirar el broker en plena corrida
// (banco, 2026-10-04). 60 s tolera esas demoras; el costo es que el broker
// tarda hasta 90 s en dar por muerta a una placa que se apago.
constexpr uint16_t BrokerKeepAliveSeconds = 60;
// Cuanto espera PubSubClient una respuesta del broker (CONNACK, lecturas).
// Su default es 15 s, todo ese tiempo con la tarea de comunicaciones parada.
constexpr uint16_t BrokerSocketTimeoutSeconds = 5;

class BrokerManager {
  public:
    BrokerManager();

    void begin(const MqttConfig& config);
    void loop();

    bool isConnected();
    bool publish(const char* topic, const char* message, bool retain = false);
    // Para emisores de rafagas (la config vigente, de a un bloque): publish()
    // con la cola llena desaloja al mas viejo en vez de fallar.
    bool isPublishQueueEmpty();
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
    unsigned long _connectedSince = 0;

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
    static const char* stateName(int state);

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
    // 4 y no 10: la cola se reserva entera al arrancar y cada entrada mide
    // sizeof(PublishMessage), asi que 10 se llevaban ~6,4 KB de heap -- el
    // mismo heap con el que despues compite el handshake TLS, que en esta
    // placa entra por poco. Con 4 alcanza: la unica rafaga posible son los
    // 3 grupos periodicos cayendo juntos, y se drenan en el tick siguiente.
    _publishQueue = xQueueCreate(4, sizeof(PublishMessage));
    if (_publishQueue == nullptr) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] ERROR creando publish queue");
    }
    else {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Publish queue creada");
    }
    _config = config;
    _initialized = true;
    if (!_client.setBufferSize(BrokerBufferSize)) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] ERROR: no se pudo agrandar el buffer MQTT");
    }
    _client.setKeepAlive(BrokerKeepAliveSeconds);
    _client.setSocketTimeout(BrokerSocketTimeoutSeconds);
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
        _connectedSince = millis();
        if (_listener)
            _listener->onBrokerConnected();
    }
    else if (!nowConnected && _wasConnected) {
        _wasConnected = false;
        // POR QUE se cayo. Antes la caida no dejaba ninguna linea en el log
        // (solo el "Conectando..." del reintento), y no habia forma de
        // distinguir un keepalive vencido de un socket cerrado por la red.
        int state = _client.state();
        DEBUG_PRINTF(DEBUG_BROKER,
                     "[BROKER] Desconectado: %s (state=%d) tras %lu s conectado -- WiFi %s, RSSI %d, heap libre %u\n",
                     stateName(state), state, (unsigned long) ((millis() - _connectedSince) / 1000UL),
                     WiFi.status() == WL_CONNECTED ? "ok" : "caida", (int) WiFi.RSSI(),
                     (unsigned) ESP.getFreeHeap());
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

    // Un handshake TLS necesita del orden de 40-50 KB de heap LIBRE, y en
    // bloques grandes y contiguos (mbedTLS pide buffers de entrada y salida
    // de ~16 KB cada uno). Por eso se loguea tambien el bloque mayor y no
    // solo el total: se puede tener 60 KB libres fragmentados en pedazos
    // chicos y fallar igual con -32512 (MBEDTLS_ERR_SSL_ALLOC_FAILED).
    DEBUG_PRINTF(DEBUG_BROKER, "[BROKER] Conectando a %s:%u -- heap libre %u, bloque mayor %u\n",
                 _config.server ? _config.server : "(sin host)", (unsigned) _config.port,
                 (unsigned) ESP.getFreeHeap(), (unsigned) ESP.getMaxAllocHeap());

    bool connected;

    if (_config.user && strlen(_config.user) > 0) {
        connected = _client.connect(clientId, _config.user, _config.password);
    } else {
        connected = _client.connect(clientId);
    }

    if (connected) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Conectado");
        subscribeAll();
        return true;
    }

    // state() es el codigo de PubSubClient, no el de TLS: el -32512 o el
    // -9984 los imprime mbedTLS por su cuenta arriba de esta linea.
    DEBUG_PRINTF(DEBUG_BROKER, "[BROKER] Fallo la conexion: %s (state=%d, heap libre %u)\n",
                 stateName(_client.state()), _client.state(), (unsigned) ESP.getFreeHeap());
    return false;
}

/* ============================================================
 *  ESTADO -> TEXTO (codigos de PubSubClient.h)
 * ============================================================ */
inline const char* BrokerManager::stateName(int state) {
    switch (state) {
        case MQTT_CONNECTION_TIMEOUT:      return "keepalive vencido (el broker no respondio a tiempo)";
        case MQTT_CONNECTION_LOST:         return "conexion perdida (socket cerrado)";
        case MQTT_CONNECT_FAILED:          return "fallo la conexion de red/TLS";
        case MQTT_DISCONNECTED:            return "desconectado";
        case MQTT_CONNECTED:               return "socket cerrado sin aviso MQTT";
        case MQTT_CONNECT_BAD_PROTOCOL:    return "protocolo rechazado";
        case MQTT_CONNECT_BAD_CLIENT_ID:   return "clientId rechazado";
        case MQTT_CONNECT_UNAVAILABLE:     return "broker no disponible";
        case MQTT_CONNECT_BAD_CREDENTIALS: return "credenciales rechazadas";
        case MQTT_CONNECT_UNAUTHORIZED:    return "no autorizado";
        default:                           return "desconocido";
    }
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

        bool success = _client.publish(msg.topic, msg.payload, msg.retain);
        if (!success) {
            // Un payload que no entra en el buffer falla SIEMPRE: dejarlo en
            // la cola trabaria todo lo que viene atras para siempre. Se
            // descarta y se sigue. Una caida del enlace, en cambio, es
            // transitoria: ahi el mensaje se conserva y se reintenta.
            size_t packet = strlen(msg.topic) + strlen(msg.payload) + 16;
            if (packet > BrokerBufferSize) {
                DEBUG_PRINTF(DEBUG_BROKER, "[BROKER] Mensaje de %u bytes no entra en el buffer -> descartado\n",
                             (unsigned) packet);
                PublishMessage oversized;
                xQueueReceive(_publishQueue, &oversized, 0);
                continue;
            }
            DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Publish fallo, mensaje queda en cola");
            break;
        }

        // Solamente ahora lo eliminamos
        PublishMessage sent;
        xQueueReceive(_publishQueue, &sent, 0);
    }
}

inline bool BrokerManager::isPublishQueueEmpty() {
    if (_publishQueue == nullptr) return false;
    return uxQueueMessagesWaiting(_publishQueue) == 0;
}

inline bool BrokerManager::publish(const char* topic, const char* message, bool retain) {
    if (_publishQueue == nullptr) {
        DEBUG_PRINTLN(DEBUG_BROKER, "[BROKER] Queue no inicializada");
        return false;
    }

    PublishMessage msg{};
    strncpy(msg.topic, topic, sizeof(msg.topic) - 1);
    strncpy(msg.payload, message, sizeof(msg.payload) - 1);
    msg.retain = retain;

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

    // Del tamano del paquete: un bloque de la configuracion remota (tarjeta
    // 24) ronda los 400 bytes, y con 128 llegaba cortado -- un JSON ilegible
    // que se descartaba sin que nada dijera por que.
    static char msg[BrokerBufferSize];

    length = min(length, sizeof(msg) - 1);

    memcpy(msg, payload, length);
    msg[length] = '\0';

    if (_listener) {
        _listener->onMessageReceived(topic, msg);
    }
}