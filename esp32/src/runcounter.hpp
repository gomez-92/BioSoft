#pragma once
// Numero de corrida (#1, #2, ...): el identificador que comparten la
// pantalla, los logs USB de las dos placas y el monitor web. Lo genera la
// placa porque es la unica que ve todo: el monitor deduce las corridas de
// targets/result y el Mega no sabe nada de las anteriores.
//
// Vive en la NVS de la ESP32 (Preferences), no en la SD: sobrevive reinicios
// y cargas de firmware, y no depende de que haya tarjeta. Borrar la flash
// entera (pio run -t erase) lo vuelve a 0.
//
// Se RESERVA al mandar el start (next(): el ultimo + 1, el mismo en cada
// reenvio) y se CONFIRMA al entrar en Running (commit()). Un start que el
// Mega rechaza no gasta numero, asi que no quedan huecos que hagan buscar
// una corrida que nunca existio. Una escritura de NVS por corrida.
#include <Preferences.h>
#include "log.hpp"

namespace RunCounter {

  constexpr const char* Namespace = "biosoft";
  constexpr const char* Key = "corrida";

  inline uint32_t _last = 0;
  inline bool _loaded = false;

  // El ultimo numero confirmado (0 = nunca hubo una corrida en esta placa).
  inline uint32_t last() {
    if (!_loaded) {
      Preferences prefs;
      // Solo lectura: sin el namespace todavia (placa nueva) begin() falla y
      // getUInt devuelve el default.
      prefs.begin(Namespace, true);
      _last = prefs.getUInt(Key, 0);
      prefs.end();
      _loaded = true;
    }
    return _last;
  }

  inline uint32_t next() { return last() + 1; }

  inline void commit(uint32_t number) {
    if (number <= last()) return;
    Preferences prefs;
    if (!prefs.begin(Namespace, false) || prefs.putUInt(Key, number) == 0) {
      // Sigue en RAM: el numero vale para esta corrida, pero tras un
      // reinicio se repetiria.
      LOG_E(CORRIDA, "no se pudo guardar el numero de corrida #%lu en la NVS", (unsigned long)number);
    }
    prefs.end();
    _last = number;
  }
}
