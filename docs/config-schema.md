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
  "runType": "normal",
  "requireMega": true,
  "menus": { },
  "intervals": { "esp32": { }, "mega": { } },
  "control": { },
  "coils": [ ],
  "detector": { "enabled": true, "sources": [ ] },
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

**`fieldIntensity` y `frequency` admiten 3 opciones como maximo** (los demas
menus, 8). El mapa de calibracion del campo nulo (seccion 5) tiene un punto por
combinacion intensidad x frecuencia, y 3 x 3 = 9 es lo que se guarda en el
Mega. El ESP32 ignora (con log) las opciones a partir de la cuarta, y el
generador bloquea la descarga si se pasa.

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
    "measureMagneticField": 2000,
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
  "deadBand": 0.01,
  "balanceMax": 0.2,
  "map": []
}
```

| Clave | Unidad | Que hace |
|---|---|---|
| `enabled` | bool | `false`: el lazo mide y reporta pero NO toca el PWM |
| `kp` | duty/mT | ganancia proporcional: el paso de duty es `kp * error` |
| `maxStep` | duty | clamp del paso por llamada a `update()` |
| `deadBand` | mT | error por debajo del cual no se toca el output |
| `balanceMax` | adimensional, 0 a 0,5 | tope operativo del `balance` de cada punto del mapa (campo nulo) |
| `map` | lista, hasta 9 puntos | mapa de calibracion intensidad x frecuencia -> duty y balance (abajo) |

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

**`enabled` ahora si llega al Mega.** `ConfigLoader` lo leia desde el principio
pero `MySystem::_buildConfigControlDoc()` no lo incluia en el frame, de modo que
`control.enabled: false` en la tarjeta no tenia ningun efecto. Una tarjeta que lo
tenia escrito en `false` sin que nadie lo notara va a empezar a *no* excitar las
bobinas despues de actualizar el firmware.

### `map` — mapa campo, duty y balance

```json
"map": [
  { "intensity": 1.0, "frequency": 10, "duty": 0.30, "balance": 0.05 },
  { "intensity": 1.0, "frequency": 50, "duty": 0.31, "balance": 0.04 }
]
```

| Clave | Unidad | Que es |
|---|---|---|
| `intensity` | mT | intensidad de campo objetivo del punto (0 excluido, hasta 100) |
| `frequency` | Hz | frecuencia del punto (0 excluido, hasta 1000) |
| `duty` | 0 a 1 | duty **comun** que da esa intensidad, medido en banco |
| `balance` | +-0,5 | delta de balance entre grupos de fase, solo lo usa el campo nulo |

**Que hace con el mapa cada modo de exposicion** (el modo lo elige el operador
en la pantalla):

- **Campo X.** Si hay punto para la combinacion elegida, el experimento
  arranca con ese duty ya aplicado y el `FieldController` solo corrige a partir
  de ahi (antes partia de 0 y subia hasta el objetivo). Sin punto arranca de
  0, como siempre: el campo X nunca se rechaza por falta de mapa.
- **Campo nulo.** Hay que someter a los animales a todo el estres de la
  experiencia, pero sin campo efectivo. Se excita con el **mismo duty mapeado**,
  con las bobinas directas (1 y 3) a `duty x (1 + balance)` y las del mux
  invertido (2 y 4) a `duty x (1 - balance)`, **sin lazo**: el objetivo medido es
  0 y regular hacia arriba no tiene sentido. La banda del Detector se centra en
  0 y conserva la **tolerancia % del operador sobre la intensidad elegida**
  (±tol% x intensidad). **Sin punto, o con `|balance| > balanceMax`, el Mega
  rechaza el arranque entero** (rechazar, no recortar): no hay duty que aplicar
  y excitar "a ojo" daria un grupo control de mentira.

**Un arranque rechazado se muestra como un experimento terminado antes de
empezar.** El Mega ya acepto el `start` (ack), asi que informa el rechazo con un
`result_data` de `reason: "refused"` mas `cause` (`nomap`, `balance`); no pasa por `_finish()` porque nunca arranco nada. La ESP32 abre
la pantalla Resultado en el panel de Falla con el titulo **NO INICIO**, el
modo elegido, el motivo y el detalle redactados desde `cause`, y progreso 0 %.
Despues el flujo sigue normal: VOLVER lleva a Principal. REPETIR no aparece: con
`nomap` o `balance` el arranque fallaria igual hasta cambiar la configuracion
o el mapa. Un rechazo
**no se publica por MQTT**: sin un `targets` previo el monitor web armaria con
ese `result` una corrida huerfana de un experimento que nunca existio.

**El balance no es unico.** Depende de la intensidad *y* de la frecuencia (la
inductancia de las bobinas y la etapa de potencia no responden igual), por eso
se mide por punto. Con una sola constante, un grupo ganaria sobre el otro en
cuanto se cambiara la combinacion.

**El mapa se indexa por valor, no por posicion en el menu.** Los menus de
`fieldIntensity` y `frequency` son configurables, asi que las combinaciones
posibles cambian de una tarjeta a otra. El Mega busca por coincidencia exacta
(±0,001 mT, ±0,5 Hz), sin interpolar: un campo que no se midio no se estima.
Por el mismo motivo hay **un tope de 3 intensidades x 3 frecuencias** (seccion 3),
que acota el mapa a 9 puntos.

**Medicion del campo: valor eficaz (RMS), sin tara.** El MLX90393 mide el campo
total (bobinas mas ambiente, ~0,025 a 0,065 mT), y con 1 mT una banda de 5 %
es ±0,05 mT: el ambiente puede ocuparla entera, y en campo nulo es lo unico que
se lee. Una lectura instantanea ademas depende de en que punto de la senal cae
(la bobina excita una senal alterna de 1 a 100 Hz). Por eso CEM1 no entrega una
muestra sino el **valor eficaz de la fundamental**, calculado en el Mega
(`fieldsampler.hpp`): se toman grupos de 10 muestras con la hora real de cada
una, y por cada grupo se ajusta por minimos cuadrados
`a cos(wt) + b sen(wt) + c` en cada eje. Hasta unos 33 Hz las 10 muestras caen
en un periodo exacto (T/10); por encima, donde el bus del sensor no da para
tanto, se espacian cada ~0,38 o ~0,62 T (razon aurea): el grupo abarca varios
periodos (~5 a 100 Hz, unos 55 ms) y las fases quedan repartidas sin repetirse.
Con 3 muestras por periodo (lo que daria el bus en un solo periodo) el ajuste
no tendria ningun grado de libertad de sobra y el 2do armonico caeria justo
sobre la fundamental; con 10 muestras hay 7 grados de libertad y los
armonicos 2 a 7 se rechazan (en simulacion, un 20 % de armonico entra como
<2 % de error). El costo es un grupo de decenas de ms en vez de uno de ~10 ms
y un intervalo entre muestras con holgura para el jitter del `loop()`. El termino constante
`c` **es el campo ambiente**: queda fuera del resultado, asi que ya no hace
falta medirlo aparte ni descontarlo (se elimino la tara y sus tres umbrales,
`control.tare`). Rige igual en campo X y en campo nulo, y el mapa se calibra con
esta medicion.
La ventana de salida es de `measureMagneticField` (2 s de fabrica): el valor
que ven `FieldController`, el Detector y la pantalla se renueva a esa cadencia,
promediando decenas de grupos. Un grupo se descarta si una muestra tarda de mas
(bus ocupado, ranura perdida) o si el ajuste esta mal condicionado, y cinco
fallas seguidas del bus invalidan el sensor (el silencio de CEM1 corta el
experimento, ver tarjeta 25). El muestreo no bloquea: cada vuelta de `loop()`
hace a lo sumo una transaccion I2C, de modo que ni el boton de emergencia ni el
resto de las tareas esperan a la medicion; el bus ademas tiene un timeout de
25 ms.
Notas para banco, **sin verificar**: el piso de ruido del RMS no es cero (el
ruido de las muestras aporta una cota inferior positiva, relevante en campo
nulo); a 100 Hz el sensor da unas 160 muestras/s (6,2 ms entre disparos, ver
`[MLX90393] muestreo ...` en el log) y la atenuacion de su filtro interno hay
que medirla; la frecuencia real de la
senal es la del AD9833 (paso de 0,0931 Hz), no la nominal, y el firmware usa
esa. Con 50 o 60 Hz, el ruido de red entra en banda.

El generador avisa cuales combinaciones del menu quedan sin punto ("campo nulo
no disponible para X mT / Y Hz") y cuales puntos del mapa no coinciden con
ninguna combinacion. Los valores de fabrica son un mapa **vacio**: ningun duty
ni balance se inventa, salen de la calibracion de banco (ver
`docs/protocolo-calibracion-intensidad.md`).

**Limitaciones conocidas** (mejoras futuras, no de esta version):

- Una tarjeta **sin** mapa manda igual un unico `config_map` con `k = 0, n = 0`,
  que le dice al Mega que vacie el que tuviera de una conexion anterior.
- El MLX90393 entrega la magnitud sin signo, asi que el sistema no sabe de que
  lado se desvia el campo nulo.
- **Ajuste online del balance (opcion B).** Hoy el balance es fijo, el de la
  calibracion. La mejora seria un ajuste en caliente: variar el delta de a
  pasos chicos y quedarse con el sentido que baja |B|, que ahora dispone de
  una medicion RMS pero aun necesita saber de que lado se desvia. Se
  descarto en esta version porque, sin signo en la medicion, el ajuste
  perseguiria ruido y podria desbalancear los grupos justo durante el experimento.

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
  "enabled": true,
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
      "maxMissedSamples": 3,
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

### `detector.enabled` — interruptor general

Con `false`, el Detector descarta todas las muestras (`Detector::setEnabled`):
ninguna fuente acumula, levanta flags ni corta el experimento, aunque la
temperatura o el campo salgan de rango. Las fuentes no se borran ni se
reconfiguran; se siguen midiendo y mostrando en pantalla. Ausente, o no
booleano, queda **encendido**, que es el default restrictivo.

Viaja en su propio frame (`config_detector`, seccion 10.1) y, como las
fuentes, se aplica en el **proximo start**: apagar el detector a mitad de una
corrida porque el ESP32 se reconecto con otra tarjeta dejaria sin vigilancia
un experimento que arranco vigilado. Es solo para banco; el generador lo
avisa.

### `enabled` (por fuente)

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
| `CEM1` | `"mlx90393"` (sensor real por I2C, **default**), `"sim"` (`MagnetometerVoltageSim`, entrada `A0`) o `"scenario"` (lecturas sinteticas) |
| `TEMP1` | `"ds18b20"` (sensor real por OneWire, **default**), `"sim"` (`ThermometerVoltageSim`, entrada `A1`, 0-5 V = 0-50 C), `"scenario"` (lecturas sinteticas) o `"none"` (ningun termometro) |

El default de CEM1 era `sim` hasta la tarjeta 22. Cambio porque **con CEM1 en
`sim` o `scenario` las bobinas quedan bloqueadas** (ver `scenario` mas abajo), y
con ese default un Mega sin tarjeta SD no podria excitar nunca. Sin MLX90393
cableado, el default real simplemente no da lecturas.

Un valor que no vale **para esa fuente** se rechaza y queda el driver que
habia (`SensorChoices::parse` en `mega2560/src/sensorchoice.hpp`): los nombres
no son intercambiables, `"mlx90393"` no es un termometro.

- **`sim`** sirve para provocar cortes en banco con un potenciometro. En
  TEMP1, cada volt son 10 grados. No mide nada: nunca para un experimento con
  animales.
- **`none`** existe solo para TEMP1 y es lo que va sin DS18B20 cableado:
  registrado, `requestTemperatures()` bloquea ~750 ms en cada medicion aunque
  no haya nadie en el bus. Con TEMP1 `enabled` y sin termometro la fuente
  existe pero no recibe muestras, asi que **nunca corta por temperatura**; el
  Mega y el generador lo avisan.
- CEM1 **no acepta `none`**: es la realimentacion del `FieldController`, y sin
  magnetometro el lazo empujaria el duty a fondo buscando un campo que no
  puede medir. Para no vigilar el campo esta `enabled`; para no excitar las
  bobinas, `control.enabled`.
- **Bobinas bloqueadas**: con CEM1 en `sim` o `scenario`, el lazo regula
  contra un campo que no existe. El Mega calcula y reporta el duty (se ve en
  pantalla y en el monitor) pero **no habilita la etapa de potencia ni los
  canales** (`CoilChannels::setOutputsInhibited`). Un escenario nunca energiza
  nada.
- **`TEST` forzado**: con cualquier fuente en `sim` o `scenario` las corridas
  salen marcadas como prueba aunque `runType` diga `normal` (seccion 15):
  datos fabricados nunca entran al historico como experimento.

### `scenario` — lecturas sinteticas

Con `sensor: "scenario"` el Mega no lee el sensor: **calcula** la lectura y la
pasa por toda la logica real (Detector, reglas, flags, corte, `result_data`,
pantalla y monitor remoto). Sirve para validar el proceso entero sin sensores.
Se aplica en el proximo start, como el resto de la fuente.

```json
"scenario": {
  "base": 0, "noise": 0.1, "ramp": 0,
  "stepAt": 60, "step": 2.5,
  "oscAmp": 0, "oscPeriod": 0,
  "dropAt": 0,
  "setpointDuty": 0.5
}
```

La senal **no lleva valores fisicos**: se mide en *niveles* relativos a los
rangos de la corrida, los que el operador elige en pantalla (TEMP1) o los que
el Mega deriva del objetivo y la tolerancia (CEM1):

| Nivel | Valor |
|---|---|
| `0` | centro del rango normal |
| `+1` / `-1` | borde superior / inferior del normal |
| `+2` / `-2` | borde superior / inferior del critico |
| mas alla de 2 | adentro de la zona critica, con el mismo paso que de 1 a 2 |

Entre esos puntos, lineal. Asi un escenario significa lo mismo con cualquier
rango: "temperatura alta" (salto a +2,5) corta igual con 30~40/25~45 que con
26~44/20~50. Con valores fijos, cambiar un combo en pantalla dejaria el
escenario adentro del rango normal sin que nadie lo note. Si el critico
coincide con el normal (sin franja de advertencia), el paso de 1 a 2 se toma
igual a medio rango normal.

| Clave | Que es |
|---|---|
| `base` | nivel de partida |
| `noise` | amplitud de un ruido uniforme, en niveles (>= 0) |
| `ramp` | niveles por minuto |
| `stepAt`, `step` | a los `stepAt` segundos del start se suman `step` niveles (0 = sin salto) |
| `oscAmp`, `oscPeriod` | oscilacion senoidal; sin periodo (0) no oscila |
| `dropAt` | a partir de ese segundo el sensor deja de dar lecturas (0 = nunca) |
| `setpointDuty` | **solo CEM1**: el duty comun con el que las bobinas alcanzan el objetivo (0..1) |

`setpointDuty` es la *planta* de CEM1: el campo es lo que producen las bobinas
(`objetivo x duty / setpointDuty`) mas la perturbacion del nivel. El lazo
regula de verdad contra el escenario, y por eso puede **compensar** una
perturbacion lenta, como haria con bobinas reales. Con `setpointDuty: 0` no hay
planta y el campo es el nivel tal cual: es lo que se usa para forzar un campo
fuera de rango.

Los tiempos cuentan desde el start, e incluyen los 10 s de estabilizacion
(`settlingTime`) en los que el Detector no evalua: un salto antes de eso se ve
recien al terminar la ventana.

El generador trae perfiles listos (todo normal, temperatura alta, temperatura
que sube, campo inestable, campo fuera de rango, sensor de temperatura caido)
que completan los numeros de las dos fuentes. "Sensor caido" es la prueba de
banco del corte por silencio (`maxMissedSamples`, arriba): TEMP1 deja de medir
a los 90 s y la corrida corta a los ~105 s.

Por serie viaja en `config_scenario` (seccion 10.1) con claves cortas
(`b n r sa s oa op da d`) porque una fuente con las 9 apenas entra en 256
bytes.

### `maxMissedSamples` — corte por sensor sin lecturas

Cuantas lecturas seguidas puede perder una fuente **vigilada** antes de que su
silencio corte el experimento (entero, 1 a 20, default 3). Se traduce a tiempo
con la cadencia de medicion de esa fuente (`intervals.mega`), con un **piso de
5 s**: de fabrica TEMP1 corta a los 15 s (3 x 5 s) y CEM1 a los 5 s (3 x 500 ms
daria 1,5 s, pero el DS18B20 bloquea ~750 ms por medicion y un tick lento no
puede contar como sensor caido).

Antes de esto el Detector solo evaluaba las muestras que llegaban: un DS18B20
desconectado a mitad de corrida dejaba a los animales expuestos sin control de
temperatura, y la corrida terminaba `completed`. Cuenta como silencio todo lo
que no llega como lectura valida: un sensor que no contesta, el `-127` del
DS18B20 desconectado, un MLX90393 que falla por I2C.

- El reloj corre desde el start, pero el corte recien se habilita al terminar
  la estabilizacion (10 s), como el resto de las reglas. Una fuente que nunca
  dio una lectura corta en cuanto termina la estabilizacion o vence su limite,
  lo que ocurra despues.
- Corta con un `flag_data` de tipo **`silence`** y el mismo `result_data` que un
  limite critico: Resultado muestra "TEMP1 SIN LECTURAS" y el monitor web
  "sin lecturas". Para la salud y el color de las alertas cuenta como critico.
- No corta con `detector.enabled: false` ni para una fuente no vigilada,
  **salvo CEM1 con el lazo de control activo**: es la realimentacion del lazo,
  y sin ella las bobinas seguirian excitandose con el ultimo duty, a ciegas.
  Ese corte es seguridad del control, no deteccion, y actua aunque CEM1 no
  este vigilada y aunque el Detector este apagado. Consecuencia en banco: con
  el MLX90393 sin cablear y `control.enabled: true`, toda corrida corta a los
  ~15 s; se esquiva con `control.enabled: false` o con CEM1 en `sim`/`scenario`.
- TEMP1 vigilada con `sensor: "none"` cortaria en cada corrida, asi que el
  generador lo rechaza.

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

Parametriza los grupos de `esp32/src/topics.hpp` y las funciones
`MySystem::_publish*()` (`esp32/src/mysystem.hpp`). Solo ESP32.

La telemetria va repartida en **6 grupos**, cada uno con **su propio topic**.
No es una decision estetica: todo junto no entra en un mensaje (el payload
tiene un tope duro, ver mas abajo) y cada grupo tiene una cadencia natural
distinta — las mediciones cambian todo el tiempo, los objetivos una vez por
experimento. Separados, cada uno se habilita y se espacia sin tocar a los
demas, y el dashboard mapea cada topic a su propio set de campos sin tener que
adivinar la forma del JSON.

```json
"telemetry": {
  "groups": {
    "measures": { "enabled": true, "topic": "biosoft/telemetry/measures", "interval": 30000, "retain": true },
    "coils":    { "enabled": true, "topic": "biosoft/telemetry/coils",    "interval": 30000, "retain": true },
    "status":   { "enabled": true, "topic": "biosoft/telemetry/status",   "interval": 30000, "retain": true },
    "targets":  { "enabled": true, "topic": "biosoft/telemetry/targets",  "retain": true },
    "alerts":   { "enabled": true, "topic": "biosoft/telemetry/alerts",   "retain": false },
    "result":   { "enabled": true, "topic": "biosoft/telemetry/result",   "retain": true }
  },
  "fields": {
    "magneticField": { "enabled": true, "name": "CEM1" },
    "temperature":   { "enabled": true, "name": "TEMP1" },
    "health":        { "enabled": true, "name": "ESTADO" },
    "progress":      { "enabled": true, "name": "PROGRESS" },
    "elapsedTime":   { "enabled": true, "name": "ELAPSED_TIME" }
  }
}
```

### 9.1 Los grupos

| Grupo | Cuando publica | Contenido |
|---|---|---|
| `measures` | periodico | campo magnetico y temperatura |
| `coils` | periodico | `c1`..`c4` (corriente, A) y `d1`..`d4` (duty aplicado, %) |
| `status` | periodico | salud, avance, transcurrido, `REMAINING`, `STATE`, `MEGA` |
| `targets` | al arrancar el experimento | `MODE`, `CEM`, `FREQ`, `DUR`, `TOL`, `TNMIN`, `TNMAX`, `TCMIN`, `TCMAX`, `TEST`, `CFG`, `RLX` |
| `alerts` | con cada alerta | `SRC`, `TYPE`, `COUNT`, `LIMIT` |
| `result` | al terminar | `REASON`, `DESC`, `PROGRESS`, `ELAPSED`, `MEAN`, `TEST` y, si corto una fuente, `SRC`/`TYPE`/`COUNT`/`LIMIT` |

Los tres primeros son **periodicos** y llevan `interval` (entero positivo, ms).
Los otros tres son **por evento** y no lo llevan: ocurren una vez, y aceptar un
intervalo ahi sugeriria que se puede espaciar algo que no se repite. Si se
escribe igual, se ignora.

Para apagar un grupo esta `enabled`, no el intervalo. Un grupo deshabilitado
no registra su tarea en el `Timer`.

`retain` le pide al broker que **guarde el ultimo mensaje del topic** y se lo
entregue a cualquiera que se suscriba despues. Es lo que hace que el dashboard
muestre el estado actual apenas se abre, en vez de quedar vacio hasta la
proxima tanda -- y para `targets` y `result`, que se publican una sola vez por
experimento, es la diferencia entre verlos o no verlos nunca.

Va en `true` en todo lo que es **estado** y en `false` en `alerts`, que es un
**flujo de eventos**: una alerta retenida se le entregaria a cada nuevo
suscriptor como si acabara de ocurrir, mucho despues de que el experimento
termino. El generador avisa si se la habilita.

**`coils` publica solo las bobinas que existen** — las que reportaron al menos
una vez en un frame `coil_data`. Mandar siempre las cuatro significaria mandar
ceros de bobinas no montadas, indistinguibles de una bobina real que no esta
recibiendo nada.

**`targets` es el unico que dice si la corrida es del grupo tratado o del
grupo control** (`MODE`: `"x"` o `"null"`). Sin el, en el dashboard las dos se
ven igual. **`result` es el unico que informa el corte**: deshabilitarlo deja
al monitor remoto sin saber por que termino un experimento, ni siquiera cuando
corta por una alerta critica.

### 9.2 Los campos con clave configurable

`fields` cubre **solo** los cinco de la tabla: son los que ya formaban el
contrato con el consumidor antes de la division en grupos. Las demas claves
(`c1`..`d4`, `REASON`, `SRC`, `MODE`...) son **estructurales** — parte de la
forma del mensaje, no valores que el operador elija — y no se configuran una
por una.

Un campo con `enabled: false` no ocupa lugar en el payload. Si todos los
campos de un grupo quedan deshabilitados, ese grupo no publica (no se manda
`{}`).

> **Cambiar un `topic` o un `name` rompe el dashboard remoto.** El dashboard
> espera exactamente esos topics y esas claves; lo que se renombre aca deja de
> llegar, silenciosamente y sin error visible en el firmware.
> **Dos grupos no pueden compartir topic**: el dashboard recibiria dos formas de
> JSON distintas por la misma suscripcion sin poder distinguirlas. El
> generador lo rechaza.

### 9.3 Topes

`topic` maximo 63 caracteres y cada `name` maximo 15; lo que exceda se trunca
al copiarse.

El payload serializado tiene que entrar en `Topics::MaxPayloadLength`
(384 bytes), y el paquete MQTT completo — topic + cabecera + payload — en el
buffer de PubSubClient, que `BrokerManager::begin()` agranda a 512. El default
de la libreria es 256, que alcanzaba cuando la telemetria era un unico mensaje
de 128 bytes pero no para el grupo `result`, que lleva la descripcion del
corte.

La telemetria es **solo salida**: la placa publica y no se suscribe a nada.
`MySystem::onMessageReceived()` esta vacio a proposito y no hay comandos
remotos -- el monitor remoto observa, no controla.

Si un payload no entra, **el grupo no se publica** y queda en el log:
`serializeJson` recorta en silencio y un JSON cortado no lo puede parsear el
el dashboard, asi que publicarlo cambiaria un dato faltante por un mensaje
entero perdido sin aviso. Y si aun asi un mensaje llegara a la cola sin entrar en el
buffer de PubSubClient, se descarta en vez de reintentarse: un publish que
falla por tamano falla siempre, y dejarlo encolado trabaria todo lo que venga
atras.

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
| 2 | `config_control` | las claves escalares de `control` (kp, maxStep, deadBand, balanceMax, enabled) |
| 3..6 | `config_coil` | un canal de `coils` cada uno (nombre, enabled, factor) |
| 7..8 | `config_source` | la CABECERA de una fuente (nombre, enabled, sensor, bufferSize, maxMissedSamples, criticalMultiplier) |
| 9..14 | `config_rule` | una regla de una fuente (`source`, `rule`, threshold, cooldown, maxEvents) |
| 15..18 | `config_current` | un canal de `currentSensors` cada uno, identificado por (`address`, `channel`) |
| 19 | `config_detector` | `enabled` (el interruptor general del Detector), solo si el archivo lo trae |
| 20..21 | `config_scenario` | la senal sintetica de una fuente, con claves cortas, solo si el archivo la trae |
| 22..30 | `config_map` | un punto de `control.map`: `k` (indice), `n` (cuantos trae la tarjeta), `i` intensidad, `f` frecuencia, `d` duty, `b` balance |

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
constexpr const char* ConfigCurrent   = "config_current";
constexpr const char* ConfigDetector  = "config_detector";
constexpr const char* ConfigScenario  = "config_scenario";
constexpr const char* ConfigMap       = "config_map";
```

