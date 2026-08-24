#pragma once

// Multiplicadores usados para derivar el rango CRITICO de una fuente del
// Detector a partir de su rango NORMAL configurado (ver Engine::onCommand,
// caso Start). Centralizado aca para no tener "numeros magicos" sueltos en
// el codigo -- sigue siendo firmware fijo, no configurable desde la ESP32
// (a diferencia de la temperatura, que si tiene rangos normal/critico
// independientes elegidos por el operador). Exponerlo como parametro
// configurable desde la ESP32, o mover estos valores a un archivo de
// configuracion propio, queda como mejora futura (ver MOD-001/MOD-025 en
// Trello).
namespace SafetyMargins {
  // CEM1: rango critico = rango normal * este factor
  // (ej. tol=5% -> normal +-5%, critico +-6.25%).
  constexpr float CemCriticalMultiplier = 1.25f;
};
