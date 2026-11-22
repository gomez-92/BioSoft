# Librerías locales del Mega

## Adafruit_MLX90393 (copia de 2.0.5)

Es una copia de `adafruit/Adafruit MLX90393@2.0.5` con **un solo cambio**: las
tablas `mlx90393_lsb_lookup` (512 bytes) y `mlx90393_tconv` (128 bytes) se
guardan en flash (`PROGMEM`) y se leen con `pgm_read_float`. En la versión
original son constantes en RAM: 640 bytes de los ~1850 que el Mega tiene
libres para heap y stack. Ese margen es lo que se agotaba en los cuelgues al
terminar un experimento (ver CLAUDE.md, sección del Mega y `src/diag.hpp`).

Por eso la librería ya no figura en `lib_deps` de `platformio.ini`: si vuelve
a figurar, PlatformIO puede elegir esa en lugar de esta copia y se pierden
los 640 bytes sin ningún error.

Hay que tener en cuenta dos cosas:

- Si una tabla se indexa directamente fuera de esta librería (sin
  `pgm_read_float`), compila igual y lee basura de RAM. Hoy la usa un solo
  lugar: `src/magnetometermlx90993.hpp`.
- El bug de `begin_I2C()` (llamarla dos veces usa un puntero ya liberado)
  **no** está corregido acá. Lo esquiva `MagnetometerMlx90393::begin()`, que
  la llama una sola vez.

Para actualizar la librería: copiar la versión nueva y volver a aplicar el
mismo cambio (`PROGMEM` en las dos tablas y `pgm_read_float` en los cuatro
lugares donde se leen).
