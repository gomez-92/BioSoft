#pragma once

namespace Intervals {
  constexpr unsigned long Ping                  = 2000;
  constexpr unsigned long SendState             = 5000;
  constexpr unsigned long MeasureTemperature    = 10000;
  constexpr unsigned long MeasureCurrent        = 5000;
  constexpr unsigned long MeasureMagneticField  = 500;
  constexpr unsigned long UpdateProgress        = 1000;
  constexpr unsigned long UpdateVitals          = 5000;
  constexpr unsigned long SendFlags             = 25000;
  constexpr unsigned long SendResult            = 1000;
  constexpr unsigned long SettlingTime          = 10000;
};