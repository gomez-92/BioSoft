#pragma once
// Log estandar de BioSoft. IDENTICO byte a byte en esp32/src y mega2560/src
// (mismo patron que seriallink.hpp): si se edita uno, se copia al otro.
//
//   LOG_E(MODULO, "fmt", ...)   error: algo fallo y se perdio una funcion,
//                               o la corrida quedo afectada
//   LOG_W(MODULO, "fmt", ...)   advertencia: entrada rechazada o
//                               funcionamiento degradado, pero sigue
//   LOG_I(MODULO, "fmt", ...)   info: transiciones, UNA vez por evento
//   LOG_D(MODULO, "fmt", ...)   detalle: lo periodico y lo fino
//
// Una llamada = una linea completa, con este formato:
//
//      12.345 I CORRIDA    inicio intensidad=1.0 mT dur=300 s
//
// segundos.ms desde el arranque, letra del nivel, modulo (ancho fijo) y el
// mensaje. Reglas de redaccion: castellano sin tildes, minuscula inicial,
// sin punto final, datos como clave=valor CON unidad. Nunca contrasenas,
// tokens ni el contenido de la config; SSID y host del broker solo en D.
// Una condicion que se repite se loguea por flanco (empieza / termina), no
// en cada vuelta.
//
// El formato es printf y tiene que ser un literal: la macro lo manda a
// flash con PSTR (en el Mega no ocupa RAM). En el Mega se escribe con
// vfprintf_P caracter por caracter, sin buffer de linea: el costo de un log
// es ~60 bytes de stack mientras escribe, nada de RAM fija mas que el FILE.
// %f en el Mega necesita printf_flt (enlazado en su platformio.ini).
//
// Umbral: LOG_LEVEL (por defecto LOG_NIVEL_I) en compilacion -- lo que
// queda por debajo no llega al binario. El detalle se prende por modulo con
// -D LOG_DEBUG_<MODULO>=1, sin bajar el umbral de todos.
//
// El modulo es un identificador de la lista cerrada de abajo: uno que no
// esta no compila.

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#if defined(__AVR__)
#include <avr/pgmspace.h>
#endif

#ifndef PSTR
#define PSTR(s) (s)
#endif

#define LOG_NIVEL_E 1
#define LOG_NIVEL_W 2
#define LOG_NIVEL_I 3
#define LOG_NIVEL_D 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_NIVEL_I
#endif

// Ancho de la columna del modulo: el nombre mas largo es TELEMETRIA.
#define LOG_MODULE_WIDTH 10

// --- Modulos (comunes y de cada placa). Detalle apagado por defecto. ---
#ifndef LOG_DEBUG_SISTEMA
#define LOG_DEBUG_SISTEMA 0
#endif
#ifndef LOG_DEBUG_ENLACE
#define LOG_DEBUG_ENLACE 0
#endif
#ifndef LOG_DEBUG_CORRIDA
#define LOG_DEBUG_CORRIDA 0
#endif
#ifndef LOG_DEBUG_CONFIG
#define LOG_DEBUG_CONFIG 0
#endif
// Mega
#ifndef LOG_DEBUG_DETECTOR
#define LOG_DEBUG_DETECTOR 0
#endif
#ifndef LOG_DEBUG_CONTROL
#define LOG_DEBUG_CONTROL 0
#endif
#ifndef LOG_DEBUG_CAMPO
#define LOG_DEBUG_CAMPO 0
#endif
#ifndef LOG_DEBUG_TEMP
#define LOG_DEBUG_TEMP 0
#endif
#ifndef LOG_DEBUG_CORRIENTE
#define LOG_DEBUG_CORRIENTE 0
#endif
#ifndef LOG_DEBUG_I2C
#define LOG_DEBUG_I2C 0
#endif
// ESP32
#ifndef LOG_DEBUG_PANTALLA
#define LOG_DEBUG_PANTALLA 0
#endif
#ifndef LOG_DEBUG_REMOTA
#define LOG_DEBUG_REMOTA 0
#endif
#ifndef LOG_DEBUG_WIFI
#define LOG_DEBUG_WIFI 0
#endif
#ifndef LOG_DEBUG_BROKER
#define LOG_DEBUG_BROKER 0
#endif
#ifndef LOG_DEBUG_TELEMETRIA
#define LOG_DEBUG_TELEMETRIA 0
#endif
#ifndef LOG_DEBUG_SD
#define LOG_DEBUG_SD 0
#endif

