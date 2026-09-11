# Banco de pruebas del módulo SD (ESP32)

Proyecto PlatformIO independiente que ejercita `esp32/src/sdstorage.hpp`
contra una tarjeta real y reporta el resultado por el monitor serie.

**Es una herramienta de banco, no firmware del sistema.** No levanta
pantalla, ni WiFi, ni MQTT, ni el enlace serie con el Mega. Sirve para
responder una sola pregunta: *¿el módulo SD y su cableado funcionan?*

## Correrlo

```
cd tools/test-sd-esp32
pio run -t upload
pio device monitor
```

No necesita `secrets.h` ni ninguna librería externa: `SD` y `SPI` vienen con
el core de Arduino-ESP32.

Las pruebas corren solas al arrancar. **Enter en el monitor las vuelve a
correr** sin resetear la placa, que es lo que se quiere en el banco cuando
uno saca y pone la tarjeta, cambia un cable o prueba otra SD.

## Por qué incluye el header real y no una copia

El `platformio.ini` agrega `-I ../../esp32/src` y hace `#include
"sdstorage.hpp"` del archivo **real** del firmware. Si el banco tuviera su
propia copia, terminaría validando una versión vieja mientras la placa corre
otra — la misma deriva que ya pasó con los dos `seriallink.hpp`. Como
`sdstorage.hpp` es header-only (`inline`), alcanza con el include path.

Consecuencia: si cambiás `sdstorage.hpp`, este banco lo prueba en la
siguiente compilación, sin tocar nada acá.

## Qué prueba

| Bloque | Qué verifica |
|---|---|
| Montaje | `begin()` monta la tarjeta y `isReady()` queda en `true`. Si falla, **aborta** e imprime un diagnóstico ordenado (cableado, formato, tamaño) — sin tarjeta montada el resto no prueba nada, solo repetiría el mismo fallo |
| Información | Tipo de tarjeta, tamaño físico y uso del filesystem |
| Escritura/lectura | `writeFile` + `exists` + `readFile` con contenido y longitud exactos |
| Reescritura | Que `writeFile` **trunque** y no anexe (ver abajo) |
| Anexado | `appendFile` suma al final, y crea el archivo si no existía |
| Límite del buffer | `readFile` acepta un buffer de `len+1` y **rechaza** uno de `len`; rechaza `maxLength = 0` y buffer nulo |
| Archivo inexistente | `exists`, `readFile` y `remove` devuelven `false` |
| Borrado | `remove` borra y `exists` deja de encontrarlo |
| Archivo vacío | Un archivo de 0 bytes se lee OK, con longitud 0 |
| Archivo grande | 8192 bytes (el tamaño del buffer de config del firmware) ida y vuelta, byte a byte, con tiempos de escritura y lectura |

Después de las pruebas, y fuera del conteo, el banco **muestra
`/biosoft/config.json`** si está en la tarjeta: tamaño, si entra en los 8 KB
del buffer de `ConfigLoader`, y el contenido completo. Es la forma de
verificar que un archivo copiado a la SD llegó entero sin tener lector de
SD en la PC — lo que ve el banco es exactamente lo que va a leer el
firmware al arrancar.

### Las dos pruebas que más valen

**La reescritura.** `sdstorage.hpp` tiene anotado que *asume* que
`FILE_WRITE` trunca (semántica `"w"` del core arduino-esp32 3.x) y deja la
suposición sin verificar. Esta es la prueba que la verifica: si el core
resultara ser append-only, un `config.json` reescrito quedaría con la cola
del anterior pegada al final y el JSON dejaría de parsear.

**El límite del buffer.** Es el motivo por el que `readFile` devuelve
`false` en vez de truncar: quien lee un archivo de configuración no puede
distinguir "JSON inválido" de "JSON cortado por buffer chico" si esto
devolviera `true` — y el síntoma, arrancar con los defaults, sería el mismo
en los dos casos.

## Qué deja en la tarjeta

Archivos en la **raíz**, con prefijo `bstest_`, borrados al terminar (la
última prueba confirma que no quedó ninguno). No usa un subdirectorio propio
porque `SdStorage` no expone `mkdir`.

**No toca `/biosoft/config.json`** ni ningún otro archivo que no haya creado
él mismo.

## Leyendo la salida

Las pruebas cuyo resultado correcto *es* un fallo (buffer chico, archivo que
no entra) hacen que `sdstorage.hpp` imprima su propia línea `[SD] ERROR:
...`, porque tiene `DEBUG_SDSTORAGE` en `true`. Es lo esperado, y el banco
avisa justo antes de cada una de esas pruebas para que no se confunda con un
problema real.

Al final: `RESULTADO: N de M pruebas OK`.

## Qué NO prueba

- **La convivencia con el TFT.** El banco nunca inicializa la pantalla.
  La SD va por VSPI (18/19/23) y el TFT por HSPI (12/13/14), así que no
  comparten bus, pero sí comparten CPU y `loopTask`: si algo se pisa en el
  firmware real, acá no se va a ver.
- El `ConfigLoader` y el parseo de `config.json`.
- El envío fragmentado de configuración al Mega por el enlace serie.