El `Engine::_configureSource()` que estaba comentado en `engine.hpp` **no se
revivio**: asumia una fuente entera por frame, que no entra (seccion 10.1).
Lo reemplazan los handlers de `ConfigSource` y `ConfigRule`, que guardan lo
recibido en una plantilla por fuente y lo aplican recien al arrancar el
experimento (ver 10.4).

### 10.4 Cuando se aplica cada seccion

`intervals.mega`, `control` (incluido `config_map`) y `coils` se aplican **al
recibirse**: pisan un valor y listo. El mapa solo se *consulta* en el `start`,
asi que no cambia nada de un experimento en curso.

`detector.sources` y `detector.enabled` NO: se guardan (una plantilla por
fuente, un booleano para el interruptor general) y se aplican al arrancar el
experimento. `enabled` implica registrar o desregistrar la fuente
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
| 5 | `telemetry` | solo ESP32; coordinar con el dashboard remoto | medio: dependencia externa | **hecho** |
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

## 13. Seccion `wifi` — redes a las que conectarse

Reemplaza las redes compiladas en `esp32/include/secrets.h`. Solo ESP32.

```json
"wifi": [
  { "ssid": "RED_1", "password": "CLAVE_1" },
  { "ssid": "RED_2", "password": "CLAVE_2" }
]
```

