#pragma once

// Multiplicadores usados para derivar el rango CRITICO de una fuente del
// Detector a partir de su rango NORMAL configurado (ver Engine::onCommand,
// caso Start). Centralizado aca para no tener "numeros magicos" sueltos en
// el codigo.
//
// Ya NO es firmware fijo: es el DEFAULT COMPILADO del multiplicador, que el
// archivo de la SD puede pisar por fuente via config_source
// (criticalMultiplier, ver docs/config-schema.md seccion 7). Sigue vigente
// tal cual si no hay tarjeta o si la clave no viene en el archivo.
namespace SafetyMargins {
  // CEM1: rango critico = rango normal * este factor
  // (ej. tol=5% -> normal +-5%, critico +-6.25%).
  constexpr float CemCriticalMultiplier = 1.25f;
};
