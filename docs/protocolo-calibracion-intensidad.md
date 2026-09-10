# Protocolo de calibración del lazo de intensidad

Procedimiento de banco para calibrar el control de intensidad de campo
electromagnético: sintonizar el regulador proporcional (`FieldController`) y
obtener el factor de corrección de cada bobina.

- Tarjeta Trello: "8b — Protocolo de calibración del lazo de intensidad (banco)".
- Módulos involucrados: `FieldController`, `PwmDriver`, `CoilChannel` /
  `CoilChannels`, `MagnetometerMlx90393`, `CoilExcitation`.
- Requiere: bobinas reales, magnetómetro real y etapa de potencia completa.
  **No se puede ejecutar con el sensor simulado**: el `MagnetometerVoltageSim`
  lee una tensión en `A0` y la mapea linealmente a mT, sin ninguna relación
  física con el campo que producen las bobinas.

Todos los parámetros que se tocan acá viven en `/biosoft/config.json` de la
tarjeta SD: se cambian editando el archivo, **sin recompilar ni reflashear**.
La forma recomendada de editarlo es `tools/generador-config.html`, que valida
los rangos antes de dejar guardar (ver `docs/config-schema.md`).

---

## 0. Antes de nada: hoy el paso 1 no se puede ejecutar

> **Falta una pieza de firmware.** El paso 1 necesita fijar el duty en valores
> conocidos (0,20 / 0,40 / 0,60 / 0,80) con el lazo abierto. Hoy **no hay forma
> de hacerlo**: con `control.enabled: false`, `Engine::_start()` no llama a
> `_coilChannels.enableAll()` ni a `writeAll()` (`engine.hpp`), así que los
> canales quedan deshabilitados y el duty en 0. "Solo sensado" significa
> literalmente eso: mide y reporta, pero no excita nada.
>
> Lo que falta es una clave nueva en la sección `control` del esquema —
> `openLoopDuty`, de 0,0 a 1,0 — que, cuando `control.enabled` es `false` y
> `openLoopDuty` es mayor que 0, habilite los canales y escriba ese duty fijo
> sin cerrar el lazo. Es el mínimo que destraba el barrido, y encaja con el
> resto del esquema: llega al Mega en el frame `config_control`, que ya
> existe.
>
> Con esa clave, cada punto del barrido es una edición del archivo más un
> reset de la ESP32 (`ConfigLoader` lee en `setup()`, y la configuración se
> empuja al Mega recién al reconectar el enlace serie). Son 4 ciclos para el
> paso 1. Es incómodo, pero es la misma fricción que ya tiene INT-001 y no
> justifica inventar un canal de comandos aparte para el banco.

Los pasos 2, 3 y 4 sí son ejecutables con el firmware actual.

---

## 1. Qué se calibra, y por qué son tres cosas distintas

Se suelen confundir porque los tres salen del mismo banco.

**Ganancia de planta `K`** — cuántos mT de campo produce una unidad de duty. Es
una propiedad física del conjunto bobina + etapa de potencia + posición del
sensor. Se mide en **lazo abierto**, y es lo único de esta lista que se mide de
verdad.

**Ganancia del regulador `kp`** — la velocidad con que el lazo corrige. **No se
mide: se calcula** a partir de `K`.

> **`kp` no se puede inferir del duty de equilibrio.** En equilibrio el error es
> cero, así que `kp` no interviene: cualquier valor de `kp` lleva al mismo duty
> final. Lo que cambia es cuánto tarda en llegar y si oscila en el camino. El
> punto de equilibrio informa sobre la **planta**, no sobre el **controlador**.

**Factor por bobina `f_i`** — cuánto más o menos duty necesita cada bobina para
producir el mismo campo que las demás. Sale de comparar las bobinas entre sí, y
es lo que permite que las 4 den el mismo campo real con un único lazo.

## 2. Por qué un solo magnetómetro alcanza

El sistema tiene **un** sensor de campo, por diseño y no por omisión. Eso
determina qué forma puede tener el "control individual de intensidad":

- **No es posible** un lazo cerrado independiente por bobina: no hay
  realimentación por canal que cerrar.
- **Sí es posible**, y es lo que hace este protocolo: un único lazo cerrado que
  calcula un duty común, y cada canal lo escala por su factor `f_i` medido una
  vez en calibración (`duty_canal = duty_común × f_i`).

Los factores se miden **una vez por montaje** y se rehacen solo si cambia algo
físico (ver sección 9).

## 3. Prerrequisitos

1. **Magnetómetro real montado y fijo**, en su posición definitiva. Mover el
   sensor cambia `K` y todos los factores.
2. **Registrar la posición del sensor** respecto de cada bobina, con un croquis
   o una foto. Si hay que repetir la calibración, es lo primero que hay que
   poder reproducir.
3. **Etapa de potencia completa y alimentada**, con la senoidal ya
   acondicionada (ver `docs/coil-excitation.md`).
4. **La clave `control.openLoopDuty` implementada** (ver sección 0), si se va a
   hacer el paso 1.

