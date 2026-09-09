# Esquema de configuracion en tarjeta SD

Contrato del archivo JSON que la aplicacion externa genera, se graba en la
tarjeta SD de la ESP32 y el firmware lee al arrancar. Define QUE datos dejan
de ser constantes de compilacion y pasan a ser configurables sin reflashear
las placas.

- Tarjetas Trello: "Relevamiento de datos parametrizables (archivo SD)"
  (https://trello.com/c/ZrXfb2m2) y "Esquema JSON de configuracion (archivo
  SD)" (https://trello.com/c/QsXSZsM4).
- Consumidores del contrato: la app externa lo **genera**, la ESP32 lo **lee**
  (`SdStorage`, `esp32/src/sdstorage.hpp`), el Mega2560 **recibe un
  subconjunto** por el protocolo serie.
- Este documento define el contrato. La implementacion vive en las tarjetas
  "Migracion de datos configurables a memoria SD" y "Aplicacion externa para
  crear archivo de configuracion".

Ubicacion del archivo en la SD: `/biosoft/config.json`.

---

## 1. Principios

### 1.1 Los defaults compilados nunca desaparecen

Todos los valores que hoy son `constexpr` **siguen existiendo en el firmware**
como respaldo. El archivo de la SD solo **pisa** las claves que trae. En
consecuencia:

- Si no hay tarjeta SD, o `SD.begin()` falla, el sistema arranca con los
  valores de hoy y lo deja asentado en el log.
- Si el archivo existe pero el JSON no parsea, se descarta **entero** y se
  usan los defaults. No se aplica una configuracion a medias.
- Si el JSON parsea pero le falta una seccion, esa seccion queda en defaults.
- Si a una seccion le falta una clave, esa clave queda en su default.

No hay un modo "arranque bloqueado por falta de configuracion": el equipo
siempre debe poder correr un experimento con parametros conocidos y auditados.

### 1.2 El archivo no decide un experimento

El archivo configura el **entorno** (que opciones ofrece la pantalla, cada
cuanto se mide, que considera el Detector una lectura anormal). Los parametros
de una corrida concreta (intensidad, frecuencia y duracion elegidas por el
operador) siguen viajando en el comando `start`, como hoy.

### 1.3 Todo tope de memoria es de compilacion

La ESP32 convive con LVGL y el framebuffer del TFT: no se reserva memoria en
funcion de lo que traiga el archivo. Cada coleccion del esquema tiene un
maximo fijo; lo que exceda ese maximo se ignora y se registra en el log.

### 1.4 Fuera de alcance

`ConfigurationOptions::optionsTestMode` (combo de bring-up de INT-001) **no se
parametriza**: esta marcado para eliminarse junto con el resto del combo de
`testMode` cuando terminen las 6 pruebas de laboratorio.

---

## 2. Estructura general

```json
{
  "schemaVersion": 1,
  "menus": { },
  "intervals": { "esp32": { }, "mega": { } },
  "detector": { "sources": [ ] },
  "currentSensors": [ ],
  "telemetry": { }
}
```

`schemaVersion` es un entero obligatorio. Si falta, o no coincide con la
version que el firmware entiende, el archivo se descarta entero y se usan los
defaults (mismo camino que un JSON corrupto). Version actual: **1**.

---

## 3. Seccion `menus` — opciones del operador

Reemplaza los 6 arrays `constexpr` de `esp32/src/configurationoptions.hpp`.
Solo ESP32; el Mega no ve esta seccion.

Cada entrada tiene un `label` (lo que muestra el dropdown de la pantalla
Configuracion) y su valor asociado. **Maximo 8 opciones por menu**; a partir
de la novena se ignoran. Un menu presente pero vacio (`[]`) se trata como
ausente y cae a su default.

```json
"menus": {
  "fieldIntensity":     [ { "label": "1.0 mT",  "value": 1.0 } ],
  "frequency":          [ { "label": "10 Hz",   "value": 10 } ],
  "duration":           [ { "label": "00h 05m", "value": 300000 } ],
  "fieldTolerance":     [ { "label": "5%",      "value": 5 } ],
  "temperatureNormal":  [ { "label": "30~40 C", "min": 30.0, "max": 40.0 } ],
  "temperatureCritical":[ { "label": "25~45 C", "min": 25.0, "max": 45.0 } ]
}
```

