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

### 1.4 Que reemplazo este esquema

El combo de bring-up `testMode` (1-6) **ya no existe**: se retiro al
implementar la seccion 7. Sus tres ejes se repartieron asi:

| Eje de `testMode` | Donde vive ahora |
|---|---|
| sensor sim / real | `detector.sources[CEM1].sensor` (seccion 7) |
| CEM1 vigilada o no por el Detector | `detector.sources[CEM1].enabled` (seccion 7) |
| lazo de intensidad activo o solo sensado | `control.enabled` (seccion 5) |

---

## 2. Estructura general

```json
{
  "schemaVersion": 1,
  "menus": { },
  "intervals": { "esp32": { }, "mega": { } },
  "control": { },
  "coils": [ ],
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

**Los dos menus de temperatura estan relacionados.** El Detector del Mega da
por sentado que los rangos estan *anidados*
(`criticalMin <= normalMin < normalMax <= criticalMax`, ver `Source::addSample`
en `mega2560/src/detector.hpp`): "fuera de lo normal" tiene que ser un
superconjunto de "critico". Si no lo fuera, una lectura perfectamente normal
caeria fuera del rango critico y dispararia la alerta que corta el
experimento.

La pantalla Configuraciones garantiza eso **filtrando**: al elegir un rango de
un tipo, el combo del otro se rellena solo con los compatibles, y si lo que
estaba elegido dejo de serlo, queda sin seleccion (`--`) hasta que se elija
uno valido. Por eso **no** hace falta que todos los pares sean compatibles.

Lo que si valida el generador es que **ninguna opcion quede huerfana**: cada
rango normal necesita al menos un critico que lo contenga, y cada critico al
menos un normal contenido en el. Una opcion sin par compatible se puede
elegir y deja el otro combo vacio, sin forma de completar la configuracion.

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
    "updateProgress": 5000,
    "reSendConfig": 4000,
    "splashTimeout": 4000,
    "busyTimeout": 6000
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

`splashTimeout` y `busyTimeout` no son periodos de tarea sino **timeouts de
pantalla**: cuanto se queda el splash antes de pasar a Principal, y cuanto
espera la pantalla Procesando la confirmacion del Mega antes de rendirse y
volver. Ambos tienen un **minimo de 1000 ms** -- por debajo de eso el splash
no llega a verse y el Busy se rinde antes de que el Mega alcance a contestar.

`busyTimeout` esta acoplado a `intervals.mega.sendState`: la confirmacion que
la pantalla Procesando espera es el `state_data` que el Mega difunde con esa
cadencia. Si `busyTimeout` no la supera, Detener siempre termina por la rama
de timeout en vez de por la confirmacion real -- se ve igual (vuelve a
Principal), pero deja de ser una confirmacion. El generador avisa cuando la
combinacion cargada cae en ese caso.

Los intervalos de publicacion remota **no** viven aca: son parte de la seccion
`telemetry` (seccion 9), junto a los campos que publican.

`intervals.mega` es la primera de las porciones que viajan al Mega2560 (ver
seccion 10).

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

## 5. Seccion `control` — regulador de intensidad

Parametros de calibracion de `FieldController` (`mega2560/src/fieldcontroller.hpp`).
Hoy viven en `SoftMega2560.ino`, que es el punto donde este archivo los pisa.

```json
"control": {
  "enabled": true,
  "kp": 0.1,
  "maxStep": 0.05,
  "deadBand": 0.01
}
```

| Clave | Unidad | Que hace |
|---|---|---|
| `enabled` | bool | `false`: el lazo mide y reporta pero NO toca el PWM |
| `kp` | duty/mT | ganancia proporcional: el paso de duty es `kp * error` |
| `maxStep` | duty | clamp del paso por llamada a `update()` |
| `deadBand` | mT | error por debajo del cual no se toca el output |

`enabled: false` es el modo "solo sensado" que antes daban los `testMode`
1 y 4: sirve para verificar el sensado y la telemetria sin excitar las
bobinas. El default compilado es `true`, para que sin tarjeta SD el equipo
pueda correr un experimento completo (seccion 1.1).

**`kp` no es un numero que se elija a ojo.** Se calcula como `alfa / K`, donde
`K` es la ganancia de planta (mT por unidad de duty) medida en lazo abierto y
`alfa` va entre 0,1 y 0,3. Sale del procedimiento de banco documentado en
`docs/protocolo-calibracion-intensidad.md`. Los valores del ejemplo son los
defaults del firmware, **sin calibrar contra bobinas reales**.

`maxStep` tiene que ser mayor que el paso tipico `kp * error` en operacion, o
el clamp actua siempre y el control degenera en pasos fijos.

`sampleTime` **no** esta en esta seccion a proposito: se deriva del intervalo
real de medicion (`intervals.mega.measureMagneticField`) via
`makeFieldControllerConfig()`. Exponerlo como clave independiente permitiria
desincronizarlo justamente de lo que tiene que seguir.

Esta seccion viaja al Mega2560 (ver seccion 10).

---

## 6. Seccion `coils` — canales de bobina

Un elemento por bobina, **maximo 4**. Corresponde a `CoilChannel` /
`CoilChannels` (`mega2560/src/coilchannel.hpp`).

```json
"coils": [
  { "name": "BOB1", "enabled": true,  "calibrationFactor": 1.0 },
  { "name": "BOB2", "enabled": true,  "calibrationFactor": 1.0 },
  { "name": "BOB3", "enabled": false, "calibrationFactor": 1.0 },
  { "name": "BOB4", "enabled": false, "calibrationFactor": 1.0 }
]
```

| Clave | Tipo | Notas |
|---|---|---|
| `name` | string | Maximo 15 caracteres. Identifica el canal en los logs |
| `enabled` | bool | Con `false` el canal no se registra: sin PWM ni enable |
| `calibrationFactor` | float | Escala el duty comun del lazo para esta bobina |

### El factor no es una intensidad por bobina

Hay **un solo magnetometro** en el sistema, asi que no existe realimentacion
por bobina y no puede haber un lazo cerrado por canal. Lo que hay es un unico
`FieldController` que produce un duty **comun**, y cada canal lo escala:

```
duty_canal = duty_comun * calibrationFactor
```

El factor sale de medir cuanto duty necesita cada bobina para dar el mismo
campo, dividido por el de la bobina de referencia (ver el paso 3 del protocolo
de calibracion). **Un `1.0` significa "todavia sin medir", no "medido y sin
desvio".** Rango aceptado: `0.1` a `5.0`; un valor fuera de eso, o cero o
negativo, se rechaza y el canal queda en 1.0.

Si `duty_comun * calibrationFactor` supera 1,0, el canal satura: se limita y
**queda registrado en el log**, porque significa que esa bobina no alcanza la
consigna y va a entregar menos campo que las demas.

### Habilitacion y grupos de fase

Las bobinas 1 y 3 reciben la senoidal directa (grupo fijo); las 2 y 4 pasan
por el multiplexor y se invierten en modo campo nulo. **El agrupamiento es
cableado, no configurable desde este archivo.**

Como los grupos son alternados, habilitar solo `BOB1` y `BOB2` deja una bobina
de cada grupo y permite ejercitar los dos modos de experimento — que es el
default del firmware hoy. Habilitar un subconjunto que caiga entero en un solo
grupo hace que el modo nulo no cancele nada.

Esta seccion viaja al Mega2560 (ver seccion 10).

---

## 7. Seccion `detector.sources` — fuentes y reglas

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

Que driver alimenta la fuente. Reemplazo al combo temporal de `testMode`
por una eleccion explicita en configuracion (ver seccion 1.4).

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
estan en `DetectorConfigBuilder::defaultConfig()`: copiados de TEMP1, sin
calibrar contra campo real. Poder ajustarlos desde el archivo es justamente el
motivo por el que esta seccion existe: la calibracion se hace en laboratorio,
no recompilando.

---

## 8. Seccion `currentSensors` — canales de corriente

El gabinete admite **4 canales**, repartidos en **dos modulos ADS1115**.

### Por que dos modulos

Un ADS1115 tiene 4 entradas simples pero solo **2 pares diferenciales**
(AIN0-AIN1 y AIN2-AIN3), y el SCT013 se lee en modo diferencial. Con 4
canales hacen falta dos chips, en direcciones distintas: **0x48** (ADDR a GND)
y **0x49** (ADDR a VDD).

Por eso cada entrada se identifica por el par **(`address`, `channel`)**, que
es su identidad fisica. El firmware declara los 4 slots (2 por modulo) con un
objeto estatico cada uno, ligado de por vida a su ADS; el archivo decide
cuales se habilitan y con que parametros. **El segundo modulo esta declarado
aunque hoy no exista fisicamente**: agregarlo mas adelante es editar este
archivo, no reflashear.

> Una version anterior de este documento decia `channel: 0 a 3`. Era
> incorrecto: un canal 2 o 3 caia en el `default` de
> `CurrentSensorSct013::readRaw()`, que devuelve 0 — un sensor que mide 0 A
> para siempre y parece funcionar. Hoy se rechaza en `begin()`.

```json
"currentSensors": [
  {
    "name": "SCT013-1",
    "enabled": true,
    "address": 72,
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
| `address` | int | **Obligatoria.** 72 (0x48) o 73 (0x49) |
| `channel` | int | **Obligatoria.** 0 (AIN0-AIN1) o 1 (AIN2-AIN3). No hay 2 ni 3 |
| `name` | string | Maximo 15 caracteres. El Mega resuelve por el la bobina que mide: el `SCT013-N` le corresponde a la bobina N en `coil_data` |
| `enabled` | bool | Con `false` el canal no se registra: no se mide ni se publica |
| `ratedCurrent` | float | A |
| `ratedVoltage` | float | V |
| `calibration` | float | factor del transformador |
| `sampleRate` | int | SPS del ADS1115 — ver la advertencia de abajo |
| `integrationTimeMs` | int | ventana de integracion RMS |

`address` y `channel` son las unicas obligatorias: sin ellas no se sabe a que
entrada fisica corresponde la configuracion, y no hay default razonable que
inventar. Una entrada sin ellas se descarta.

**`sampleRate` es por MODULO, no por canal.** El data rate (y la ganancia,
que ni siquiera se expone acá) son registros del ADS1115, compartidos por sus
dos canales: si los dos sensores de un mismo modulo piden valores distintos,
gana el ultimo que se configura, en silencio. Mismo tipo de trampa que la
frecuencia del PWM, que es por timer y no por pin.

### Proteccion contra el cuelgue por I2C

`readADC_Differential_*()` espera un ACK que nunca llega si el ADS1115 no
esta, y **no tiene timeout**: cuelga `loop()` indefinidamente (confirmado en
banco). Con la configuracion viniendo de un archivo, eso pasa de ser un riesgo
que se evitaba no descomentando una linea a algo que cualquiera puede
provocar.

Por eso `CurrentSensorSct013::begin()` **prueba la direccion antes de
hablarle**: una transaccion de direccion sola
(`Wire.beginTransmission`/`endTransmission`) devuelve error limpio si nadie
contesta. Si el modulo no esta, el sensor queda deshabilitado, se registra en
el log y **el sistema sigue corriendo**; `update()` tampoco toca el bus
mientras eso no se haya confirmado.

Consecuencia practica: se puede dejar el 0x49 configurado sin tenerlo montado.
Aun asi, el default seguro para un canal no verificado sigue siendo `false`.

Los sensores de corriente **no son fuentes del Detector** y no lo seran: se
miden y se envian a la pantalla, nada mas. Las unicas fuentes son `CEM1` y
`TEMP1`.

---

## 9. Seccion `telemetry` — publicacion remota

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

Topes: `topic` maximo 63 caracteres, cada `name` maximo 15. Lo que exceda se
trunca al copiarse.

El payload serializado tiene que entrar en el buffer de 128 bytes de
`MySystem::_publishTelemetry()`. Con los nombres default entra holgado, pero
un par de nombres largos alcanzan para pasarse. Si eso pasa **la tanda no se
publica** y queda registrado en el log: `serializeJson` recorta en silencio, y
un JSON cortado no lo puede parsear el Decoder de Datacake, asi que publicarlo
solo cambiaria un dato faltante por una tanda entera perdida sin aviso.

Un campo con `enabled: false` no ocupa lugar en el payload. Si los 3 campos de
una tanda quedan deshabilitados, esa tanda no se publica (no se manda `{}`).

---

## 10. Reparto ESP32 / Mega2560

La SD esta fisicamente en la ESP32, que es la unica que lee el archivo. El
Mega no ve la tarjeta: recibe su porcion por el protocolo serie ya existente.

| Seccion | ESP32 | Mega2560 |
|---|---|---|
| `menus` | si | no |
| `intervals.esp32` | si | no |
| `intervals.mega` | reenvia | **si** |
| `control` | reenvia | **si** |
| `coils` | reenvia | **si** |
| `detector.sources` | reenvia | **si** |
| `currentSensors` | reenvia | **si** |
| `telemetry` | si | no |

### 10.1 Fragmentacion

El protocolo serie serializa cada trama a un `char[MAX_JSON_SIZE]` de 256
bytes en `SerialLink::_sendFrame()`, contando el envelope
`{"command":...,"params":{...}}`. **Ese es el unico tope real**: los tamaños
`StaticJsonDocument<128>`/`<256>` que aparecen en `seriallink.hpp` no limitan
nada, porque en ArduinoJson 7 esa clase es un `JsonDocument` elastico cuyo
`capacity()` devuelve N sin aplicarlo.

Pasarse de 256 **no da error**: `serializeJson` trunca, el CRC se calcula
sobre el texto ya recortado, y el frame llega al otro lado con CRC valido y
menos claves. El receptor no puede distinguirlo de un frame que nunca las
trajo. Por eso la configuracion se envia **fragmentada** y
`test_serialframes` (Mega, entorno native) fija el presupuesto de cada frame:

| Frame | Comando | Contenido |
|---|---|---|
| 1 | `config_intervals` | las 9 claves de `intervals.mega` |
| 2 | `config_control` | las 4 claves de `control` |
| 3..6 | `config_coil` | un canal de `coils` cada uno (nombre, enabled, factor) |
| 7..8 | `config_source` | la CABECERA de una fuente (nombre, enabled, sensor, bufferSize, criticalMultiplier) |
| 9..14 | `config_rule` | una regla de una fuente (`source`, `rule`, threshold, cooldown, maxEvents) |
| 15..18 | `config_current` | un canal de `currentSensors` cada uno, identificado por (`address`, `channel`) |

Una fuente entera en un solo frame ronda los 290 caracteres con el envelope
y **no entra**: de ahi la division cabecera + 3 reglas. Hay un test que lo
fija, para que nadie los vuelva a juntar sin darse cuenta.

Cada frame se confirma con un `ack` propio y se reenvia hasta recibirlo, con
el mismo patron `Tasks::ReSendXxx` que ya usan `start`/`stop`/`reset`. Un
frame sin ack no bloquea a los demas: cada unidad logica que si llego queda
aplicada, y la que no, queda en su default compilado.

### 10.2 Momento del envio

El envio se dispara desde `CommandListener::onSerialConnected`, es decir al
establecerse el enlace y en cada reconexion. El Mega queda configurado antes
de que exista la posibilidad de iniciar un experimento, y una reconexion tras
un reset del Mega lo reconfigura sola.

La configuracion **no** viaja dentro del `start`: ese comando sigue llevando
unicamente los parametros de la corrida.

### 10.3 Comandos

Declarados en **ambas copias** de `commands.hpp` (ESP32 y Mega, que se
mantienen identicas byte a byte):

```cpp
constexpr const char* ConfigIntervals = "config_intervals";
constexpr const char* ConfigControl   = "config_control";
constexpr const char* ConfigCoil      = "config_coil";
constexpr const char* ConfigSource    = "config_source";
constexpr const char* ConfigRule      = "config_rule";
// Falta, junto con la seccion 8:
// constexpr const char* ConfigCurrent = "config_current";
```

El `Engine::_configureSource()` que estaba comentado en `engine.hpp` **no se
revivio**: asumia una fuente entera por frame, que no entra (seccion 10.1).
Lo reemplazan los handlers de `ConfigSource` y `ConfigRule`, que guardan lo
recibido en una plantilla por fuente y lo aplican recien al arrancar el
experimento (ver 10.4).

### 10.4 Cuando se aplica cada seccion

`intervals.mega`, `control` y `coils` se aplican **al recibirse**: pisan un
valor y listo.

`detector.sources` NO: se guarda en una plantilla por fuente y se aplica al
arrancar el experimento. `enabled` implica registrar o desregistrar la fuente
en el Detector, y eso destruye y recrea el `Source`, que vuelve sin
configurar (`Source::addSample()` es un no-op mientras `_configured` sea
false). Aplicarlo al vuelo por una reconexion serie a mitad de experimento
dejaria al Detector sin vigilar esa fuente por el resto de la corrida, en
silencio y justo cuando mas importa.

---

## 11. Orden de implementacion

De menor a mayor riesgo. Cada paso deja el sistema funcionando.

| # | Seccion | Alcance | Riesgo | Estado |
|---|---|---|---|---|
| 1 | `menus` | solo ESP32, sustitucion de arrays | bajo | **hecho** |
| 2 | `intervals` | `constexpr` a variables runtime, ambas placas | bajo, mecanico | **hecho** |
| 3 | `control` + `coils` | parametros del regulador y factores por canal | medio: toca el lazo | **hecho** |
| 4 | `detector.sources` | validacion en el Mega + retiro de `testMode` | medio: toca seguridad | **hecho** |
| 5 | `telemetry` | solo ESP32; coordinar con el Decoder de Datacake | medio: dependencia externa | **hecho** |
| 6 | `currentSensors` | 2 modulos ADS1115, probe de I2C antes de registrar | alto | **hecho** |

Los pasos 1 y 2 se pudieron hacer sin tocar el protocolo serie. El paso 3 fue
el que obligo a las modificaciones de la seccion 10.3.

---

## 12. `/biosoft/seleccion.json` — la eleccion del operador

Es un archivo **distinto de `config.json` y con otro dueno**. `config.json` lo
escribe el generador y define **cuales** son las opciones de cada menu;
`seleccion.json` lo escribe el firmware (`esp32/src/selectionstore.hpp`) cuando
el operador toca GUARDAR en la pantalla Configuraciones, y dice **cual** de
esas opciones esta elegida. Estan separados justamente para que guardar desde
la pantalla no pise el archivo del generador.

```json
{
  "schemaVersion": 1,
  "fieldMode": 0,
  "fieldIntensity": 0,
  "frequency": 0,
  "duration": 0,
  "tolFieldIntensity": 0,
  "rangeNormalTemperature": 0,
  "rangeCriticalTemperature": 0
}
```

Cada valor es el **indice** de la opcion dentro del menu correspondiente de la
seccion 3, empezando en 0. El generador no lo produce ni lo lee; no hace falta
crearlo a mano.

Tres reglas, las mismas que gobiernan `config.json`:

- **Sin tarjeta SD todo funciona igual.** No hay seleccion que restaurar, queda
  el indice 0 de cada menu (el default compilado) y GUARDAR sigue valiendo para
  la sesion en curso: simplemente no sobrevive al reinicio. Ni el arranque ni el
  guardado lo tratan como un error.
- **Se descarta entero** si el JSON es invalido o si la `schemaVersion` no es la
  soportada, nunca a medias.
- **La primera vez no existe, y eso es lo normal.** Al arrancar, un archivo
  ausente no es un error: queda la seleccion por defecto. Al guardar, el
  firmware crea `/biosoft` si hace falta (`SdStorage::ensureDir`), porque en una
  tarjeta que nunca tuvo `config.json` el directorio tampoco existe y
  `SD.open(..., FILE_WRITE)` no crea directorios intermedios.
- **Cada indice se valida contra el menu de hoy.** Si `config.json` cambio entre
  un arranque y el siguiente, un menu pudo haberse achicado y el indice guardado
  apuntaria a una opcion que ya no existe; fuera de rango se ignora esa clave y
  queda el default compilado.

`SelectionStore::load()` corre en `MySystem::begin()`, o sea **despues** de
`ConfigLoader::load()`: cuando se validan los indices los menus ya son los
definitivos.

---

## 13. Archivo de ejemplo

`docs/config.example.json` es un archivo completo y valido cuyos valores son
**identicos a los defaults compilados de hoy**. Cargarlo debe producir
exactamente el comportamiento actual del firmware, lo que lo hace util como:

- contrato de salida para la aplicacion externa;
- fixture para probar el parseo (`pio test -e native`);
- punto de partida para el operador que quiera cambiar un solo valor.

La unica diferencia respecto del firmware actual es
`detector.sources[CEM1].enabled`, que va en `false` para reflejar que hoy
`detector.addSource("CEM1")` esta comentado por el bring-up sin `A0` cableado.
