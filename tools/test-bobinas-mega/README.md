# Banco de salidas de bobina (Mega2560)

Proyecto PlatformIO independiente que maneja **a mano**, desde el monitor
serie, las salidas del Mega hacia la etapa de potencia, para medirlas antes
del ensayo E2 del protocolo de laboratorio.

**Es una herramienta de banco, no firmware del sistema.** No hay Engine, ni
Detector, ni lazo, ni enlace con la ESP32: nada vigila. Probar con la etapa de
potencia **sin alimentar o desconectada**. El pulsador de emergencia (pin 28)
apaga todo, igual que en el firmware.

```
cd tools/test-bobinas-mega
pio run -t upload
pio device monitor
```

Después hay que volver a subir el firmware real (`cd mega2560 && pio run -t upload`).

## Comandos (Enter al final)

| Comando | Qué hace |
|---|---|
| `e N 0\|1` | Habilitación de la bobina N (pines 23-26; `0` = todas) |
| `p N PCT` | Ciclo de trabajo del PWM de la bobina N, 0-100 (pines 44/45/46/6, 3906 Hz) |
| `f HZ` | Senoidal del AD9833 a HZ (`f 0` la apaga) |
| `w s\|t\|q` | Forma de onda: seno / triángulo / cuadrada |
| `m x\|n` | Selector de fase, pin 22: campo X (LOW) / campo nulo (HIGH) |
| `r 0\|1` | Interruptor general, pin 3 (activo en alto) |
| `a` | Secuencia automática: cada bobina (habilitación + PWM 10/25/50/75/90 %), senoidal 10/50/100 Hz, selector y relé. Cualquier tecla la corta |
| `s` / `o` / `?` | Estado / todo apagado / ayuda |

## Qué esperar

- **Habilitaciones, pin 22, pin 3:** 0 V o 5 V con el multímetro (o un LED con 1 kΩ).
- **PWM:** en el osciloscopio, 3906 Hz y el ciclo pedido (C2, C3). Con multímetro
  en DC se ve el promedio, ≈ ciclo × 5 V; el estado lo muestra.
- **AD9833:** senoidal a la frecuencia pedida (C1). La amplitud depende del
  módulo (los comunes dan ~0,6 Vpp sobre ~0,3 V de continua). La cuadrada
  (`w q`) sale a nivel lógico y ayuda a ver si el chip responde.
- **Selector:** con senoidal prendida, la salida hacia bobinas 2 y 4 se
  invierte al pasar a `m n`.

Los pines están **copiados** de `mega2560/src/SoftMega2560.ino`; si cambian
allá, cambiarlos en `src/main.cpp`. El PWM y el AD9833 usan los headers reales
del firmware (`pwmdriver.hpp`, `signalgenerator.hpp`) vía `-I`.