> **Este archivo lleva contrasenas en texto plano** en una tarjeta que entra y
> sale de la PC. Antes, las credenciales vivian solo dentro del binario.
> Es el precio de poder cambiar de red sin recompilar ni reflashear — util
> cuando el equipo se muda de laboratorio — pero hay que tenerlo presente al
> decidir donde queda la tarjeta.

Se prueban **en orden** hasta que una conecte. Maximo **5** redes
(`WiFiConfig::MAX_NETWORKS`); `ssid` hasta 32 caracteres y `password` hasta 63,
que es el tope de WPA2.

A diferencia del resto del esquema, esta seccion **reemplaza** en vez de
completar: si trae aunque sea una red valida, las compiladas no se usan. Si se
sumaran, una red vieja que quedo en `secrets.h` no se podria sacar nunca desde
la tarjeta. Una lista vacia, ausente o con todas las entradas invalidas se
trata como ausente y deja las compiladas en pie — quedarse sin ninguna red
seria peor que ignorar la seccion.

Una entrada sin `ssid` se descarta. Sin `password` se toma como red abierta,
que es legitimo; el generador avisa por las dudas.

---

## 14. Seccion `broker` — conexion MQTT

Reemplaza los `SECRET_MQTT_*` de `secrets.h`. Solo ESP32.

