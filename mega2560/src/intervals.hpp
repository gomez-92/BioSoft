#pragma once

// Ya NO son constexpr: se pueden pisar por config_intervals, el frame que la
// ESP32 manda al reconectar con los valores de intervals.mega del archivo de
// la SD (ver docs/config-schema.md). Los valores de aca son los DEFAULTS
// COMPILADOS, vigentes si no llega el frame o si una clave falta/es invalida.
//
// Se leen en cada addTask(), no se cachean en ningun lado, asi que alcanza
// con pisarlos antes de que Engine registre sus tareas (onStart()/onReady()).
namespace Intervals {
  inline unsigned long Ping                  = 2000;
  inline unsigned long SendState             = 5000;
  inline unsigned long MeasureTemperature    = 5000;
  inline unsigned long MeasureCurrent        = 8500;
  inline unsigned long MeasureMagneticField  = 500;
  inline unsigned long UpdateProgress        = 1000;
  inline unsigned long SendFlags             = 25000;
  inline unsigned long SendResult            = 1000;
  inline unsigned long SettlingTime          = 10000;
};
