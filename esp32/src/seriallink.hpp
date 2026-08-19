#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "commands.hpp"

// =========================
// Configuración del protocolo
// =========================

#define SERIAL_STX        0x02
#define SERIAL_ETX        0x03
#define MAX_JSON_SIZE     256
#define MAX_COMMAND_SIZE  24
#define FRAME_TIMEOUT     100

#define TX_QUEUE_SIZE     10
#define TX_INTERVAL_MS    50

#define LAST_PING_INTERVAL_MS 10000
#define LAST_COMMAND_INTERVAL_MS 10000

// =========================
// Estructuras
// =========================

struct TxMessage {
    char command[MAX_COMMAND_SIZE];
    StaticJsonDocument<128> params;
};


// =========================
// Interfaz de listener
// =========================

class CommandListener {
public:
    virtual void onSerialConnected() = 0;
    virtual void onSerialDisconnected() = 0;
    virtual void onCommand(const char* command, JsonVariantConst params) = 0;
};


// =========================
// SerialLink
// =========================

class SerialLink {
public:
    SerialLink(Stream& serial);

    void begin();
    bool update(); // procesa RX y TX

    // Envío
    bool sendCommand(const char* command, JsonDocument& params);

    // Último mensaje recibido
    const char* getCommand();
    JsonVariantConst getParam(const char* key);

    // Ping / conexión
    void sendPing();
    void sendPong(long value);
    void handlePingResponse(long value);
    bool isConnected();

    // Listener
    void setListener(CommandListener* listener);

    // Métricas
    uint32_t getCrcErrors()   { return _crcErrors; }
    uint32_t getFrameErrors() { return _frameErrors; }
    uint32_t getJsonErrors()  { return _jsonErrors; }

private:
    Stream& _serial;
    bool _initialize;
    // =========================
    // Parser RX
    // =========================

    enum ParserState {
        WAIT_STX,
        READ_LEN1,
        READ_LEN2,
        READ_DATA,
        READ_CRC,
        WAIT_ETX
    };

    ParserState _state = WAIT_STX;

    uint16_t _expectedLen = 0;
    uint16_t _index = 0;
    uint8_t  _rxCrc = 0;

    char _jsonBuffer[MAX_JSON_SIZE + 1];
    uint32_t _lastByteTime = 0;

    // =========================
    // Último mensaje
    // =========================

    char _command[MAX_COMMAND_SIZE];
    StaticJsonDocument<256> _params;

    // =========================
    // TX Queue
    // =========================

    TxMessage _txQueue[TX_QUEUE_SIZE];
    uint8_t _txHead = 0;
    uint8_t _txTail = 0;

    unsigned long _lastSend = 0;

    // =========================
    // Estado conexión
    // =========================

    long _pingValue = 0;
    bool _connected = false;
    unsigned long _lastCommandTime = 0;
    unsigned long _lastPingTime = 0;

    // =========================
    // Métricas
    // =========================

    uint32_t _crcErrors = 0;
    uint32_t _frameErrors = 0;
    uint32_t _jsonErrors = 0;

    // =========================
    // Internos
    // =========================

    void _resetParser();
    void _processFrame();

    bool _isQueueFull();
    bool _isQueueEmpty();
    void _sendFrame(const TxMessage& msg);

    uint8_t _crc8(const uint8_t* data, size_t len);

    CommandListener* _listener = nullptr;
};


// ======================================================
// ================= IMPLEMENTACIÓN ======================
// ======================================================

inline SerialLink::SerialLink(Stream& serial)
    : _serial(serial), _initialize(false) {

}

inline void SerialLink::begin() {
    _resetParser();
    _command[0] = '\0';
    _connected = false;
    _lastCommandTime = millis();
    _lastPingTime = millis();
    _initialize = true;
}


// =========================
// Parser
// =========================

inline void SerialLink::_resetParser() {
    _state = WAIT_STX;
    _index = 0;
    _expectedLen = 0;
}

inline uint8_t SerialLink::_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;

    while (len--) {
        uint8_t extract = *data++;

        for (uint8_t i = 0; i < 8; i++) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) crc ^= 0x8C;
            extract >>= 1;
        }
    }
    return crc;
}


// =========================
// TX
// =========================

inline bool SerialLink::sendCommand(const char* command, JsonDocument& params) {
    if (_isQueueFull())
        return false;

    TxMessage& msg = _txQueue[_txTail];

    strncpy(msg.command, command, MAX_COMMAND_SIZE - 1);
    msg.command[MAX_COMMAND_SIZE - 1] = '\0';

    msg.params.clear();
    msg.params.set(params);

    _txTail = (_txTail + 1) % TX_QUEUE_SIZE;

    return true;
}

inline void SerialLink::_sendFrame(const TxMessage& msg) {
    StaticJsonDocument<256> doc;

    doc["command"] = msg.command;
    doc["params"]  = msg.params;

    char json[MAX_JSON_SIZE];
    size_t len = serializeJson(doc, json);

    uint8_t crc = _crc8((uint8_t*)json, len);

    _serial.write(SERIAL_STX);
    _serial.write((uint8_t)(len & 0xFF));
    _serial.write((uint8_t)(len >> 8));
    _serial.write((uint8_t*)json, len);
    _serial.write(crc);
    _serial.write(SERIAL_ETX);
}


// =========================
// LOOP
// =========================