```json
"broker": {
  "server": "broker.example.com",
  "port": 8883,
  "user": "usuario_mqtt",
  "password": "password_mqtt",
  "clientId": "biosoft-esp32"
}
```

Al reves que `wifi`, esta seccion se aplica **clave por clave**: cada una que
venga pisa a la compilada y el resto se mantiene, asi se puede cambiar solo el
host sin repetir usuario y contrasena. Un `port` en 0 se ignora.

Topes: `server` 63 caracteres, `user` 31, `password` 63, `clientId` 31.

**El certificado del broker no esta aca.** Un PEM dentro de un JSON obliga a
escapar cada salto de linea, y es el unico dato de esta seccion que no es una
linea de texto; sigue compilado como `ROOT_CA_CERT` en `SoftEsp32.ino`. Cambiar
de broker desde la tarjeta sirve mientras el certificado siga valiendo.

El generador avisa si se pone el puerto **1883**: es MQTT sin cifrar, y con el
broker fuera de la red del equipo las credenciales y la telemetria viajarian en
texto plano.

---

## 15. `runType` y `requireMega` — corridas de prueba y banco

Declara si las corridas hechas con esta configuracion son **experimentos** o
**pruebas de banco**. Solo ESP32; el Mega no lo recibe porque no cambia nada
de lo que hace.

