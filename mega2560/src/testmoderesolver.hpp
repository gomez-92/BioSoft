#pragma once

// Bring-up temporal INT-001 (2026-08-31, ver tarjeta Trello "Prueba de
// integracion -- Control de intensidad CEM"): decodifica el parametro
// "testMode" (1-6) del comando start en los 3 ejes de comportamiento que
// Engine necesita aplicar. Extraido como funcion pura para poder testear la
// tabla en host (test_testmoderesolver) sin depender de un harness de
// Engine. Quitar junto con el resto del bring-up cuando termine.
struct TestModeConfig {
  bool useRealSensor;
  bool closedLoop;
  bool controlLoopEnabled;
};

inline TestModeConfig resolveTestMode(uint8_t testMode) {
  TestModeConfig cfg;
  cfg.useRealSensor = (testMode >= 4);
  cfg.closedLoop = (testMode == 3 || testMode == 6);
  cfg.controlLoopEnabled = cfg.closedLoop || testMode == 2 || testMode == 5;
  return cfg;
}
