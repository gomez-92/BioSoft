#pragma once
#include <Arduino.h>
#include "detector.hpp"
#include "safetymargins.hpp"

// Encapsula COMO se arma un SourceConfig del Detector a partir de valores
// ya validados. Engine (MOD-001) sigue decidiendo CUANDO se llama esto y
// QUE hacer con el resultado (orquestacion), pero ya no arma manualmente
// los campos del SourceConfig inline dentro de onCommand/Start -- ese
// "como" quedaba como zona gris no anticipada por el contrato (ver
// auditoria de MOD-001 en Trello). Separarlo tambien centraliza en un
// solo lugar los valores PLACEHOLDER DE PRUEBA de CEM1 (ver MOD-017),
// para que sea obvio donde reemplazarlos cuando haya calibracion real.
namespace DetectorConfigBuilder {

  // CEM1: rangos normal/critico derivados del target + tolerancia (el
  // multiplicador critico/normal esta en SafetyMargins::CemCriticalMultiplier).
  // Reglas critical/streak/frequency son PLACEHOLDER DE PRUEBA (mismos
  // numeros que TEMP1), sin calibrar contra campo real -- existen solo
  // para validar que el mecanismo de flags funciona end-to-end con el
  // sensor simulado (MagnetometerVoltageSim, ver MOD-007).
  inline SourceConfig buildCemConfig(float cemTarget, int cemTol) {
    SourceConfig config;

    config.criticalMin = cemTarget * (1.0f - SafetyMargins::CemCriticalMultiplier * cemTol / 100.0f);
    config.criticalMax = cemTarget * (1.0f + SafetyMargins::CemCriticalMultiplier * cemTol / 100.0f);
    config.normalMin   = cemTarget * (1.0f - cemTol / 100.0f);
    config.normalMax   = cemTarget * (1.0f + cemTol / 100.0f);

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

  // TEMP1: rangos normal/critico vienen directamente de los parametros del
  // comando start (no se derivan de un target+tolerancia, a diferencia de
  // CEM1). bufferSize se deja en el default (32 muestras) -- esa ventana
  // la usa "frequency"; critical y streak usan racha consecutiva
  // (Source::evaluate en detector.hpp), no la ventana. Con
  // Intervals::MeasureTemperature = 5000ms (corregido -- el comentario
  // original decia 25000ms, que en realidad es Intervals::SendFlags):
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
  inline SourceConfig buildTempConfig(float normalMin, float normalMax, float criticalMin, float criticalMax) {
    SourceConfig config;

    config.normalMin = normalMin;
    config.normalMax = normalMax;
    config.criticalMin = criticalMin;
    config.criticalMax = criticalMax;

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

};
