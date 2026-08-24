#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_SDSTORAGE = true;

// Modulo generico de almacenamiento en tarjeta SD -- agnostico del
// significado de los datos que guarda (contrato MOD-020). Expone
// operaciones basicas de archivo; quien decide QUE guardar y CUANDO es
// responsabilidad de quien lo usa (Engine/MySystem), no de este modulo.
//
// La SD en la ESP32-2432S028 (CYD) comparte el bus SPI fisico con el TFT
// (MISO=12, MOSI=13, SCLK=14 -- ver platformio.ini/display.hpp) pero tiene
// su propio Chip Select (GPIO 5). Por eso este modulo abre su propia
// SPIClass(HSPI) en esos mismos pines en vez de reusar la instancia
// interna de TFT_eSPI -- las mantiene desacopladas.
class SdStorage {
  public:
    explicit SdStorage(uint8_t csPin = 5, uint8_t sckPin = 14, uint8_t misoPin = 12, uint8_t mosiPin = 13);

    bool begin();
    bool isReady() const;

    bool exists(const char* path) const;
    bool remove(const char* path);

    // Devuelve false si el archivo no existe o no entra en el buffer
    // (maxLength incluye el terminador nulo). outLength, si no es nullptr,
    // recibe la cantidad de bytes leidos (sin contar el terminador).
    bool readFile(const char* path, char* outBuffer, size_t maxLength, size_t* outLength = nullptr) const;

    // Sobreescribe el archivo si ya existe (lo crea si no).
    bool writeFile(const char* path, const char* data);

    // Agrega al final del archivo (lo crea si no existe).
    bool appendFile(const char* path, const char* data);

  private:
    uint8_t _csPin;
    SPIClass _spi;
    bool _ready;
};

inline SdStorage::SdStorage(uint8_t csPin, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin)
  : _csPin(csPin), _spi(HSPI), _ready(false) {
  _spi.begin(sckPin, misoPin, mosiPin, csPin);
}

inline bool SdStorage::begin() {
  _ready = SD.begin(_csPin, _spi);
  if (!_ready) {
    DEBUG_PRINTLN(DEBUG_SDSTORAGE, F("[SD] ERROR: no se pudo inicializar la tarjeta SD"));
  }
  return _ready;
}

inline bool SdStorage::isReady() const {
  return _ready;
}

inline bool SdStorage::exists(const char* path) const {
  if (!_ready) return false;
  return SD.exists(path);
}

inline bool SdStorage::remove(const char* path) {
  if (!_ready) return false;
  return SD.remove(path);
}

inline bool SdStorage::readFile(const char* path, char* outBuffer, size_t maxLength, size_t* outLength) const {
  if (!_ready || outBuffer == nullptr || maxLength == 0) return false;

  File file = SD.open(path, FILE_READ);
  if (!file) return false;

  size_t available = file.size();
  size_t toRead = (available < maxLength - 1) ? available : (maxLength - 1);
  size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(outBuffer), toRead);
  outBuffer[bytesRead] = '\0';
  file.close();

  if (outLength != nullptr) {
    *outLength = bytesRead;
  }
  return true;
}

// NOTA: asume que FILE_WRITE trunca el archivo (semantica del core
// arduino-esp32 3.x, "w"). Si al compilar resulta ser append-only en la
// version de core instalada, hay que agregar un SD.remove(path) previo.
inline bool SdStorage::writeFile(const char* path, const char* data) {
  if (!_ready || data == nullptr) return false;

  File file = SD.open(path, FILE_WRITE);
  if (!file) return false;

  size_t written = file.print(data);
  file.close();
  return written == strlen(data);
}

inline bool SdStorage::appendFile(const char* path, const char* data) {
  if (!_ready || data == nullptr) return false;

  File file = SD.open(path, FILE_APPEND);
  if (!file) return false;

  size_t written = file.print(data);
  file.close();
  return written == strlen(data);
}