```json
"runType": "normal"
```

| Valor | Efecto |
|---|---|
| `"normal"` | Experimento. Es el default: sin la clave, sin tarjeta o con un valor que no se entiende. |
| `"test"` | Prueba. Franja "MODO PRUEBA" en todas las pantallas, cartel al arrancar que hay que cerrar con ENTENDIDO, y `TEST: true` en `targets` y `result`. |

Es una **declaracion, no un modo de funcionamiento**: no habilita ni
deshabilita nada. Cada relajacion (apagar el detector, una fuente, un sensor,
el lazo de control) se configura por separado en su propia seccion. No hay
una llave maestra que las active juntas, y por la misma razon una corrida
declarada `normal` *puede* tener relajaciones activas — la marca dice que
intencion tenia el operador, no que estaba encendido.

Lo que la marca protege es el historico del monitor remoto: una corrida de
banco y una con animales se ven, en todo lo demas, identicas. El monitor
guarda la marca en cada corrida (`runType`: `normal` / `test` / `unknown`) y
**por defecto no lista las de prueba**. `unknown` es una corrida que llego sin
`TEST` — firmware anterior a esta seccion o simulador viejo — y no se lee
como experimento: no se sabe que lo fuera.

La placa manda `TEST` **siempre, true o false**. La ausencia de la clave es
justamente lo que distingue un firmware viejo, asi que omitirla cuando vale
`false` haria que todo experimento nuevo entrara como `unknown`.

