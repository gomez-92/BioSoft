#pragma once
#include <Arduino.h>
#include "detector.hpp"

// Encapsula COMO se arma un SourceConfig del Detector a partir de valores
// ya validados. Engine (MOD-001) sigue decidiendo CUANDO se llama esto y
// QUE hacer con el resultado (orquestacion), pero ya no arma manualmente
// los campos del SourceConfig inline dentro de onCommand/Start -- ese
// "como" quedaba como zona gris no anticipada por el contrato (ver
// auditoria de MOD-001 en Trello).
//
// Un SourceConfig se arma hoy con datos de DOS origenes distintos, y por eso
// esta partido en dos mitades:
//
//   1) PLANTILLA (bufferSize + las 3 reglas): defaults compilados que puede
//      pisar el archivo de la SD, via los frames config_source/config_rule.
//      Engine guarda una plantilla por fuente y la mantiene entre corridas.
//   2) RANGOS (normalMin/Max, criticalMin/Max): salen de los parametros que
//      el operador eligio en la pantalla y llegan en el comando start, asi
//      que se escriben recien al arrancar cada experimento.
//
// Por eso hay defaultXxxConfig() (la mitad 1) y applyXxxRanges() (la mitad
// 2) en vez de un unico build: si fueran una sola funcion, cada start
// pisaria las reglas configuradas desde la SD con los defaults compilados.
namespace DetectorConfigBuilder {

  // Topes de validacion de docs/config-schema.md, seccion 7. El criterio
  // del contrato es RECHAZAR ENTERO lo que no cumple (no recortarlo a un
  // extremo): un umbral fuera de rango es un error de configuracion, y
  // silenciarlo dejandolo en el borde mas cercano daria una regla que nadie
  // eligio y que igual corta o deja de cortar un experimento.
  constexpr size_t MinBufferSize = 8;
  constexpr size_t MaxBufferSize = 32;
  constexpr uint16_t MaxCooldown = 64;
  constexpr uint16_t MinMaxEvents = 1;
  constexpr uint16_t MaxMaxEvents = 10;
  constexpr float MinCriticalMultiplier = 1.0f;
  constexpr float MaxCriticalMultiplier = 3.0f;

  inline bool isValidBufferSize(size_t bufferSize) {
    return bufferSize >= MinBufferSize && bufferSize <= MaxBufferSize;
  }

  // Lecturas perdidas antes del corte por silencio (tarjeta 25). 1 seria
  // cortar por un solo tick lento; mas de 20 dejaria a TEMP1 (5 s por
  // lectura) casi dos minutos sin vigilancia.
  constexpr uint16_t MinMaxMissedSamples = 1;
  constexpr uint16_t MaxMaxMissedSamples = 20;

  inline bool isValidMaxMissedSamples(unsigned long value) {
    return value >= MinMaxMissedSamples && value <= MaxMaxMissedSamples;
  }

  inline bool isValidCriticalMultiplier(float multiplier) {
    return multiplier >= MinCriticalMultiplier && multiplier <= MaxCriticalMultiplier;
  }

  // threshold se valida contra el bufferSize de la MISMA fuente: un
  // threshold mayor que la ventana no se puede alcanzar nunca (la regla
  // frequency no tiene de donde sacar tantas muestras), asi que seria una
  // regla apagada de hecho, pero con apariencia de estar configurada.
  inline bool isValidRule(const Rule& rule, size_t bufferSize) {
    if (rule.threshold < 1 || rule.threshold > bufferSize) return false;
    if (rule.cooldown > MaxCooldown) return false;
    if (rule.maxEvents < MinMaxEvents || rule.maxEvents > MaxMaxEvents) return false;
    return true;
  }

