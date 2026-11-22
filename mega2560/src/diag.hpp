#pragma once

#include <Arduino.h>

// Diagnostico de cuelgues y reinicios del Mega (2026-11-21).
//
// Sintoma en banco: al terminar un experimento la ESP32 perdia el enlace
// (icono tachado) y solo volvia desenergizando; otras veces el monitor
// quedaba con la corrida abierta y la placa estaba en Principal. Las dos
// cosas son compatibles con el Mega quedandose sin RAM (heap y stack
// pisandose: ~1850 bytes libres sobre 8 KB y el corte es el camino de stack
// mas profundo, ~650 bytes) -- a veces se cuelga, a veces salta a 0 y
// arranca de nuevo. Este modulo da tres cosas:
//
//  1) Watchdog de 2 s: un Mega colgado se reinicia solo (y al reiniciar los
//     pines quedan en alta impedancia: rele y enables con pull-down, es
//     decir bobinas CORTADAS). Antes un cuelgue a mitad de corrida dejaba
//     la ultima excitacion puesta, sin nadie mirando.
//  2) La "miga" (Diag::crumb, etapa del loop en curso) en .noinit, que
//     sobrevive a un reset que no sea de encendido: dice DONDE estaba
//     cuando murio. MCUSR se captura en .init3, pero con el bootloader
//     stk500v2 del Mega llega SIEMPRE en 0: el bootloader lo lee, lo borra
//     para apagar el watchdog y salta al programa sin pasarlo (banco,
//     2026-11-22: una carga de firmware se leia como "salto a 0"). Por eso
//     la causa no se puede saber; un encendido si (la RAM no trae la marca
//     de la sesion anterior), y un reinicio a mitad de corrida lo detecta la
//     ESP32 porque `up` baja. Se probo y se descarto el modo interrupcion +
//     reset del watchdog para anotar la causa: si el cuelgue es con las
//     interrupciones apagadas, ese modo no reinicia nunca.
//  3) Marca de agua del stack: se pinta la RAM libre al arrancar y se busca
//     hasta donde llego el stack. Viaja en state_data (ver
//     Engine::_sendState) para leerlo desde la ESP32.
//
// OJO con el bootloader: el stk500v2 viejo de algunos Mega (y clones) no
// apaga el watchdog tras un reset por WDT y queda reiniciandose en bucle
// (LED parpadeando, nunca llega a setup). Se prueba una vez en banco
// (docs/puesta-en-marcha.html); si pasa, hay que actualizar el bootloader.
// En el peor caso es lo mismo de hoy: desenergizar.

#ifdef __AVR__
#include <avr/wdt.h>
#endif

namespace Diag {

  // Etapas de Diag::crumb. Un numero por paso de Engine::update() y por
  // tramo del corte, que es donde se sospecha el problema.
  enum Crumb : uint8_t {
    CrumbBoot = 1,
    CrumbTimer = 2,
    CrumbSerial = 3,
    CrumbButton = 4,
    CrumbMagnetometer = 5,
    CrumbThermometer = 6,
    CrumbSilence = 7,
    CrumbFinish = 10,
    CrumbResult = 11,
    CrumbReset = 12,
    CrumbIdle = 20,
  };

  constexpr uint8_t StackPaint = 0xC5;
  constexpr uint16_t CrumbMagic = 0xB105;

#ifdef __AVR__
  // .noinit: el core no los pone en cero al arrancar.
  extern volatile uint8_t crumb;
  extern volatile uint16_t crumbMagic;
#else
  inline volatile uint8_t crumb = 0;
  inline volatile uint16_t crumbMagic = 0;
#endif

  // Lo que se leyo al arrancar (ver begin()).
  inline uint8_t resetFlags = 0;
  inline uint8_t previousCrumb = 0;
  // true si la RAM no traia la marca de una sesion anterior: encendido.
  inline bool powerOn = false;

  inline void mark(uint8_t value) { crumb = value; }

  // Llamar al principio de setup(). La causa ya fue capturada en .init3.
  inline void begin();
  // Llamar al final de setup(), despues de todo lo que bloquea (delay,
  // escaneos de bus, begin de sensores).
  inline void enableWatchdog();
  inline void feed() {
#ifdef __AVR__
    wdt_reset();
#endif
  }

  // Bytes de stack que se llegaron a usar desde el arranque (maximo).
  inline uint16_t stackHighWater();
  // Bytes libres entre el tope del heap y el punto mas bajo que alcanzo el
  // stack. Es el margen REAL que queda: si llega a ~0, se pisan.
  inline int16_t minGap();
  // Libres ahora (heap top -> stack actual).
  inline int16_t freeNow();
}

#ifdef __AVR__

namespace Diag {
  volatile uint8_t crumb __attribute__((section(".noinit")));
  volatile uint16_t crumbMagic __attribute__((section(".noinit")));
  uint8_t mcusrAtBoot __attribute__((section(".noinit")));
}

// Corre antes que el constructor de cualquier global y antes de init(): tras
// un reset por WDT el watchdog sigue encendido con el tiempo minimo (16 ms) y
// hay que apagarlo antes de que el arranque normal lo dispare de nuevo.
void diagEarlyInit() __attribute__((naked, used, section(".init3")));
void diagEarlyInit() {
  Diag::mcusrAtBoot = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

extern char __heap_start;
extern char* __brkval;

namespace Diag {

  inline char* _heapTop() {
    return __brkval == 0 ? &__heap_start : __brkval;
  }

  inline void begin() {
    resetFlags = mcusrAtBoot;
    // La miga solo vale si sobrevivio a un reset que no sea de encendido
    // (en un power-on la RAM es basura y la marca no coincide). PORF casi
    // nunca llega (el bootloader borra MCUSR): decide la marca.
    powerOn = (resetFlags & _BV(PORF)) != 0 || crumbMagic != CrumbMagic;
    previousCrumb = powerOn ? 0 : crumb;
    crumbMagic = CrumbMagic;
    crumb = CrumbBoot;

    // Pinta desde el tope actual del heap hasta un poco debajo del stack
    // actual. Lo que el heap ocupe despues lo pisa, y por eso las mediciones
    // buscan desde arriba (ver stackHighWater).
    char marker;
    char* end = &marker - 32;
    for (char* p = _heapTop(); p < end; p++) *p = (char)StackPaint;
  }

  inline void enableWatchdog() {
    wdt_enable(WDTO_2S);
  }

  // Punto mas bajo que toco el stack: bajando desde el stack actual, el
  // primer tramo de 16 bytes seguidos con la pintura intacta. Pedir 16
  // seguidos evita que un byte viejo de stack que justo valga 0xC5 corte la
  // busqueda antes de tiempo.
  inline char* _stackLow() {
    char marker;
    char* p = &marker;
    char* floor = _heapTop();
    uint8_t run = 0;
    while (p > floor) {
      p--;
      if ((uint8_t)*p == StackPaint) {
        if (++run >= 16) return p + 16;
      } else {
        run = 0;
      }
    }
    return floor;
  }

  inline uint16_t stackHighWater() {
    return (uint16_t)((char*)RAMEND - _stackLow());
  }

  inline int16_t minGap() {
    return (int16_t)(_stackLow() - _heapTop());
  }

  inline int16_t freeNow() {
    char marker;
    return (int16_t)(&marker - _heapTop());
  }
}

#else

namespace Diag {
  inline void begin() {}
  inline void enableWatchdog() {}
  inline uint16_t stackHighWater() { return 0; }
  inline int16_t minGap() { return -1; }
  inline int16_t freeNow() { return -1; }
}

#endif
