// BioSoft - Banco de pruebas de las salidas de bobina (Mega2560)
//
// Herramienta de banco, NO firmware del sistema: no hay Engine, ni Detector,
// ni enlace con la ESP32, ni lazo de control. Cada salida hace exactamente lo
// que se le pide por el monitor serie, para medirla con osciloscopio,
// multimetro o un LED. Ver README.md.
//
// OJO: aca no vigila nadie. Probar con la etapa de potencia SIN alimentar (o
// desconectada). El unico corte es el pulsador de emergencia (pin 28), que
// apaga todo igual que en el firmware.

#include <Arduino.h>
#include <SPI.h>
#include <stdlib.h>
#include <string.h>

// Headers REALES del firmware (platformio.ini agrega -I ../../mega2560/src).
#include "pwmdriver.hpp"
#include "signalgenerator.hpp"

// ---- Pines: COPIA de SoftMega2560.ino. Si cambian alla, cambiarlos aca. ----
static const uint8_t PWM_PINS[4]    = {44, 45, 46, 6};
static const uint8_t ENABLE_PINS[4] = {23, 24, 25, 26};
static const uint8_t SPI_CS_PIN = 5;
static const uint8_t FIELD_MODE_PIN = 22;   // HIGH = campo nulo (rama invertida)
static const uint8_t RELE1_PIN = 3;         // activo en alto
static const uint8_t EMERGENCY_BUTTON_PIN = 28;  // pull-up, contacto NC: HIGH = presionado
static const uint16_t PWM_HZ = 3906;

SPIClass spi;
SignalGenerator generator(spi, SPI_CS_PIN);
PwmDriver pwm1(PWM_PINS[0], PWM_HZ);
PwmDriver pwm2(PWM_PINS[1], PWM_HZ);
PwmDriver pwm3(PWM_PINS[2], PWM_HZ);
PwmDriver pwm4(PWM_PINS[3], PWM_HZ);
PwmDriver* pwms[4] = {&pwm1, &pwm2, &pwm3, &pwm4};

// Estado que se muestra con 's'.
bool coilEnabled[4] = {false, false, false, false};
uint8_t coilDuty[4] = {0, 0, 0, 0};    // %
float sineHz = 0;                       // 0 = apagada
char wave = 's';                        // s seno, t triangulo, q cuadrada
bool nullMode = false;
bool relayOn = false;
bool lastEstop = false;

// ------------------------------------------------------------------ salidas

void setEnable(uint8_t i, bool on) {
  digitalWrite(ENABLE_PINS[i], on ? HIGH : LOW);
  coilEnabled[i] = on;
}

void setDuty(uint8_t i, uint8_t pct) {
  if (pct > 100) pct = 100;
  coilDuty[i] = pct;
  pwms[i]->write(pct / 100.0f);
}

void applyWave() {
  if (sineHz <= 0) { generator.off(); return; }
  switch (wave) {
    case 't': generator.setTriangleWave(sineHz); break;
    case 'q': generator.setSquareWave(sineHz); break;
    default:  generator.setSineWave(sineHz); break;
  }
}

void setMode(bool null) {
  nullMode = null;
  digitalWrite(FIELD_MODE_PIN, null ? HIGH : LOW);
}

void setRelay(bool on) {
  relayOn = on;
  digitalWrite(RELE1_PIN, on ? HIGH : LOW);
}

void allOff() {
  for (uint8_t i = 0; i < 4; i++) { setDuty(i, 0); setEnable(i, false); }
  sineHz = 0;
  applyWave();
  setMode(false);
  setRelay(false);
}

bool estopPressed() {
  return digitalRead(EMERGENCY_BUTTON_PIN) == HIGH;  // NC: apretar abre el contacto
}

// ------------------------------------------------------------------ texto

