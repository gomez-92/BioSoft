#pragma once
#include <Arduino.h>

// Interruptor maestro de logs de debug -- si esta en false, TODOS los logs
// quedan deshabilitados sin importar el flag de cada modulo (los flags de
// modulo solo tienen efecto si este tambien esta en true). Duplicado
// byte-a-byte en ambas placas (mismo patron que seriallink.hpp/commands.hpp,
// ver CLAUDE.md) -- mantener sincronizado manualmente si se edita.
constexpr bool DEBUG_ENABLED = true;

#define DEBUG_PRINT(moduleFlag, ...)   do { if (DEBUG_ENABLED && (moduleFlag)) { Serial.print(__VA_ARGS__); } } while (0)
#define DEBUG_PRINTLN(moduleFlag, ...) do { if (DEBUG_ENABLED && (moduleFlag)) { Serial.println(__VA_ARGS__); } } while (0)
#define DEBUG_PRINTF(moduleFlag, ...)  do { if (DEBUG_ENABLED && (moduleFlag)) { Serial.printf(__VA_ARGS__); } } while (0)
