#pragma once

#include <math.h>
#include <stdint.h>

/*
 * ============================================================================
 *  RmsAccumulator
 * ============================================================================
 *
 * Filtro de remoción de offset DC de un solo paso (running average) + cálculo
 * de RMS sobre una ventana de muestras, igual al usado por EmonLib/OpenEnergy
 * Monitor. Extraído de CurrentSensorSct013::update() (ver MOD-006 en Trello)
 * para poder testearse en host sin depender del ADS1115: no toca hardware,
 * solo acumula valores en volts ya leídos por el driver.
 */
class RmsAccumulator {
  private:
    float _sumSquares = 0;
    float _dcOffset = 0;
    uint16_t _count = 0;

  public:
    void addSample(float volts) {
      _dcOffset += (volts - _dcOffset) / (_count + 1);
      float filtered = volts - _dcOffset;
      _sumSquares += filtered * filtered;
      _count++;
    }

    // Devuelve false (sin modificar out) si no se agregó ninguna muestra, o
    // si el RMS resultante es NaN/Inf; true en caso contrario con el RMS
    // filtrado en out.
    bool result(float& out) const {
      if (_count == 0)
        return false;

      float rms = sqrt(_sumSquares / _count);
      if (isnan(rms) || isinf(rms))
        return false;

      out = rms;
      return true;
    }
};
