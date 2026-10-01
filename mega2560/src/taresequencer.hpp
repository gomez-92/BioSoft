#pragma once

#include "fieldtare.hpp"

// Secuencia de tiempos de la tara del campo ambiente: cuando tomar cada
// lectura, cuando darse por vencido y que pasa si llega un start repetido o
// un stop. Es la parte de la tara que NO es el calculo (eso es FieldTare) ni
// el hardware (eso es Engine): una maquina de tiempos pura, sin Arduino.h,
// que recibe `now` en milisegundos para poder probarla en el host, incluida
// la vuelta del contador de millis().
//
// Uso (ver Engine::_updateTare):
//   begin(now, cadencia)          al recibir el start
//   poll(now)                     en cada vuelta de loop()
//     TakeSample -> leer el sensor y avisar con report(estado de la tara)
//     Timeout    -> la tara no termino a tiempo
//   cancel()                      en stop / reset
enum class TareAction : uint8_t { None, TakeSample, Timeout };
enum class TareOutcome : uint8_t { Pending, Ready, Rejected };

class TareSequencer {
  private:
    bool _pending = false;
    unsigned long _startedMs = 0;
    unsigned long _nextSampleMs = 0;
    unsigned long _cadenceMs = 0;
    unsigned long _timeoutMs = TareTiming::TimeoutMs;

    // Comparacion de tiempos que sobrevive a la vuelta de millis() (~49 dias).
    static bool _reached(unsigned long now, unsigned long target) {
      return (long)(now - target) >= 0;
    }

  public:
    // Arranca la secuencia. Devuelve false, sin tocar nada, si ya hay una en
    // curso: la ESP32 reenvia start hasta recibir el ack, y un start repetido
    // no puede reiniciar la tara a mitad de camino.
    bool begin(unsigned long now, unsigned long cadenceMs,
               unsigned long settleMs = TareTiming::SettleMs,
               unsigned long timeoutMs = TareTiming::TimeoutMs) {
      if (_pending) return false;
      _pending = true;
      _startedMs = now;
      _cadenceMs = cadenceMs;
      _timeoutMs = timeoutMs;
      // La primera lectura espera el asentamiento, no una cadencia entera.
      _nextSampleMs = now + settleMs;
      return true;
    }

    // Aborta la secuencia (stop, reset, o fin ya resuelto).
    void cancel() { _pending = false; }

    bool pending() const { return _pending; }

    // Que hacer en esta vuelta. El vencimiento tiene prioridad sobre una
    // lectura que justo tocaba: si ya se paso de tiempo, no se lee mas.
    // Despues de Timeout la secuencia queda terminada.
    TareAction poll(unsigned long now) {
      if (!_pending) return TareAction::None;
      if ((unsigned long)(now - _startedMs) > _timeoutMs) {
        _pending = false;
        return TareAction::Timeout;
      }
      if (!_reached(now, _nextSampleMs)) return TareAction::None;
      _nextSampleMs = now + _cadenceMs;
      return TareAction::TakeSample;
    }

    // Estado de la tara despues de una lectura. Collecting sigue pendiente;
    // Ready y Failed terminan la secuencia. Cualquier otro estado (Idle: el
    // sensor se limpio por debajo) cuenta como rechazo, porque seguir
    // esperando una tara que ya no se esta juntando solo agotaria el timeout.
    TareOutcome report(TareStatus status) {
      if (!_pending) return TareOutcome::Pending;
      if (status == TareStatus::Collecting) return TareOutcome::Pending;
      _pending = false;
      return status == TareStatus::Ready ? TareOutcome::Ready : TareOutcome::Rejected;
    }
};