  // Defaults compilados de las reglas. CEM1 y TEMP1 comparten numeros: los
  // de CEM1 son PLACEHOLDER DE PRUEBA copiados de TEMP1 (ver MOD-017), sin
  // calibrar contra campo real -- existen para validar que el mecanismo de
  // flags funciona end-to-end. Que ahora se puedan pisar desde la SD es
  // justamente el motivo por el que la seccion detector.sources existe: la
  // calibracion se hace en laboratorio, no recompilando.
  //
  // Con Intervals::MeasureTemperature = 5000ms:
  //
  // critical: 3 muestras CRITICAS seguidas = flag. cooldown 1 (deja pasar
  // al menos 1 muestra entre disparos). maxEvents 2 -> con la 2da (4ta
  // muestra critica seguida) se corta el experimento.
  //
  // streak: 5 muestras fuera de lo normal (pero no criticas) SEGUIDAS =
  // flag. cooldown 4. maxEvents 5 -> corta si la desviacion se sostiene
  // mucho mas tiempo que para "critical" (umbral mas alto a proposito, es
  // una condicion menos grave).
  //
  // frequency: 16 de las ultimas 32 muestras (mitad de la ventana) NO
  // normales (criticas o fuera de rango), sin importar el orden = flag.
  // cooldown 16 (practicamente espera a renovar la ventana antes de
  // reevaluar). maxEvents 3 -> mide inestabilidad sostenida en el tiempo,
  // no un pico puntual.
  inline SourceConfig defaultConfig() {
    SourceConfig config;

    // SourceConfig no inicializa los rangos: se dejan en 0 para que una
    // fuente sin applyXxxRanges() no arrastre basura de la pila.
    config.normalMin = 0.0f;
    config.normalMax = 0.0f;
    config.criticalMin = 0.0f;
    config.criticalMax = 0.0f;

    config.critical.threshold = 3;
    config.critical.cooldown = 1;
    config.critical.maxEvents = 2;

    config.streak.threshold = 5;
    config.streak.cooldown = 4;
    config.streak.maxEvents = 5;

    config.frequency.threshold = 16;
    config.frequency.cooldown = 16;
    config.frequency.maxEvents = 3;

    return config;
  }

  // CEM1: los rangos se DERIVAN del target y la tolerancia que eligio el
  // operador. criticalMultiplier ensancha la banda critica respecto de la
  // normal (1.25 = tolerancia 5% da normal +-5% y critico +-6.25%); es
  // parametro y ya no la constante fija de SafetyMargins, para poder
  // ajustarlo desde la SD.
  inline void applyCemRanges(SourceConfig& config, float cemTarget, int cemTol, float criticalMultiplier) {
    config.criticalMin = cemTarget * (1.0f - criticalMultiplier * cemTol / 100.0f);
    config.criticalMax = cemTarget * (1.0f + criticalMultiplier * cemTol / 100.0f);
    config.normalMin   = cemTarget * (1.0f - cemTol / 100.0f);
    config.normalMax   = cemTarget * (1.0f + cemTol / 100.0f);
  }

  // CEM1 en CAMPO NULO: el objetivo es 0, asi que la banda no se puede
  // derivar multiplicando el target (daria un rango de ancho cero). Se
  // mantiene la tolerancia del operador, pero medida sobre la INTENSIDAD que
  // eligio (1 mT al 5% => +-0.05 mT alrededor de 0), no sobre el cero. El
  // magnetometro entrega modulo (sin signo), asi que en la practica solo
  // cuenta el borde superior; la banda se arma simetrica igual para que el
  // rango sea el que describe el contrato.
  inline void applyCemNullRanges(SourceConfig& config, float chosenIntensity, int cemTol, float criticalMultiplier) {
    float band = chosenIntensity * cemTol / 100.0f;
    config.normalMin   = -band;
    config.normalMax   =  band;
    config.criticalMin = -criticalMultiplier * band;
    config.criticalMax =  criticalMultiplier * band;
  }

  // TEMP1: los rangos vienen tal cual de los parametros del comando start
  // (los dos combos de temperatura de la pantalla), sin derivacion.
  inline void applyTempRanges(SourceConfig& config, float normalMin, float normalMax, float criticalMin, float criticalMax) {
    config.normalMin = normalMin;
    config.normalMax = normalMax;
    config.criticalMin = criticalMin;
    config.criticalMax = criticalMax;
  }

};