| Clave | Tipo de `value` | Unidad | Origen actual |
|---|---|---|---|
| `fieldIntensity` | float | mT | `optionsFieldIntensity[]` |
| `frequency` | int | Hz | `optionsFrequency[]` |
| `duration` | unsigned long | ms | `optionsDuration[]` |
| `fieldTolerance` | int | % | `optionsTolFieldIntensity[]` |
| `temperatureNormal` | float `min`/`max` | grados C | `optionsRangeNormalTemperature[]` |
| `temperatureCritical` | float `min`/`max` | grados C | `optionsRangeCriticalTemperature[]` |

`label` se limita a 15 caracteres mas terminador; lo que exceda se trunca.

**Nota de implementacion.** Los arrays pasan a ser buffers estaticos de 8
elementos con un contador de largo real. Esto elimina de paso el riesgo ya
conocido de `buildDropdown()` (`esp32/src/screencontroller.hpp`), que hoy
recibe la cantidad de opciones como literal escrito a mano en cada llamada:
ya fallo una vez cuando la lista de tolerancia paso de 3 a 2 entradas.

---

## 4. Seccion `intervals` — periodos de tareas

Reemplaza los `constexpr` de `esp32/src/intervals.hpp` y
`mega2560/src/intervals.hpp`. Todos los valores en **milisegundos**, enteros
positivos. Un `0` o un valor negativo se rechaza y esa clave queda en default.

```json
"intervals": {
  "esp32": {
    "updateScreens": 50,
    "reSendStart": 4000,
    "reSendStop": 4000,
    "reSendReset": 4000,
    "updateProgress": 5000
  },
  "mega": {
    "ping": 2000,
    "sendState": 5000,
    "measureTemperature": 5000,
    "measureCurrent": 8500,
    "measureMagneticField": 500,
    "updateProgress": 1000,
    "sendFlags": 25000,
    "sendResult": 1000,
    "settlingTime": 10000
  }
}
```

Los intervalos de publicacion remota **no** viven aca: son parte de la seccion
`telemetry` (seccion 7), junto a los campos que publican.

`intervals.mega` es la primera de las porciones que viajan al Mega2560 (ver
seccion 8).

### Advertencias de acoplamiento

- `measureTemperature` no baja de 1000 ms: `ThermometerDS18B20::update()`
  bloquea ~750 ms por llamada, sin importar si hay sensor en el bus.
- `settlingTime` es la ventana ciega inicial en la que las muestras se
  registran pero no llegan al Detector. Bajarla acerca el primer flag posible
  al arranque del experimento, cuando el lazo de control todavia no convergio.
- `measureTemperature` y el `threshold` de la regla `frequency` de TEMP1
  determinan juntos cuanto tiempo real cubre la ventana del Detector. Cambiar
  uno sin revisar el otro corre el significado de la regla.

---

## 5. Seccion `detector.sources` — fuentes y reglas

Parametriza lo que hoy arma `mega2560/src/detectorconfigbuilder.hpp`. Es la
seccion de mayor impacto en seguridad: define cuando el sistema considera que
una lectura es anormal y cuando corta el experimento.

**Maximo 2 fuentes** (`CEM1` y `TEMP1`). El tipo y la cantidad de fuentes son
fijos por diseno; lo configurable es el sensor asociado, los rangos y las
reglas.

