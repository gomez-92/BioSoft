#pragma once
#include <Arduino.h>

// Direccion (ROM code) de un DS18B20, configurable desde la SD
// (detector.sources[TEMP1].address). Antes estaba fija en el .ino: montar
// otra unidad del mismo sensor hacia que getTempC() devolviera -127 para
// siempre, y la corrida se cortaba por silencio sin que nada dijera que el
// problema era la direccion y no el cableado.
//
// Formato aceptado: 16 digitos hex, con o sin separadores (":", "-" o
// espacios): "28:3F:E5:57:04:E1:3D:ED" o "283FE55704E13DED". Se valida
// ENTERA, como las reglas del Detector: familia 0x28 (DS18B20) y el CRC8 del
// ultimo byte. Una direccion con un digito mal copiado se rechaza y queda la
// anterior, en vez de quedar apuntando a un sensor que no existe.
//
// Sin dependencias de OneWire.h a proposito: se prueba en native
// (test_onewireaddress).
namespace OneWireAddress {

  constexpr uint8_t Length = 8;
  constexpr uint8_t FamilyDs18b20 = 0x28;
  // "28:3F:E5:57:04:E1:3D:ED" + '\0'
  constexpr size_t TextLength = Length * 3;

  // CRC8 Dallas/Maxim (polinomio x^8 + x^5 + x^4 + 1, reflejado), el mismo
  // que OneWire::crc8.
  inline uint8_t crc8(const uint8_t* data, uint8_t length) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < length; i++) {
      uint8_t byte = data[i];
      for (uint8_t bit = 0; bit < 8; bit++) {
        uint8_t mix = (crc ^ byte) & 0x01;
        crc >>= 1;
        if (mix) crc ^= 0x8C;
        byte >>= 1;
      }
    }
    return crc;
  }

  inline int8_t hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  enum class ParseResult : uint8_t { Ok, BadFormat, NotDs18b20, BadCrc };

  // `out` solo se escribe si el resultado es Ok.
  inline ParseResult parse(const char* text, uint8_t out[Length]) {
    if (text == nullptr) return ParseResult::BadFormat;
    uint8_t bytes[Length];
    uint8_t digits = 0;
    for (const char* p = text; *p != '\0'; p++) {
      if (*p == ':' || *p == '-' || *p == ' ') continue;
      int8_t value = hexValue(*p);
      if (value < 0 || digits >= Length * 2) return ParseResult::BadFormat;
      if (digits % 2 == 0) bytes[digits / 2] = (uint8_t)(value << 4);
      else bytes[digits / 2] |= (uint8_t)value;
      digits++;
    }
    if (digits != Length * 2) return ParseResult::BadFormat;
    if (bytes[0] != FamilyDs18b20) return ParseResult::NotDs18b20;
    if (crc8(bytes, Length - 1) != bytes[Length - 1]) return ParseResult::BadCrc;
    memcpy(out, bytes, Length);
    return ParseResult::Ok;
  }

  // "28:3F:E5:57:04:E1:3D:ED". `buffer` de TextLength bytes como minimo.
  // Sin tabla: en AVR un array constante vive en RAM, y el Mega anda justo.
  inline char hexDigit(uint8_t value) {
    return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
  }

  inline void format(const uint8_t address[Length], char* buffer) {
    for (uint8_t i = 0; i < Length; i++) {
      buffer[i * 3] = hexDigit(address[i] >> 4);
      buffer[i * 3 + 1] = hexDigit(address[i] & 0x0F);
      buffer[i * 3 + 2] = (i + 1 < Length) ? ':' : '\0';
    }
  }
}