inline bool SerialLink::update() {
    if(!_initialize) return false;
    if(_connected && millis() - _lastCommandTime > LAST_COMMAND_INTERVAL_MS) {
        if(_listener != nullptr) _listener->onSerialDisconnected();
        _connected = false;
        sendPing();
    }
    else if(!_connected && millis() - _lastPingTime > LAST_PING_INTERVAL_MS) {
        Serial.println("Es tiempo de intentar reconectar la comunicación serial");
        sendPing();
    }

    bool newMessage = false;

    // =========================
    // RX
    // =========================

    while (_serial.available()) {
        uint8_t b = _serial.read();
        _lastByteTime = millis();

        switch (_state) {

            case WAIT_STX:

                if (b == SERIAL_STX) {

                    _resetParser();

                    _state = READ_LEN1;
                }

                break;

            case READ_LEN1:

                _expectedLen = b;

                _state = READ_LEN2;

                break;

            case READ_LEN2:

                _expectedLen |= (b << 8);

                if (_expectedLen > MAX_JSON_SIZE) {

                    _frameErrors++;

                    _resetParser();

                    break;
                }

                _state = READ_DATA;

                break;

            case READ_DATA:

                if (_index < MAX_JSON_SIZE) {

                    _jsonBuffer[_index++] = b;
                }
                else {

                    _frameErrors++;

                    _resetParser();
                }

                if (_index >= _expectedLen) {

                    _state = READ_CRC;
                }

                break;

            case READ_CRC:

                _rxCrc = b;

                _state = WAIT_ETX;

                break;

            case WAIT_ETX:

                if (b == SERIAL_ETX) {

                    uint8_t calc = _crc8((uint8_t*)_jsonBuffer, _expectedLen);
                    _jsonBuffer[_expectedLen] = '\0';

                    if (calc == _rxCrc) {

                        _processFrame();
                        newMessage = true;
                    }
                    else {

                        _crcErrors++;
                    }
            }
            else {
                Serial.println("ETX FAIL");
                _frameErrors++;
            }

    _resetParser();

    break;
        }
    }

    // =========================
    // FRAME TIMEOUT
    // =========================

    if (_state != WAIT_STX &&
        millis() - _lastByteTime > FRAME_TIMEOUT) {

        _frameErrors++;

        _resetParser();
    }

    // =========================
    // TX scheduler
    // =========================

    unsigned long now = millis();

    if (now - _lastSend >= TX_INTERVAL_MS &&
        !_isQueueEmpty()) {

        _lastSend = now;

        TxMessage& msg = _txQueue[_txHead];

        _sendFrame(msg);

        _txHead = (_txHead + 1) % TX_QUEUE_SIZE;
    }

    return newMessage;
}


// =========================
// RX procesamiento
// =========================

inline void SerialLink::_processFrame() {
    StaticJsonDocument<256> doc;

    DeserializationError err = deserializeJson(doc, _jsonBuffer);

    if (err) {
        _jsonErrors++;
        return;
    }

    if (!doc.containsKey("command")) {
        _jsonErrors++;
        return;
    }

    const char* cmd = doc["command"];

    strncpy(_command, cmd, MAX_COMMAND_SIZE - 1);
    _command[MAX_COMMAND_SIZE - 1] = '\0';

    _params.clear();

    if (doc.containsKey("params")) {
        JsonObject p = doc["params"];

        for (JsonPair kv : p) {
            _params[kv.key()] = kv.value();
        }
    }

    if (_listener) {
        _lastCommandTime = millis();
        _listener->onCommand(_command, _params);
    }
}


// =========================
// API pública
// =========================

inline const char* SerialLink::getCommand() {
    return _command;
}

inline JsonVariantConst SerialLink::getParam(const char* key) {
    return _params[key];
}


// =========================
// Ping
// =========================

inline void SerialLink::sendPing() {
    _pingValue = random(1, 100000);
    StaticJsonDocument<32> p;
    p["value"] = _pingValue;

    Serial.print("Enviando ping: ");
    // LOG JSON
    String json;
    serializeJson(p, json);
    Serial.println(json);

    sendCommand(Commands::Ping, p);
    _lastPingTime = millis();
}

inline void SerialLink::sendPong(long value) {
    StaticJsonDocument<32> p;
    p["value"] = value;

    Serial.print("Respondiendo pong: ");
    // LOG JSON
    String json;
    serializeJson(p, json);
    Serial.println(json);

    sendCommand(Commands::Pong, p);
}

inline void SerialLink::handlePingResponse(long value) {
    bool result = (value == _pingValue);
    if(result != _connected){
        Serial.print("El estado de la comunicación serial a cambiado a: ");
        _connected = result; 
        if(_listener == nullptr) return;
        if(_connected){
            Serial.println("conectado!");
            _listener->onSerialConnected();
        }
        else {
            Serial.println("desconectado!");
            _listener->onSerialDisconnected();
        }
    }
}

inline bool SerialLink::isConnected() {
    return _connected;
}


// =========================
// Queue helpers
// =========================

inline bool SerialLink::_isQueueFull() {
    return ((_txTail + 1) % TX_QUEUE_SIZE) == _txHead;
}

inline bool SerialLink::_isQueueEmpty() {
    return _txHead == _txTail;
}


// =========================
// Listener
// =========================

inline void SerialLink::setListener(CommandListener* listener) {
    _listener = listener;
}