```json
"detector": {
  "sources": [
    {
      "name": "CEM1",
      "enabled": false,
      "sensor": "sim",
      "bufferSize": 32,
      "range": { "mode": "derived", "criticalMultiplier": 1.25 },
      "rules": {
        "critical":  { "threshold": 3,  "cooldown": 1,  "maxEvents": 2 },
        "streak":    { "threshold": 5,  "cooldown": 4,  "maxEvents": 5 },
        "frequency": { "threshold": 16, "cooldown": 16, "maxEvents": 3 }
      }
    },
    {
      "name": "TEMP1",
      "enabled": true,
      "sensor": "ds18b20",
      "bufferSize": 32,
      "range": { "mode": "operator" },
      "rules": {
        "critical":  { "threshold": 3,  "cooldown": 1,  "maxEvents": 2 },
        "streak":    { "threshold": 5,  "cooldown": 4,  "maxEvents": 5 },
        "frequency": { "threshold": 16, "cooldown": 16, "maxEvents": 3 }
      }
    }
  ]
}
```

### `enabled`

Reemplaza el `detector.addSource("CEM1")` comentado a mano en
`SoftMega2560.ino`. Con `false`, la fuente no se registra: se sigue midiendo y
enviando a la pantalla, pero no puede generar flags ni cortar el experimento.
Es el interruptor que hoy obliga a recompilar durante un bring-up sin `A0`
cableado.

### `sensor`

Que driver alimenta la fuente. Reemplaza el combo temporal de `testMode`
(`testmoderesolver.hpp`) por una eleccion explicita en configuracion.

| Fuente | Valores validos |
|---|---|
| `CEM1` | `"sim"` (`MagnetometerVoltageSim`, entrada analogica) o `"mlx90393"` (sensor real por I2C) |
| `TEMP1` | `"ds18b20"` |

### `range`

Como se obtienen los limites normal y critico. Dos modos, uno por fuente, no
intercambiables:

- **`"derived"`** (solo CEM1): los rangos se calculan desde el target y la
  tolerancia elegidos por el operador en el `start`. `criticalMultiplier` es
  el factor que hoy es firmware fijo en `SafetyMargins::CemCriticalMultiplier`
  (1.25, es decir tolerancia 5% da normal +-5% y critico +-6.25%). Rango
  aceptado: `1.0` a `3.0`.
- **`"operator"`** (solo TEMP1): los rangos vienen tal cual de los menus que
  eligio el operador (`temperatureNormal` / `temperatureCritical`), sin
  derivacion.

### `rules`

Las tres reglas del Detector, con sus tres campos cada una. `Engine::_readRule()`
(`mega2560/src/engine.hpp`) ya sabe parsear exactamente esta forma.

| Regla | Que cuenta | `threshold` |
|---|---|---|
| `critical` | muestras **criticas** consecutivas | cuantas seguidas disparan un flag |
| `streak` | muestras fuera de lo normal (no criticas) consecutivas | cuantas seguidas disparan un flag |
| `frequency` | muestras no normales dentro de la ventana `bufferSize`, sin importar el orden | cuantas dentro de la ventana disparan un flag |

`cooldown` es la cantidad de muestras que la regla espera antes de volver a
evaluar tras disparar. `maxEvents` es el numero de flags de esa regla que
terminan el experimento: al alcanzarlo, `Engine::_finish()` corta con
`reason="critical"`.

### Clamps de seguridad

El Mega valida cada valor recibido y descarta la regla completa si algo cae
fuera de rango, quedando esa regla en su default compilado:

| Campo | Rango aceptado |
|---|---|
| `bufferSize` | 8 a 32 (tope de RAM del Mega) |
| `threshold` | 1 a `bufferSize` |
| `cooldown` | 0 a 64 |
| `maxEvents` | 1 a 10 |

Los valores de CEM1 del ejemplo son los **placeholder de prueba** que hoy
estan en `DetectorConfigBuilder::buildCemConfig`: copiados de TEMP1, sin
calibrar contra campo real. Poder ajustarlos desde el archivo es justamente el
motivo por el que esta seccion existe: la calibracion se hace en laboratorio,
no recompilando.

---

## 6. Seccion `currentSensors` — canales de corriente

El gabinete admite **4 canales**; hoy se instancia 1 solo, con pines y
calibracion fijados en `SoftMega2560.ino`. `CurrentSensorsManager` ya soporta
hasta 10 (`MAX_CURRENT_SENSORS`).