Un valor invalido (`"prueba"`, `"Test"`) cae en `normal` y queda en el log: es
el default restrictivo del resto del esquema. El generador de `tools/` no deja
descargar un valor fuera de los dos validos, asi que esto solo pasa editando
el archivo a mano.

### `requireMega` — exigir la placa de control

```json
"requireMega": true
```

Solo ESP32. Con `true` (default, y lo que rige si falta o no es booleano)
Iniciar y Repetir solo hacen algo con el Mega conectado: es quien energiza las
bobinas y quien corta por seguridad. Con `false` y **sin** Mega conectado, la
pantalla funciona sola para el banco: pasa del splash a Principal sin esperar
`state_data`, Iniciar va directo a En curso sin mandar `start`, y Detener
arma un resultado local. Con el Mega conectado no cambia nada.

Reemplaza la constante de compilacion `BENCH_SIN_MEGA` de `mysystem.hpp`, que
habia quedado en `true` en el firmware de produccion. Va junto a `runType`
porque los dos son decisiones de banco, pero son independientes: una corrida
sin Mega no tiene mediciones reales y deberia declararse `test`.

### Trazabilidad: `CFG` y `RLX` en `targets`

Cada corrida publica con que configuracion corrio (tarjeta 23):

- **`CFG`** — identificador de la configuracion: CRC32 del JSON leido **sin
  `wifi` ni `broker`** (cambiar de red no cambia el experimento), en 8 digitos
  hex. `"default"` sin tarjeta, sin archivo o con un archivo descartado entero
  (lo que corre en esos casos son los defaults compilados). Lo calcula **solo la
  placa** (`ConfigLoader::configId()`); el monitor lo guarda y no lo recalcula,
  asi no hay dos implementaciones del mismo hash que puedan divergir. No se
  escribe adentro del archivo a proposito: un archivo editado a mano
  conservaria un id que ya no describe su contenido.
