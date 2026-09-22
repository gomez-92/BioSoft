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
// La SD en la ESP32-2432S028 (CYD) NO comparte el bus SPI con el TFT: el
// TFT va por HSPI (MISO=12, MOSI=13, SCLK=14 -- ver platformio.ini) y el
// slot SD esta cableado a VSPI (SCLK=18, MISO=19, MOSI=23) con CS en
// GPIO 5. Por eso este modulo abre su propia SPIClass(VSPI) en esos pines,
// independiente de la instancia interna de TFT_eSPI.
//
// Antes se asumia que la SD colgaba del bus del TFT (14/12/13, HSPI). Con
// esos pines la tarjeta nunca contesta y el core reporta
// "sdSelectCard(): Select Failed" antes de intentar montar -- verificado
// en placa con tools/test-sd-esp32. Ademas, una segunda SPIClass(HSPI)
// pisaria la configuracion del periferico que TFT_eSPI ya usa.
class SdStorage {
  public:
    explicit SdStorage(uint8_t csPin = 5, uint8_t sckPin = 18, uint8_t misoPin = 19, uint8_t mosiPin = 23);

    bool begin();
    bool isReady() const;

    bool exists(const char* path) const;
    // Crea el directorio si no existe (no falla si ya estaba). Hace falta
    // porque SD.open(..., FILE_WRITE) NO crea los directorios intermedios:
    // en una tarjeta virgen, o en una que nunca tuvo config.json, escribir
    // en /biosoft/... falla sin mas explicacion que un open() en false.
    bool ensureDir(const char* path);
    bool remove(const char* path);

    // Devuelve false si el archivo no existe, si el buffer es invalido, o
    // si el contenido NO ENTRA COMPLETO en el buffer (maxLength incluye el
    // terminador nulo). outLength, si no es nullptr, recibe la cantidad de
    // bytes leidos (sin contar el terminador).
    //
    // Que un archivo truncado devuelva false es deliberado: quien lee un
    // archivo de configuracion no tiene forma de distinguir "JSON invalido"
    // de "JSON cortado por buffer chico" si esto devolviera true, y el
    // sintoma -- arrancar con los defaults -- seria el mismo. Ver
    // docs/config-schema.md.
    bool readFile(const char* path, char* outBuffer, size_t maxLength, size_t* outLength = nullptr) const;

    // Sobreescribe el archivo si ya existe (lo crea si no).
    bool writeFile(const char* path, const char* data);

    // Agrega al final del archivo (lo crea si no existe).
    bool appendFile(const char* path, const char* data);

  private:
    uint8_t _csPin;
    uint8_t _sckPin;
    uint8_t _misoPin;
    uint8_t _mosiPin;
    SPIClass _spi;
    bool _ready;
};

// El constructor solo guarda los pines: este objeto se declara global en el
// .ino y su constructor corre antes que setup(), asi que no toca hardware.
// El bus SPI se abre en begin(), mismo criterio que PwmDriver y
// CoilExcitation en el Mega.
inline SdStorage::SdStorage(uint8_t csPin, uint8_t sckPin, uint8_t misoPin, uint8_t mosiPin)
  : _csPin(csPin), _sckPin(sckPin), _misoPin(misoPin), _mosiPin(mosiPin), _spi(VSPI), _ready(false) {
}

inline bool SdStorage::begin() {
  _spi.begin(_sckPin, _misoPin, _mosiPin, _csPin);
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

inline bool SdStorage::ensureDir(const char* path) {
  if (!_ready || path == nullptr) return false;
  if (SD.exists(path)) return true;
  return SD.mkdir(path);
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

  // El contenido tiene que entrar entero, con lugar para el terminador. Si
  // no entra se corta acá, en vez de devolver un buffer truncado que el
  // llamador tomaria por bueno.
  if (available > maxLength - 1) {
    file.close();
    DEBUG_PRINT(DEBUG_SDSTORAGE, F("[SD] ERROR: "));
    DEBUG_PRINT(DEBUG_SDSTORAGE, path);
    DEBUG_PRINT(DEBUG_SDSTORAGE, F(" no entra en el buffer ("));
    DEBUG_PRINT(DEBUG_SDSTORAGE, available);
    DEBUG_PRINT(DEBUG_SDSTORAGE, F(" bytes, buffer de "));
    DEBUG_PRINT(DEBUG_SDSTORAGE, maxLength);
    DEBUG_PRINTLN(DEBUG_SDSTORAGE, F(")"));
    return false;
  }

  size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(outBuffer), available);
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