**Maximo 4 entradas.** Es la seccion de mayor esfuerzo de las cinco: obliga a
pasar de instanciacion en compilacion a instanciacion en runtime, con el mismo
patron `clear/add` que ya usa `_applyTestMode()` para el magnetometro. Por eso
va **ultima** en el orden de implementacion.

```json
"currentSensors": [
  {
    "name": "SCT013-1",
    "enabled": true,
    "channel": 0,
    "ratedCurrent": 5.0,
    "ratedVoltage": 1.0,
    "calibration": 0.775,
    "sampleRate": 860,
    "integrationTimeMs": 200
  }
]
```

| Clave | Tipo | Notas |
|---|---|---|
| `name` | string | Maximo 15 caracteres. Es el identificador que viaja en `current_data` |
| `enabled` | bool | Con `false` el canal no se instancia |
| `channel` | int | 0 a 3. Entrada diferencial del ADS1115 |
| `ratedCurrent` | float | A |
| `ratedVoltage` | float | V |
| `calibration` | float | factor del transformador |
| `sampleRate` | int | SPS del ADS1115 |
| `integrationTimeMs` | int | ventana de integracion RMS |

**Advertencia de hardware.** `CurrentSensorSct013::update()` puede colgar
`loop()` de forma indefinida esperando un ACK de I2C que no llega si el
ADS1115 no esta cableado. Habilitar un canal por configuracion tiene el mismo
riesgo que hoy tiene descomentar la linea a mano: `enabled: true` sin el chip
presente congela el MCU. La configuracion no agrega proteccion contra eso, y
el default seguro es `false` para todo canal no verificado.

Los sensores de corriente **no son fuentes del Detector** y no lo seran: se
miden y se envian a la pantalla, nada mas. Las unicas fuentes son `CEM1` y
`TEMP1`.

---

## 7. Seccion `telemetry` — publicacion remota

Parametriza `MySystem::_publishMeasures()` / `_publishStatus()`
(`esp32/src/mysystem.hpp`) y el topic de `esp32/src/topics.hpp`. Solo ESP32.

```json
"telemetry": {
  "topic": "biosoft/telemetry",
  "intervals": { "measures": 30000, "status": 30000 },
  "fields": {
    "magneticField": { "enabled": true, "name": "CEM1" },
    "temperature":   { "enabled": true, "name": "TEMP1" },
    "current":       { "enabled": true, "name": "BOB1" },
    "health":        { "enabled": true, "name": "ESTADO" },
    "progress":      { "enabled": true, "name": "PROGRESS" },
    "elapsedTime":   { "enabled": true, "name": "ELAPSED_TIME" }
  }
}
```

El **set de campos es cerrado**: son los 6 que el firmware sabe producir, y no
se pueden agregar nuevos desde el archivo. Lo configurable es cuales se
publican, con que nombre de clave salen y a que cadencia.

`fields.*.name` es la clave JSON que va en el payload MQTT.
`magneticField`/`temperature`/`current` salen en la publicacion de mediciones
(`intervals.measures`); `health`/`progress`/`elapsedTime` en la de estado
(`intervals.status`).

> **Cambiar `topic` o cualquier `name` rompe el dashboard remoto.** El Decoder
> configurado del lado de Datacake espera exactamente esas 6 claves en ese
> topic. Renombrar un campo aca obliga a actualizar el Decoder en Datacake, o
> el valor deja de llegar, silenciosamente y sin error visible en el firmware.
> Los nombres del ejemplo son los que el Decoder tiene hoy. `BOB1` es el
> nombre historico del Decoder para la bobina, reusado para la corriente
> medida.

Topes: `topic` maximo 63 caracteres, cada `name` maximo 15. El payload
serializado sigue entrando en el buffer de 128 bytes de `_publishMeasures()`;
nombres mas largos que los actuales acercan ese limite y el firmware trunca
antes de publicar.

---

## 8. Reparto ESP32 / Mega2560

La SD esta fisicamente en la ESP32, que es la unica que lee el archivo. El
Mega no ve la tarjeta: recibe su porcion por el protocolo serie ya existente.