- **`RLX`** — mascara de bits de las relajaciones activas. Cada bit es un
  hecho de la configuracion; una clave ausente cuenta con su default de
  fabrica.

| Bit | Valor | Relajacion | ¿Avisa en una corrida normal? |
|---|---|---|---|
| 0 | 1 | `detector.enabled: false` | si |
| 1 | 2 | TEMP1 sin vigilar | si |
| 2 | 4 | CEM1 sin vigilar | **no**: es el default de fabrica (reglas sin calibrar) |
| 3 | 8 | alguna fuente en `sim` o `scenario` | si |
| 4 | 16 | TEMP1 con `sensor: "none"` | si |
| 5 | 32 | `control.enabled: false` | si |
| 6 | 64 | `requireMega: false` | si |

El monitor guarda las dos en la corrida, muestra el id y la lista de
relajaciones en el detalle, y marca **CON RELAJACIONES** (en el historial, el
detalle y en vivo) a un experimento declarado `normal` con alguna relajacion
que avisa. En una corrida de prueba no se marca: ahi son lo esperado. El orden
de los bits esta en `ConfigLoader::Relaxations` (ESP32) y en
`web/client/src/lib/format.ts`; cambiar uno sin el otro traduce mal cada
corrida, sin error visible.

---

## 16. Archivo de ejemplo

`docs/config.example.json` es un archivo completo y valido cuyos valores son
**identicos a los defaults compilados de hoy**, con una excepcion: las
credenciales de `wifi` y `broker` son marcadores (`RED_1`, `CLAVE_1`,
`broker.example.com`). Ese archivo se versiona en el repo, asi que nunca lleva
credenciales reales — esas las escribe el operador en el generador y quedan
solo en la tarjeta. Cargarlo debe producir
exactamente el comportamiento actual del firmware, lo que lo hace util como:

- contrato de salida para la aplicacion externa;
- fixture para probar el parseo (`pio test -e native`);
- punto de partida para el operador que quiera cambiar un solo valor.

La unica diferencia respecto del firmware actual es
`detector.sources[CEM1].enabled`, que va en `false` para reflejar que hoy
`detector.addSource("CEM1")` esta comentado por el bring-up sin `A0` cableado.

---

## 17. Configuracion remota desde el monitor web

