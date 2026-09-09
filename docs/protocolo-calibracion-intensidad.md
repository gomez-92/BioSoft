# Protocolo de calibración del lazo de intensidad

Procedimiento de banco para calibrar el control de intensidad de campo
electromagnético: sintonizar el regulador proporcional (`FieldController`) y
obtener el factor de corrección de cada bobina.

- Tarjeta Trello: "Calibración del regulador proporcional + factor por bobina".
- Módulos involucrados: `FieldController`, `PwmDriver`, `MagnetometerMlx90393`,
  `CoilChannel` (cuando exista).
- Requiere: bobinas reales, magnetómetro real y osciloscopio o al menos el
  monitor serie. **No se puede ejecutar con el sensor simulado.**

## 1. Qué se calibra, y por qué son dos cosas distintas

Hay dos parámetros que suelen confundirse porque ambos salen del mismo banco:

**Ganancia de planta `K`** — cuántos mT de campo produce una unidad de duty.
Es una propiedad física del conjunto bobina + etapa de potencia + posición del
sensor. Se mide en **lazo abierto**.

**Factor por bobina `f_i`** — cuánto más o menos duty necesita cada bobina para
producir el mismo campo que las demás. Sale de comparar las bobinas entre sí, y
es lo que permite que las 4 den el mismo campo real con un único lazo de
control.

De ahí se deriva:

**Ganancia del regulador `kp`** — la velocidad con que el lazo corrige. **No se
mide**: se calcula a partir de `K`.

> **`kp` no se puede inferir del duty de equilibrio.** En equilibrio el error es
> cero, así que `kp` no interviene: cualquier valor de `kp` lleva al mismo duty
> final. Lo que cambia es cuánto tarda en llegar y si oscila en el camino. El
> punto de equilibrio informa sobre la **planta**, no sobre el **controlador**.

## 2. Por qué un solo magnetómetro alcanza

El sistema tiene **un** sensor de campo, y no está previsto agregar más. Eso
determina qué forma puede tener el "control individual de intensidad":

- **No es posible** un lazo cerrado independiente por bobina: no hay
  realimentación por canal que cerrar.
- **Sí es posible**, y es lo que hace este protocolo: un único lazo cerrado que
  calcula un duty común, y cada canal lo escala por su factor `f_i` medido una
  vez en calibración. Las 4 bobinas quedan igualadas entre sí.

Los factores se miden **una vez por montaje** y se reconfiguran solo si se
cambia una bobina, se la mueve, o se toca la etapa de potencia.

## 3. Prerrequisitos

Antes de empezar a medir:

1. **Corregir `sampleTime`.** `FieldController::Config::sampleTime` vale `0.2`
   (200 ms) pero `update()` se llama cada 500 ms
   (`Intervals::MeasureMagneticField`). Afecta al término integral. Calibrar
   contra un valor que después se corrige invalida la calibración.
2. **Magnetómetro real montado y fijo.** El MLX90393 debe quedar en su posición
   definitiva: mover el sensor cambia `K` y todos los factores.
3. **Registrar la posición del sensor** respecto de cada bobina. Si hay que
   repetir la calibración, esto es lo primero que hay que reproducir.
4. **Etapa de potencia completa y alimentada**, con la senoidal ya
   acondicionada (ver `docs/coil-excitation.md`).
5. **Un modo de fijar el duty a mano**, sin lazo — hoy se consigue con
   `control.enabled: false` en el archivo de la SD, que deja el sistema
   midiendo sin actuar sobre el PWM (ver `docs/config-schema.md`, seccion 5).

## 4. Paso 1 — Ganancia de planta `K` (lazo abierto)

Con **una** bobina habilitada y el lazo de control apagado, fijar el duty a
valores conocidos y registrar el campo medido.

| Duty | Campo medido (mT) |
|---|---|
| 0,20 | |
| 0,40 | |
| 0,60 | |
| 0,80 | |

Esperar a que la lectura se estabilice en cada punto antes de anotar (al menos
5 muestras, o sea ~2,5 s a 500 ms por muestra).

**Resultado**: `K = ΔB / Δduty`, la pendiente de la recta. En mT por unidad de
duty.

**Qué mirar además del número:**
- **¿Es lineal?** Si los 4 puntos no caen sobre una recta, la etapa satura o
  hay no-linealidad magnética. Un regulador proporcional simple asume
  linealidad; si no la hay, hay que limitar el rango de trabajo a la zona que
  sí lo sea.
- **¿Pasa por el origen?** Extrapolar a duty 0 debería dar ~0 mT (más el campo
  ambiente). Un offset grande indica que la etapa entrega señal con duty cero.