| Seccion | ESP32 | Mega2560 |
|---|---|---|
| `menus` | si | no |
| `intervals.esp32` | si | no |
| `intervals.mega` | reenvia | **si** |
| `detector.sources` | reenvia | **si** |
| `currentSensors` | reenvia | **si** |
| `telemetry` | si | no |

### 8.1 Fragmentacion

El protocolo serie limita cada trama a `MAX_JSON_SIZE = 256` bytes, con un
`StaticJsonDocument<256>` del lado de recepcion (`seriallink.hpp`, duplicado
en ambas placas). La configuracion completa del Mega **no entra en una sola
trama**, y ampliar ese limite duplicaria buffers dentro de los 8 KB de RAM del
Mega. Por eso se envia **fragmentada, un frame por unidad logica**:

| Frame | Comando | Contenido |
|---|---|---|
| 1 | `config_intervals` | las 9 claves de `intervals.mega` |
| 2 | `config_source` | una fuente completa de `detector.sources` (rangos + 3 reglas) |
| 3 | `config_source` | la otra fuente |
| 4..7 | `config_current` | un canal de `currentSensors` cada uno |

Cada frame se confirma con un `ack` propio y se reenvia hasta recibirlo, con
el mismo patron `Tasks::ReSendXxx` que ya usan `start`/`stop`/`reset`. Un
frame sin ack no bloquea a los demas: cada unidad logica que si llego queda
aplicada, y la que no, queda en su default compilado.

### 8.2 Momento del envio

El envio se dispara desde `CommandListener::onSerialConnected`, es decir al
establecerse el enlace y en cada reconexion. El Mega queda configurado antes
de que exista la posibilidad de iniciar un experimento, y una reconexion tras
un reset del Mega lo reconfigura sola.

La configuracion **no** viaja dentro del `start`: ese comando sigue llevando
unicamente los parametros de la corrida.

### 8.3 Comandos a declarar

`Commands::ConfigSource` no existe: el handler `Engine::_configureSource()`
esta comentado en `engine.hpp`, pero el nombre de comando nunca llego a
declararse en `commands.hpp`. Hay que agregar a **ambas copias** de
`commands.hpp` (ESP32 y Mega, que se mantienen identicas byte a byte):

```cpp
constexpr const char* ConfigIntervals = "config_intervals";
constexpr const char* ConfigSource    = "config_source";
constexpr const char* ConfigCurrent   = "config_current";
```

`_configureSource()` se descomenta y se completa con los clamps de la seccion
5. Su `_sendConfirmSourceConfig()` se reemplaza por el `ack` estandar, para no
tener dos mecanismos de confirmacion distintos conviviendo.

---

## 9. Orden de implementacion

De menor a mayor riesgo. Cada paso deja el sistema funcionando.

| # | Seccion | Alcance | Riesgo |
|---|---|---|---|
| 1 | `menus` | solo ESP32, sustitucion de arrays | bajo |
| 2 | `intervals` | `constexpr` a variables runtime, ambas placas | bajo, mecanico |
| 3 | `detector.sources` | revive `ConfigSource` + clamps en el Mega | medio: toca seguridad |
| 4 | `telemetry` | solo ESP32; coordinar con el Decoder de Datacake | medio: dependencia externa |
| 5 | `currentSensors` | compilacion a runtime, riesgo de cuelgue I2C | alto |

Los pasos 1 y 2 se pueden hacer sin tocar el protocolo serie. El paso 3 es el
que obliga a las modificaciones de la seccion 8.3.

---

## 10. Archivo de ejemplo

`docs/config.example.json` es un archivo completo y valido cuyos valores son
**identicos a los defaults compilados de hoy**. Cargarlo debe producir
exactamente el comportamiento actual del firmware, lo que lo hace util como:

- contrato de salida para la aplicacion externa;
- fixture para probar el parseo (`pio test -e native`);
- punto de partida para el operador que quiera cambiar un solo valor.

La unica diferencia respecto del firmware actual es
`detector.sources[CEM1].enabled`, que va en `false` para reflejar que hoy
`detector.addSource("CEM1")` esta comentado por el bring-up sin `A0` cableado.