void printStatus() {
  Serial.println();
  for (uint8_t i = 0; i < 4; i++) {
    Serial.print(F("  BOB")); Serial.print(i + 1);
    Serial.print(F("  hab pin ")); Serial.print(ENABLE_PINS[i]);
    Serial.print(coilEnabled[i] ? F(" = HIGH") : F(" = LOW "));
    Serial.print(F("   PWM pin ")); Serial.print(PWM_PINS[i]);
    Serial.print(F(" = ")); Serial.print(coilDuty[i]); Serial.print(F(" %"));
    Serial.print(F("  (DC prom. ~")); Serial.print(5.0f * coilDuty[i] / 100.0f, 2);
    Serial.println(F(" V)"));
  }
  Serial.print(F("  AD9833      "));
  if (sineHz <= 0) Serial.println(F("apagado"));
  else {
    Serial.print(sineHz, 2); Serial.print(F(" Hz  "));
    Serial.println(wave == 't' ? F("triangular") : wave == 'q' ? F("cuadrada") : F("senoidal"));
  }
  Serial.print(F("  Selector 22 "));
  Serial.println(nullMode ? F("HIGH = campo NULO (rama invertida)") : F("LOW  = campo X (rama directa)"));
  Serial.print(F("  Rele 3      "));
  Serial.println(relayOn ? F("HIGH = cerrado") : F("LOW  = abierto"));
  Serial.print(F("  Pulsador 28 "));
  Serial.println(estopPressed() ? F("PRESIONADO") : F("suelto"));
  Serial.println();
}

void printHelp() {
  Serial.println(F("\nBanco de salidas de bobina - comandos (Enter al final):"));
  Serial.println(F("  e N 0|1   habilitacion de la bobina N (1-4; 0 = todas)"));
  Serial.println(F("  p N PCT   ciclo de trabajo PWM de la bobina N, 0-100 (0 = todas)"));
  Serial.println(F("  f HZ      senoidal del AD9833 a HZ (acepta decimales; 0 = apagar)"));
  Serial.println(F("  w s|t|q   forma de onda: seno / triangulo / cuadrada"));
  Serial.println(F("  m x|n     selector de fase (pin 22): campo X / campo nulo"));
  Serial.println(F("  r 0|1     interruptor general (pin 3)"));
  Serial.println(F("  a         secuencia automatica (cualquier tecla la corta)"));
  Serial.println(F("  s         estado     o  todo apagado     ?  esta ayuda"));
  Serial.println(F("Ej.: 'e 1 1' + 'p 1 50' -> pin 23 en alto y PWM 50 % en el 44."));
}

// ------------------------------------------------------------------ secuencia

// Espera ms milisegundos; devuelve false si hay que cortar (tecla o pulsador).
bool waitOrAbort(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    if (Serial.available()) { while (Serial.available()) Serial.read(); return false; }
    if (estopPressed()) return false;
  }
  return true;
}

void step(const __FlashStringHelper* msg) {
  Serial.print(F("  > ")); Serial.println(msg);
}

void runSequence() {
  Serial.println(F("\nSecuencia automatica (cualquier tecla corta). Pasos de 2 s."));
  allOff();
  bool ok = true;

  for (uint8_t i = 0; i < 4 && ok; i++) {
    Serial.print(F("BOB")); Serial.print(i + 1);
    Serial.print(F(": habilitacion pin ")); Serial.print(ENABLE_PINS[i]);
    Serial.print(F(", PWM pin ")); Serial.println(PWM_PINS[i]);
    setEnable(i, true);
    step(F("habilitacion HIGH, PWM 0 %"));
    ok = waitOrAbort(2000);
    static const uint8_t duties[] = {10, 25, 50, 75, 90};
    for (uint8_t d = 0; d < sizeof(duties) && ok; d++) {
      setDuty(i, duties[d]);
      Serial.print(F("  > PWM ")); Serial.print(duties[d]);
      Serial.print(F(" %  (DC prom. ~")); Serial.print(5.0f * duties[d] / 100.0f, 2);
      Serial.println(F(" V)"));
      ok = waitOrAbort(2000);
    }
    setDuty(i, 0);
    setEnable(i, false);
    if (ok) { step(F("PWM 0 %, habilitacion LOW")); ok = waitOrAbort(1000); }
  }

  static const float freqs[] = {10, 50, 100};
  for (uint8_t k = 0; k < 3 && ok; k++) {
    sineHz = freqs[k]; wave = 's'; applyWave();
    Serial.print(F("AD9833: senoidal ")); Serial.print(sineHz, 0); Serial.println(F(" Hz"));
    ok = waitOrAbort(3000);
  }

  if (ok) {
    sineHz = 50; applyWave();
    Serial.println(F("Selector 22 con senoidal 50 Hz (mirar la salida hacia bobinas 2 y 4)"));
    for (uint8_t k = 0; k < 2 && ok; k++) {
      setMode(true);  step(F("campo NULO (22 HIGH): salida invertida")); ok = waitOrAbort(2000);
      if (!ok) break;
      setMode(false); step(F("campo X (22 LOW): salida directa"));     ok = waitOrAbort(2000);
    }
  }

  if (ok) {
    Serial.println(F("Interruptor general (pin 3)"));
    setRelay(true);  step(F("HIGH (rele cerrado)")); ok = waitOrAbort(2000);
    setRelay(false); if (ok) step(F("LOW (rele abierto)"));
  }

  allOff();
  Serial.println(ok ? F("Secuencia terminada. Todo apagado.") : F("Secuencia CORTADA. Todo apagado."));
}