`sampleTime` **no** es un prerrequisito: se deriva solo de
`intervals.mega.measureMagneticField` vía `makeFieldControllerConfig()`, así
que cambiar la cadencia de medición retonifica el controlador sin que haya que
tocar nada más. (Esto antes había que corregirlo a mano y era el prerrequisito
número uno de este protocolo; ya no.)

### Configuración de partida en `config.json`

```json
"control":  { "enabled": false, "kp": 0.1, "maxStep": 0.05, "deadBand": 0.01 },
"coils": [
  { "name": "BOB1", "enabled": true,  "calibrationFactor": 1.0 },
  { "name": "BOB2", "enabled": false, "calibrationFactor": 1.0 },
  { "name": "BOB3", "enabled": false, "calibrationFactor": 1.0 },
  { "name": "BOB4", "enabled": false, "calibrationFactor": 1.0 }
]
```

Y en `detector.sources`, para que el Detector no corte la corrida por flags
mientras se calibra:

```json
{ "name": "CEM1", "enabled": false, "sensor": "mlx90393", ... }
```

**Los factores arrancan todos en 1,0**, que significa "todavía sin medir" y no
"medido y sin desvío". Dejarlos en 1,0 durante los pasos 1 a 3 es lo correcto:
se están midiendo, no aplicando.

### Dónde se leen los números

En el monitor serie del Mega, con `DEBUG_ENABLED` activo:

- **Campo medido**: la traza de `MagnetometerMlx90393` imprime µT y mT en cada
  lectura. El sistema entero trabaja en **mT**; el valor en µT está para
  distinguir "el sensor no lee" de "el campo ambiente realmente es así de
  chico", porque cerca de ambiente los mT redondean a 0,0000.
- **Duty aplicado**: la línea de `Engine::onMagnetometerSample`, que imprime
  campo y duty juntos. Ojo con la etiqueta: dice `[TESTMODE][PWM]` por herencia
  del combo `testMode` que ya no existe. Es la línea correcta igual.
- **Saturación de un canal**: si `duty × f_i` supera 1,0, `CoilChannel` recorta
  **y lo registra**. Durante la calibración esa línea no es ruido: es el
  síntoma que se está buscando.

---

## 4. Paso 1 — Ganancia de planta `K` (lazo abierto)

Con **una sola bobina habilitada** y el lazo apagado, fijar el duty en valores
conocidos y registrar el campo medido.

| Duty | Campo medido (mT) | Campo − ambiente (mT) |
|---|---|---|
| 0,00 (ambiente) | | — |
| 0,20 | | |
| 0,40 | | |
| 0,60 | | |
| 0,80 | | |

Medir primero con duty 0 para tener el **campo ambiente**, y restarlo de los
demás puntos. Esperar a que la lectura se estabilice antes de anotar: al menos
5 muestras, o sea ~2,5 s a 500 ms por muestra.

**Resultado**: `K = ΔB / Δduty`, la pendiente de la recta, en mT por unidad de
duty.

**Qué mirar además del número:**
- **¿Es lineal?** Si los puntos no caen sobre una recta, la etapa satura o hay
  no-linealidad magnética. Un regulador proporcional asume linealidad; si no la
  hay, limitar el rango de trabajo a la zona que sí lo sea.
- **¿Pasa por el origen?** Extrapolar a duty 0 debería dar el campo ambiente y
  nada más. Un offset grande indica que la etapa entrega señal con duty cero.
- **¿A qué duty se alcanza la consigna de trabajo?** Si 2 mT piden duty 0,95,
  casi no queda margen de control y cualquier factor por bobina mayor que 1
  satura.

## 5. Paso 2 — `kp`, `maxStep` y `deadBand`

`kp · error` es un **paso de duty**, así que `kp` tiene unidades de duty/mT: es
el inverso de `K`.

```
kp = α / K       con α entre 0,1 y 0,3
```

`α = 1` corregiría todo el error en un solo paso — exactamente el rebote que ya
se corrigió una vez en `FieldController` (MOD-011, cuando el paso era fijo).
Empezar por `α = 0,15` y ajustar.

`maxStep` acota cuánto puede moverse el duty por llamada, como red de seguridad
frente a una lectura espuria. Con 500 ms por muestra, `maxStep = 0,05` significa
que ir de 0 a duty pleno lleva 10 s como mínimo. Debe ser **mayor** que el paso
típico `kp · error` en operación normal, o el clamp actúa siempre y el control
degenera otra vez en pasos fijos.

`deadBand` es el error por debajo del cual no se toca el duty. Sirve para que el
lazo no persiga el ruido del sensor una vez llegado al setpoint. Un punto de
partida razonable es el ruido pico a pico observado en el paso 1 con duty fijo:
si el campo medido oscila ±0,01 mT sin que nadie toque nada, un `deadBand`
menor que eso garantiza que el duty nunca se quede quieto.

**Verificación con escalón**: `control.enabled: true`, consigna desde reposo,
registrar duty y campo en cada muestra hasta que se estabilice.

| Muestra | Duty | Campo (mT) |
|---|---|---|
| 1 | | |
| 2 | | |
| ... | | |

