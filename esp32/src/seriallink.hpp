#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "commands.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_SERIALLINK = true;

// =========================
// Configuración del protocolo
// =========================

#define SERIAL_STX        0x02
#define SERIAL_ETX        0x03
#define MAX_JSON_SIZE     256
#define MAX_COMMAND_SIZE  24
#define FRAME_TIMEOUT     100

// Cola de salida: bytes de tramas YA armadas, no JsonDocuments. Ver
// sendCommand(). Sobrescribible desde platformio.ini: el Mega la achica
// por RAM. Tiene que entrar al menos una trama maxima (MAX_JSON_SIZE + 5).
#ifndef TX_BUFFER_SIZE
#define TX_BUFFER_SIZE    2048
#endif
#define TX_INTERVAL_MS    50

#define LAST_PING_INTERVAL_MS 10000
#define LAST_COMMAND_INTERVAL_MS 10000


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

    // Último mensaje recibido. Los params ya no se guardan: solo viven
    // durante onCommand() (ver _processFrame).
    const char* getCommand();

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

    // =========================
    // TX Queue
    // =========================

    // Tramas completas una detras de otra (STX..ETX); la primera es la
    // proxima a salir.
    uint8_t _txBuffer[TX_BUFFER_SIZE];
    uint16_t _txLength = 0;

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

    void _sendNextFrame();

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

// La trama se arma ACA, entera, directo en _txBuffer: el JSON se escribe
// a mano como {"command":"<cmd>","params":<params>} -- los mismos bytes que
// daba serializar un sobre {command, params} -- y despues CRC y ETX.
//
// Antes la cola guardaba una COPIA del JsonDocument por mensaje y al enviar
// armaba un sobre (otra copia) mas un char[256] en el stack: tres copias
// del mismo mensaje en el heap/stack de un Mega con 8 KB. En banco
// (2026-10-03) eso dejaba el corte de un experimento sin memoria para su
// result_data: salia vacio, la ESP32 no sabia por que habia terminado y el
// monitor no cerraba la corrida. Ahora el unico heap es el `params` del que
// llama, que vive solo mientras dura su funcion.
//
// Devuelve false si la cola no tiene lugar o si la trama no entra en
// MAX_JSON_SIZE. Lo segundo antes se truncaba en silencio con CRC valido
// (ver test_serialframes); ahora no sale.
inline bool SerialLink::sendCommand(const char* command, JsonDocument& params) {
    static const char Head[] = "{\"command\":\"";
    static const char Middle[] = "\",\"params\":";
    const size_t headLen = sizeof(Head) - 1;
    const size_t middleLen = sizeof(Middle) - 1;
    const size_t commandLen = strlen(command);
    const size_t paramsLen = measureJson(params);

    const size_t jsonLen = headLen + commandLen + middleLen + paramsLen + 1;   // + '}'
    if (jsonLen > MAX_JSON_SIZE)
        return false;

    const size_t frameLen = jsonLen + 5;   // STX, LEN_LO, LEN_HI, ..., CRC, ETX
    if (_txLength + frameLen > TX_BUFFER_SIZE)
        return false;

    uint8_t* frame = _txBuffer + _txLength;
    frame[0] = SERIAL_STX;
    frame[1] = (uint8_t)(jsonLen & 0xFF);
    frame[2] = (uint8_t)(jsonLen >> 8);

    char* json = (char*)(frame + 3);
    size_t pos = 0;
    memcpy(json + pos, Head, headLen);           pos += headLen;
    memcpy(json + pos, command, commandLen);     pos += commandLen;
    memcpy(json + pos, Middle, middleLen);       pos += middleLen;
    // serializeJson pone un '\0' al final: cae justo donde va la '}'.
    serializeJson(params, json + pos, paramsLen + 1);
    pos += paramsLen;
    json[pos] = '}';

    frame[3 + jsonLen] = _crc8((uint8_t*)json, jsonLen);
    frame[4 + jsonLen] = SERIAL_ETX;

    _txLength += frameLen;
    return true;
}

inline void SerialLink::_sendNextFrame() {
    const uint16_t jsonLen = (uint16_t)_txBuffer[1] | ((uint16_t)_txBuffer[2] << 8);
    const uint16_t frameLen = jsonLen + 5;

    _serial.write(_txBuffer, frameLen);

    _txLength -= frameLen;
    memmove(_txBuffer, _txBuffer + frameLen, _txLength);
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
        DEBUG_PRINTLN(DEBUG_SERIALLINK, "Es tiempo de intentar reconectar la comunicación serial");
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
                DEBUG_PRINTLN(DEBUG_SERIALLINK, "ETX FAIL");
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

    if (now - _lastSend >= TX_INTERVAL_MS && _txLength > 0) {
        _lastSend = now;
        _sendNextFrame();
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

    // Los params se pasan directo desde `doc`, sin copiarlos a un documento
    // miembro: esa segunda copia duplicaba el heap de cada trama y en el Mega
    // (8 KB) fue parte de lo que lo colgaba. Consecuencia: los handlers NO
    // pueden guardar punteros a strings de params mas alla de onCommand()
    // (copiar con strncpy/snprintf, como ya hacen).
    if (_listener) {
        _lastCommandTime = millis();
        _listener->onCommand(_command, doc["params"]);
    }
}


// =========================
// API pública
// =========================

inline const char* SerialLink::getCommand() {
    return _command;
}


// =========================
// Ping
// =========================

inline void SerialLink::sendPing() {
    _pingValue = random(1, 100000);
    StaticJsonDocument<32> p;
    p["value"] = _pingValue;

    DEBUG_PRINT(DEBUG_SERIALLINK, "Enviando ping: ");
    // LOG JSON
    String json;
    serializeJson(p, json);
    DEBUG_PRINTLN(DEBUG_SERIALLINK, json);

    sendCommand(Commands::Ping, p);
    _lastPingTime = millis();
}

inline void SerialLink::sendPong(long value) {
    StaticJsonDocument<32> p;
    p["value"] = value;

    DEBUG_PRINT(DEBUG_SERIALLINK, "Respondiendo pong: ");
    // LOG JSON
    String json;
    serializeJson(p, json);
    DEBUG_PRINTLN(DEBUG_SERIALLINK, json);

    sendCommand(Commands::Pong, p);
}

// La conexion se infiere unicamente de que vuelva el MISMO valor que se
// mando en el ultimo ping -- no alcanza con recibir cualquier byte/mensaje.
// Esto descarta un pong tardio de un ping anterior (que ya no coincide con
// _pingValue) como si fuera prueba de conexion viva.
inline void SerialLink::handlePingResponse(long value) {
    bool result = (value == _pingValue);
    if(result != _connected){
        DEBUG_PRINT(DEBUG_SERIALLINK, "El estado de la comunicación serial a cambiado a: ");
        _connected = result; 
        if(_listener == nullptr) return;
        if(_connected){
            DEBUG_PRINTLN(DEBUG_SERIALLINK, "conectado!");
            _listener->onSerialConnected();
        }
        else {
            DEBUG_PRINTLN(DEBUG_SERIALLINK, "desconectado!");
            _listener->onSerialDisconnected();
        }
    }
}

inline bool SerialLink::isConnected() {
    return _connected;
}


// =========================
// Listener
// =========================

inline void SerialLink::setListener(CommandListener* listener) {
    _listener = listener;
}