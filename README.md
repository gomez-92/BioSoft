# BioSoft

Proyecto final de Bioingeniería (Mario) — sistema de exposición controlada a
campo electromagnético de baja frecuencia para animales de experimentación,
en el marco de una investigación de la facultad.

Dos placas, un mismo experimento:

- **`esp32/`** — pantalla táctil (LVGL) que muestra el estado del sistema,
  permite configurar los parámetros del experimento y envía datos para
  monitoreo remoto por MQTT.
- **`mega2560/`** — control físico: genera el campo (bobina), mide campo /
  corriente / temperatura, y decide cuándo cortar el experimento (por
  tiempo cumplido, por peligro, o por pérdida de rigor científico).

Ambas placas se comunican por serial con un protocolo de frames propio
(`seriallink.hpp`, duplicado a propósito en cada placa).

## Compilar

Cada carpeta es un proyecto PlatformIO independiente:

```
cd esp32 && pio run
cd mega2560 && pio run
```

## Credenciales

`esp32/include/secrets.h` (WiFi + MQTT) no se versiona. Copiar
`esp32/include/secrets.example.h` como `secrets.h` y completar con los
valores reales antes de compilar.