- **¿A qué duty se alcanza el máximo de la consigna?** Si 2 mT requieren duty
  0,95, casi no queda margen de control.

## 5. Paso 2 — `kp` y `maxStep`

`kp · error` es un **paso de duty**, así que `kp` tiene unidades de duty/mT: es
el inverso de `K`.

```
kp = α / K       con α entre 0,1 y 0,3
```

`α = 1` corregiría todo el error en un solo paso — exactamente el
comportamiento de rebote que ya se corrigió una vez en `FieldController` (ver
MOD-011). Empezar por `α = 0,15` y ajustar.

`maxStep` acota cuánto puede moverse el duty por llamada, como red de
seguridad frente a una lectura espuria. Con 500 ms por muestra, `maxStep = 0,05`
significa que ir de 0 a duty pleno lleva 10 s como mínimo. Debe ser **mayor**
que el paso típico `kp · error` en operación normal, o el clamp actúa siempre y
el control vuelve a ser de paso fijo.

**Verificación con escalón**: aplicar una consigna desde reposo y registrar el
duty en cada muestra hasta que se estabilice.

| Muestra | Duty | Campo (mT) |
|---|---|---|
| 1 | | |
| ... | | |

Criterios:
- **Converge** dentro de la ventana de settling (10 s, `Intervals::SettlingTime`).
- **No sobrepasa** el setpoint más allá de la tolerancia configurada.
- **No oscila** de forma sostenida alrededor del setpoint.

Si sobrepasa u oscila, bajar `α`. Si tarda demasiado, subirlo.

## 6. Paso 3 — Factor de corrección por bobina

Con el lazo ya estable, **una bobina por vez**, misma consigna para todas.

| Bobina | Duty de equilibrio | Factor `f_i` |
|---|---|---|
| 1 | | |
| 2 | | |
| 3 | | |
| 4 | | |

Tomar como referencia la bobina de menor duty (la más eficiente) y calcular:

```
f_i = duty_i / duty_referencia
```

Ejemplo: si la bobina 1 estabiliza en 0,40 y la 2 en 0,55, entonces
`f_2 = 1,375` — la bobina 2 necesita 37,5% más duty para el mismo campo.

**Importante**: el duty de equilibrio de cada bobina se lee **con el lazo
cerrado y estabilizado**, no en el transitorio. Si el lazo todavía oscila
(paso 2 no superado), "dónde estabiliza" no significa nada y los factores
salen mal.

**Qué mirar:**
- Factores muy dispares (>1,5 entre extremos) sugieren un problema físico:
  bobina mal bobinada, conexión con más resistencia, o sensor muy asimétrico
  respecto del conjunto.
- Si alguna bobina no llega a la consigna ni con duty 1,0, la etapa no tiene
  margen suficiente y hay que revisar el hardware antes de seguir.

## 7. Paso 4 — Verificación conjunta

Con los factores cargados y las 4 bobinas habilitadas:

1. Consigna al valor de trabajo, modo campo X.
2. Verificar que el campo total converge y se mantiene dentro de tolerancia.
3. Verificar que ningún canal satura (duty · `f_i` ≤ 1,0 en todos).
4. Registrar el duty final de cada canal.

**Prueba de campo nulo** (cuando la inversión de fase esté montada, ver
`docs/adr/001-inversion-de-fase.md`): con modo nulo y las 4 bobinas activas, el
campo medido debe ser sensiblemente menor que en modo X. No va a dar cero —hay
campo ambiente, asimetrías de montaje y desbalance de amplitud— pero una
reducción pobre indica que la cancelación no está funcionando y hay que revisar
la etapa analógica antes de usar el equipo con animales.

## 8. Qué queda registrado

Al terminar, estos valores tienen que quedar asentados (y, cuando el archivo de
configuración en SD esté implementado, cargados ahí — ver
`docs/config-schema.md`):

- `K` medido, y si la respuesta resultó lineal.
- `kp`, `maxStep` y el `α` usado.
- Los 4 factores `f_i` y cuál fue la bobina de referencia.
- Posición del sensor y fecha de la calibración.

Sin la fecha y la posición del sensor, la calibración no es reproducible: son
lo primero que hay que poder reconstruir si los números dejan de cerrar.

## 9. Cuándo repetir la calibración

- Si se cambia o se mueve una bobina.
- Si se mueve el magnetómetro.
- Si se modifica la etapa de potencia (amplificador, filtro, alimentación).
- Si se cambia la frecuencia de trabajo, en caso de que la respuesta de la
  etapa dependa de ella.