// ------------------------------------------------------------------ comandos

void handleLine(char* line) {
  char cmd = line[0];
  char* rest = line + 1;
  char* end;
  long a = strtol(rest, &end, 10);
  bool hasA = end != rest;
  char* rest2 = end;
  long b = strtol(rest2, &end, 10);
  bool hasB = end != rest2;

  switch (cmd) {
    case 'e':
    case 'p': {
      if (!hasA || !hasB || a < 0 || a > 4) { Serial.println(F("? uso: e N 0|1  /  p N PCT (N 1-4, 0 = todas)")); return; }
      for (uint8_t i = 0; i < 4; i++) {
        if (a != 0 && i != a - 1) continue;
        if (cmd == 'e') setEnable(i, b != 0);
        else setDuty(i, (uint8_t)constrain(b, 0, 100));
      }
      break;
    }
    case 'f': {
      float hz = strtod(rest, &end);
      if (end == rest || hz < 0 || hz > 12500000.0f) { Serial.println(F("? uso: f HZ (0 = apagar)")); return; }
      sineHz = hz; applyWave();
      break;
    }
    case 'w': {
      char* p = rest; while (*p == ' ') p++;
      if (*p != 's' && *p != 't' && *p != 'q') { Serial.println(F("? uso: w s|t|q")); return; }
      wave = *p;
      if (sineHz <= 0) Serial.println(F("(forma guardada; prender con 'f HZ')"));
      applyWave();
      break;
    }
    case 'm': {
      char* p = rest; while (*p == ' ') p++;
      if (*p != 'x' && *p != 'n') { Serial.println(F("? uso: m x|n")); return; }
      setMode(*p == 'n');
      break;
    }
    case 'r':
      if (!hasA) { Serial.println(F("? uso: r 0|1")); return; }
      setRelay(a != 0);
      break;
    case 'a': runSequence(); return;
    case 'o': allOff(); break;
    case 's': break;
    case '?': case 'h': printHelp(); return;
    default:
      Serial.println(F("? comando desconocido ('?' = ayuda)"));
      return;
  }
  printStatus();
}

// ------------------------------------------------------------------ setup/loop

void setup() {
  // Antes que nada, todo en reposo (mismo nivel seguro que el firmware).
  for (uint8_t i = 0; i < 4; i++) { pinMode(ENABLE_PINS[i], OUTPUT); digitalWrite(ENABLE_PINS[i], LOW); }
  pinMode(FIELD_MODE_PIN, OUTPUT); digitalWrite(FIELD_MODE_PIN, LOW);
  pinMode(RELE1_PIN, OUTPUT);      digitalWrite(RELE1_PIN, LOW);
  pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  spi.begin();
  generator.begin();
  generator.off();

  // enable() fija el prescaler (3906 Hz) del timer de cada pin; duty en 0.
  for (uint8_t i = 0; i < 4; i++) { pwms[i]->enable(); pwms[i]->write(0); }

  Serial.println(F("\n=== BioSoft - banco de salidas de bobina (Mega2560) ==="));
  Serial.println(F("NO es el firmware: nada vigila. Etapa de potencia SIN alimentar."));
  printHelp();
  lastEstop = estopPressed();
  printStatus();
}

void loop() {
  bool estop = estopPressed();
  if (estop && !lastEstop) {
    allOff();
    Serial.println(F("\n!! Pulsador de emergencia: todo apagado."));
    printStatus();
  }
  lastEstop = estop;

  static char buf[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[len] = '\0';
      if (len > 0) { Serial.print(F("> ")); Serial.println(buf); handleLine(buf); }
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}
