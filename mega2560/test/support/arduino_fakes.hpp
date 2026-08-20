#pragma once

// Helpers compartidos para los tests nativos (pio test -e native). El
// firmware real usa Serial.print/println en todos lados para trazas por
// USB (ver CLAUDE.md: es intencional, no se saca al tocar ese codigo). En
// ArduinoFake, Serial es un fakeit::Mock puro: si el codigo bajo test llama
// a un metodo que no fue "faked" explicitamente, tira una excepcion no
// atrapada y el binario del test crashea (access violation) en vez de
// fallar como un test mas. silenceSerial() deja todas las variantes de
// print/println/write como no-op, para poder testear la logica sin tener
// que enumerar cada Serial.print de cada archivo bajo test.
//
// Llamar a silenceSerial() en el setUp() de cada suite que ejercite codigo
// con trazas por Serial (practicamente todo lo que no sea Buffer/Rule
// puros).

#include <ArduinoFake.h>

using namespace fakeit;

inline void silenceSerial() {
    Fake(
        OverloadedMethod(ArduinoFake(Serial), print, size_t(const __FlashStringHelper*)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(const String&)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(const char[])),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(char)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(unsigned char, int)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(int, int)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(unsigned int, int)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(long, int)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(unsigned long, int)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(double, int)),
        OverloadedMethod(ArduinoFake(Serial), print, size_t(const Printable&)),

        OverloadedMethod(ArduinoFake(Serial), println, size_t(const __FlashStringHelper*)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(const String&)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(const char[])),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(char)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(unsigned char, int)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(int, int)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(unsigned int, int)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(long, int)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(unsigned long, int)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(double, int)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(const Printable&)),
        OverloadedMethod(ArduinoFake(Serial), println, size_t(void)),

        OverloadedMethod(ArduinoFake(Serial), write, size_t(uint8_t)),
        OverloadedMethod(ArduinoFake(Serial), write, size_t(const uint8_t*, size_t))
    );
}