#define LOG_ENABLED(nivel, mod) \
  ((LOG_LEVEL >= (nivel)) || ((nivel) == LOG_NIVEL_D && LOG_DEBUG_##mod))

#define LOG_AT(nivel, letra, mod, fmt, ...)                               \
  do {                                                                    \
    if (LOG_ENABLED(nivel, mod)) {                                        \
      Log::write(letra, PSTR(#mod), PSTR(fmt), ##__VA_ARGS__);            \
    }                                                                     \
  } while (0)

#define LOG_E(mod, fmt, ...) LOG_AT(LOG_NIVEL_E, 'E', mod, fmt, ##__VA_ARGS__)
#define LOG_W(mod, fmt, ...) LOG_AT(LOG_NIVEL_W, 'W', mod, fmt, ##__VA_ARGS__)
#define LOG_I(mod, fmt, ...) LOG_AT(LOG_NIVEL_I, 'I', mod, fmt, ##__VA_ARGS__)
#define LOG_D(mod, fmt, ...) LOG_AT(LOG_NIVEL_D, 'D', mod, fmt, ##__VA_ARGS__)

namespace Log {

// Destino de las lineas. La placa lo fija en setup() (Serial); sin destino
// los logs no hacen nada, ni siquiera leer millis() -- por eso los tests
// nativos no necesitan stubs de Serial.
typedef void (*Sink)(const char* data, size_t length);

inline Sink& sink() {
  static Sink current = nullptr;
  return current;
}

inline void begin(Sink output) { sink() = output; }

#if defined(__AVR__)

inline int _put(char c, FILE*) {
  sink()(&c, 1);
  return 0;
}

inline FILE* _stream() {
  static FILE stream;
  static bool ready = false;
  if (!ready) {
    fdev_setup_stream(&stream, _put, nullptr, _FDEV_SETUP_WRITE);
    ready = true;
  }
  return &stream;
}

// module y fmt estan en flash (PSTR). %S = cadena en flash en avr-libc.
inline void write(char level, const char* module, const char* fmt, ...) {
  if (sink() == nullptr) return;
  unsigned long now = millis();
  FILE* stream = _stream();
  fprintf_P(stream, PSTR("%6lu.%03u %c %-10S "), now / 1000UL, (unsigned)(now % 1000UL), level, module);
  va_list args;
  va_start(args, fmt);
  vfprintf_P(stream, fmt, args);
  va_end(args);
  sink()("\r\n", 2);
}

#else

#ifndef LOG_LINE_MAX
#define LOG_LINE_MAX 192
#endif

// La linea se arma entera y sale con UNA escritura: en la ESP32 escriben
// los dos nucleos, y asi dos lineas no se intercalan. Lo que no entra en
// LOG_LINE_MAX se corta, pero la linea siempre termina en \r\n.
inline void write(char level, const char* module, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

inline void write(char level, const char* module, const char* fmt, ...) {
  if (sink() == nullptr) return;
  char line[LOG_LINE_MAX];
  unsigned long now = millis();
  int used = snprintf(line, sizeof(line), "%6lu.%03u %c %-10s ", now / 1000UL, (unsigned)(now % 1000UL),
                      level, module);
  if (used < 0) return;
  size_t room = sizeof(line) - 2;
  if ((size_t)used < room) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(line + used, room - used, fmt, args);
    va_end(args);
  }
  size_t length = strnlen(line, room - 1);
  line[length++] = '\r';
  line[length++] = '\n';
  sink()(line, length);
}

#endif

}  // namespace Log