El monitor puede **leer** la configuracion vigente de la placa y **mandarle**
una nueva (tarjeta 24). Lo que llega se graba en la SD y **se aplica en el
proximo reinicio**. Codigo: `esp32/src/remoteconfig.hpp` (placa) y
`web/server/src/domain/configsync.ts` (monitor).

### Topics (fijos, no configurables desde la SD: son el canal por el que se cambia la SD)

| Topic | Sentido | Retain | Contenido |
|---|---|---|---|
| `biosoft/config/current/<i>` | placa -> monitor | si | bloque `i` de la configuracion vigente: `{id, i, n, d}` |
| `biosoft/config/current/meta` | placa -> monitor | si | `{id, n, len}`; `n = 0` con `id: "default"` (defaults compilados) |
| `biosoft/config/set` | monitor -> placa | no | bloque `i` de una configuracion nueva: `{r, i, n, d}` |
| `biosoft/config/status` | placa -> monitor | no | `{r, st, i?, msg?, id?}` |

### Transporte

La configuracion pesa ~7 KB y la placa tiene un solo buffer MQTT de 512 bytes,
asi que el texto del JSON viaja en **bloques** de tamano fijo, no por seccion
(`detector` o `menus` solas ya pasan de 512). El tope del bloque son 340
caracteres **ya escapados** dentro del string `d`. Placa y monitor parten igual
(`RemoteConfig::chunkLength` / `chunkText`).

- **Vigente**: la placa la publica al conectar con el broker (y en cada
  reconexion), de a un bloque por tick, **retenida**. Es el texto exacto que
  hasheo para el `configId` (seccion 15), asi que el monitor verifica que el
  CRC32 del texto reensamblado de el id antes de guardarlo: bloques retenidos
  de dos configuraciones mezclados no pasan.
- **Nueva**: el monitor manda de a **un bloque por vez** y espera la
  confirmacion (`st: "parcial", i`) antes del siguiente; sin confirmacion
  reintenta el mismo bloque 3 veces (5 s cada una) y despues da el pedido por
  no entregado. Nunca manda un bloque a ciegas. La placa escribe cada bloque a
  `/biosoft/config.new` en la SD, no en RAM.

### Lo que hace la placa al completar un pedido

1. Parsea `config.new` (con el mismo `schemaVersion` que `ConfigLoader`). Si no
   es valido: `invalida`, no se toca nada.
2. Calcula el id que va a tener (sin credenciales) y le agrega `wifi` y
   `broker` **tal como estaban en el archivo vigente**: las credenciales no
   viajan nunca por MQTT.
3. Rechaza un archivo que no entraria en el buffer de 8 KB de `ConfigLoader`.
4. Copia `config.json` a **`config.prev.json`** (respaldo) y graba el nuevo
   `config.json`, **minificado** (indentado podria pasarse de 8 KB con las
   credenciales).
5. Responde `aceptada` con el id nuevo.

Todo por streams de la SD: ningun buffer de 8 KB, que saldria del mismo heap
que necesita el handshake TLS (tarjetas 17 y 18).

### Estados de un pedido

| Estado | Significado |
|---|---|
| `enviando` / `parcial` | en curso (`parcial` = la placa confirmo hasta el bloque `i`) |
| `aceptada` | grabada en la SD; vale desde el proximo reinicio |
| `ocupada` | rechazada: hay un experimento en curso (o arrancando / deteniendose) |
| `invalida` | rechazada: no parsea, `schemaVersion` no soportada o no entra en 8 KB |
| `incompleta` | faltaron bloques o llegaron fuera de orden; o 30 s sin bloque nuevo |
| `error` | la placa no pudo escribir (sin tarjeta, SD llena...) |
| `no_entregada` | la placa no confirmo un bloque (estado que pone el monitor) |

### Salvaguardas, y lo que falta

- Nunca se aplica en caliente; se rechaza durante una corrida; se valida antes
  de tocar `config.json`; queda el respaldo; `wifi`/`broker` no viajan; no hay
  comandos ni reinicio remotos.
- **No hay confirmacion en la pantalla de la placa** ni boton "Reiniciar
  ahora": requieren SquareLine y quedaron como mejora (tarjeta 26).
  Consecuencia: lo aceptado se aplica en el proximo reinicio, **cualquiera sea
  su causa**, sin que nadie en la sala lo haya aprobado.
- **La regla de acceso del broker** (solo la credencial del monitor puede
  publicar en `biosoft/config/set`) se configura a mano en la consola de EMQX
  (ver `docs/despliegue-monitor-web.md`). Sin ella, cualquiera con una
  credencial del broker puede reescribir la tarjeta.

### El formulario es el generador

La seccion Configuracion del monitor **embebe `tools/generador-config.html`**
y se comunica por `postMessage`: le pasa la vigente, recibe el JSON ya
validado. Asi hay una sola fuente de reglas. Embebido, el generador no muestra
ni produce `wifi`/`broker`. El contenedor del monitor se construye solo con
`web/`, asi que lleva una **copia** (`web/client/public/generador-config.html`,
`npm run sync:generador`) y un test del front falla si difiere del original.