Criterios de aceptación:
- **Converge** dentro de la ventana de settling (`intervals.mega.settlingTime`,
  10 s por default). No es un número arbitrario: mientras dura esa ventana las
  muestras no llegan al Detector, así que un lazo que todavía no convergió
  cuando la ventana termina puede disparar flags por su propio transitorio.
- **No sobrepasa** el setpoint más allá de la tolerancia configurada.
- **No oscila** de forma sostenida alrededor del setpoint.

Si sobrepasa u oscila, bajar `α`. Si tarda demasiado, subirlo. Si oscila con
amplitud chica y constante incluso con `α` bajo, subir `deadBand` antes de
seguir bajando `α`.

## 6. Paso 3 — Factor de corrección por bobina

**Solo válido si el paso 2 pasó.** Si el lazo oscila, "dónde estabiliza" no
significa nada y los factores salen mal.

Con el lazo cerrado y estable, **una bobina por vez** (el resto con
`enabled: false` en `coils`), misma consigna para todas.

| Bobina | Duty de equilibrio | Factor `f_i` |
|---|---|---|
| BOB1 | | |
| BOB2 | | |
| BOB3 | | |
| BOB4 | | |

Tomar como referencia la bobina de **menor** duty (la más eficiente) y calcular:

```
f_i = duty_i / duty_referencia
```

Ejemplo: si BOB1 estabiliza en 0,40 y BOB2 en 0,55, entonces `f_2 = 1,375` —
BOB2 necesita 37,5 % más duty para el mismo campo.

El duty de equilibrio se lee **con el lazo cerrado y ya estabilizado**, no en el
transitorio.

**Qué mirar:**
- Factores muy dispares (más de 1,5 entre extremos) sugieren un problema
  físico: bobina mal bobinada, conexión con más resistencia, o sensor muy
  asimétrico respecto del conjunto. Conviene resolverlo en el hardware antes de
  compensarlo por software.
- Si alguna bobina no llega a la consigna ni con duty 1,0, la etapa no tiene
  margen y hay que revisar el hardware antes de seguir.
- **El rango aceptado es 0,1 a 5,0.** Un factor fuera de eso lo rechaza el Mega
  y el canal queda en 1,0 — es decir, sin compensar, que es justamente lo que
  se quería evitar.

## 7. Paso 4 — Verificación conjunta

Cargar los 4 factores en `coils[].calibrationFactor`, habilitar las 4 bobinas y:

1. Consigna al valor de trabajo, modo **campo X**.
2. Verificar que el campo total converge y se mantiene dentro de tolerancia.
3. Verificar que **ningún canal satura**: no debe aparecer la traza de recorte
   de `CoilChannel`. Equivale a `duty_común × f_i ≤ 1,0` en los cuatro.
4. Registrar el duty final de cada canal.

**Prueba de campo nulo** (requiere la inversión de fase montada, ver
`docs/adr/001-inversion-de-fase.md`): repetir con modo **nulo** y las 4 bobinas
activas. El campo medido debe ser sensiblemente menor que en modo X.

No va a dar cero: hay campo ambiente, asimetrías de montaje y desbalance de
amplitud. Pero conviene tener presente cuánto pesa un error de fase, porque es
lo que se está verificando: el campo residual va como `2·sin(φ/2)`, así que 1°
de error deja 1,7 % de campo y 18° dejan 31 %. Una reducción pobre indica que
la cancelación no funciona, y hay que revisar la etapa analógica **antes de
usar el equipo con animales**: un grupo control mal cancelado es un grupo
tratado sin que nadie lo sepa.

> El modo se elige **antes** de arrancar. `CoilExcitation::setMode()` se rechaza
> mientras hay un experimento corriendo, a propósito: cambiar de campo a nulo a
> mitad de una corrida cambiaría la condición experimental de animales ya
> expuestos.

## 8. Qué queda registrado

Al terminar, estos valores tienen que quedar asentados **y cargados en
`/biosoft/config.json`**:

| Dato | Dónde va |
|---|---|
| `kp`, `maxStep`, `deadBand` | `control` |
| Los 4 factores `f_i` | `coils[].calibrationFactor` |
| `K` medido y si resultó lineal | bitácora (el firmware no lo usa) |
| El `α` usado y la bobina de referencia | bitácora |
| Posición del sensor y fecha | bitácora |

Sin la fecha y la posición del sensor la calibración no es reproducible: son lo
primero que hay que poder reconstruir si los números dejan de cerrar. `K` no lo
consume el firmware, pero es de donde sale `kp`: sin registrarlo, recalcular
`kp` obliga a repetir el paso 1 entero.

## 9. Cuándo repetir la calibración

- Si se cambia o se mueve una bobina.
- Si se mueve el magnetómetro.
- Si se modifica la etapa de potencia (amplificador, filtro, alimentación).
- Si se cambia la frecuencia de trabajo, en caso de que la respuesta de la
  etapa dependa de ella.

Un factor que vuelve a 1,0 en el archivo, por la razón que sea, no es un valor
neutro: es una bobina sin compensar.
