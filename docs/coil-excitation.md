# Excitación de bobinas — contrato de salida

Qué señales entrega este sistema hacia la etapa de potencia que excita las
bobinas, con qué características, y qué asume de lo que hay del otro lado.

- Tarjeta Trello: "Salida senoidal para excitación de bobinas (reemplazo de
  On/Off)" (https://trello.com/c/GPQ34KRC).
- Origen: devolución de los profesores (2026-09-07) — la salida a las bobinas
  debe dejar de ser un On/Off y pasar a excitarlas con la señal senoidal real
  mientras el sistema está en "On".

## 1. Alcance y frontera

Este documento describe **lo que entregamos**: las señales que salen de la
placa de control. La etapa de potencia (amplificación, acoplamiento,
alimentación) y las bobinas en sí **están fuera de este alcance**:

- Las bobinas están caracterizadas en un trabajo previo, cuyos datos no están
  incorporados acá.
- La validación se hará contra un banco de pruebas con un túnel de
  magnetoterapia todavía no relevado.

Por eso el documento define una **frontera**, no un diseño eléctrico completo:
de este lado, señales con características conocidas y acotadas; del otro,
requisitos que quien arme la etapa de potencia tiene que satisfacer. No se
dimensionan acá amplificador, filtro ni carga: hacerlo sin los datos de la
bobina real sería inventar números.

## 2. La cadena de señal

```
                        ┌─► directa ────────────────► par 1-2
AD9833 ──► senoidal ────┤
  (forma y frecuencia)  └─► inversor ──┐
                                       ├─ mux ──────► par 3-4
                        (directa) ─────┘   ▲          (en fase o a 180°)
                                           │
                              selector de modo (1 GPIO)

PWM ──► filtro RC ──► nivel DC (amplitud) ─────► etapa de potencia ──► bobina
                                                    (fuera de alcance)

enable (por bobina) ───────────────────────────► habilita/corta ese canal
interruptor general ───────────────────────────► corta toda la etapa
```

La inversión de fase para el campo nulo es **analógica**, no por software: un
op-amp inversor produce la señal opuesta y un multiplexor elige cuál recibe el
par 3-4. Se descartó usar un segundo AD9833 con fase programable porque dos
generadores con osciladores independientes derivan y el campo nulo dejaría de
serlo a los pocos minutos — el razonamiento completo está en
`docs/adr/001-inversion-de-fase.md`.

La decisión de fondo: **la senoidal y la intensidad viajan por caminos
separados y se combinan en la etapa de potencia**. El AD9833 aporta la forma
de onda y la frecuencia; el PWM, una vez filtrado a un nivel DC, aporta la
amplitud. La bobina recibe entonces una senoidal continua cuya amplitud es la
variable que el lazo de control mueve.

Esto es lo que reemplaza al On/Off: el relé deja de ser el que decide "hay
campo o no hay campo" modulando la energía, y pasa a ser un corte de seguridad
(ver §6). La energía que llega a la bobina la gobierna la amplitud de una
senoidal continua, no la presencia o ausencia de un nivel binario.

**Lo que este esquema descarta explícitamente** es usar el PWM para trocear la
senoidal (conmutarla a 3,9 kHz). Eso haría llegar ráfagas a la bobina en vez
de una senoidal continua — exactamente la clase de señal que la devolución de
los profesores pide dejar atrás.

## 3. Señales que entrega el sistema

### 3.1 Senoidal (AD9833)

| Propiedad | Valor |
|---|---|
| Origen | AD9833 por SPI, `SignalGenerator` (`signalgenerator.hpp`) |
| Forma | senoidal (el chip también puede triangular y cuadrada; no se usan) |
| Frecuencia | la del experimento, elegida por el operador (hoy 10 o 50 Hz) |
| Nivel | salida de bajo nivel del AD9833, con offset DC — **a medir en banco** |
| Cantidad | **1 solo** para todo el sistema (ver ADR-001) |

El nivel de salida del AD9833 es de bajo nivel y viene con offset DC: **no es
apto para atacar una bobina directamente**, y requiere una etapa de
acondicionamiento (centrado + ganancia) del lado de la potencia. El valor
exacto depende del Rset del circuito y debe medirse en banco antes de
dimensionar esa etapa; no se documenta acá un número de datasheet como si
fuera un dato verificado del armado real.

### 3.2 PWM de intensidad

| Propiedad | Valor |
|---|---|
| Origen | `PwmDriver` (`pwmdriver.hpp`), timer de hardware del Mega2560 |
| Nivel lógico | 0 / 5 V |
| Frecuencia | **3906 Hz** (nominal 3921, ver §4) |
| Resolución | 8 bits (256 pasos de duty) |
| Duty | 0 a 100 %, desde `FieldController` vía lazo cerrado |
| Cantidad | 1 por bobina (4 en el diseño final; 1 hoy) |

El consumidor de esta señal es un filtro RC que la convierte en un nivel DC
proporcional al duty. Ese nivel es el que fija la amplitud de la senoidal.

### 3.3 Enable por bobina

Una línea digital por bobina, nivel lógico, que habilita o corta ese canal de
forma individual. Es una entrada digital a una etapa de estado sólido, no un
contacto mecánico: no lleva blanking ni debounce. Su implementación en
firmware corresponde a `CoilChannel` (tarjeta separada, todavía no existe en
código).

### 3.4 Interruptor general

Un único relé, remoto, en conector de 3 pines (GND, VDD, enable), que corta
toda la etapa de potencia. Es independiente del enable por bobina: aquél
selecciona qué bobinas participan, éste corta todo. Ver la tarjeta "Colapsar
RelayManager → interruptor general único".

## 4. Por qué 3906 Hz

La frecuencia del PWM deja de ser cosmética en cuanto la señal se filtra: es
lo que determina el compromiso entre el rizado que queda sobre la amplitud y
la velocidad con que el nivel DC sigue al lazo de control.

Los timers de 16 bits del Mega2560 corren en **PWM phase correct de 8 bits**
(así los deja el core de Arduino), donde `f = 16 MHz / (2 · N · 256)`. Con los
5 prescalers disponibles, las únicas frecuencias alcanzables son:

| Prescaler | Frecuencia |
|---|---|
| 1 | 31250 Hz |
| 8 | **3906 Hz** |
| 64 | 488 Hz (el default de Arduino) |
| 256 | 122 Hz |
| 1024 | 30,5 Hz |

Se elige el prescaler 8 (**3906 Hz**): es la opción más alta que no exige
cambiar el modo del timer, y deja casi dos décadas de margen sobre la senoidal
de trabajo (10–50 Hz), suficiente para que el RC filtre la portadora sin
volverse lento frente a las correcciones del lazo.

`PwmDriver` acepta la frecuencia pedida y elige el prescaler más cercano, así
que configurarlo con 3921 Hz (o cualquier valor cercano) resuelve al mismo
prescaler 8. **No hay frecuencias intermedias**: pedir 2000 Hz no da 2000 Hz,
da la opción de la tabla más próxima.

### La frecuencia es por timer, no por pin

Cada timer gobierna 3 pines y tiene **un solo** prescaler. Dos canales PWM
sobre el mismo timer comparten frecuencia obligatoriamente, y el último
`enable()` define la de todos, sin aviso. Para 4 canales a la misma frecuencia
esto es indistinto; si en el futuro un canal necesitara otra, tiene que vivir
en otro timer.

## 5. Pines

El Mega2560 tiene PWM por hardware en **15 pines** (2–13 y 44–46), no en 3.
`PwmDriver` cubre los cuatro timers de 16 bits, que comparten la misma tabla
de prescalers:

| Timer | Pines | Estado |
|---|---|---|
| Timer1 | 11, 12 | disponible |
| Timer3 | 2, 3, 5 | **ocupados**: OneWire (2), relé (3), SPI CS (5) |
| Timer4 | 6, 7, 8 | disponible |
| Timer5 | 44, 45, 46 | 44 en uso (PWM actual), 45 y 46 libres |

Quedan deliberadamente fuera:
- **Timer0** (pines 4, 13): es el que cuenta `millis()`/`micros()`/`delay()`.
  Cambiarle el prescaler descalibra todas las tareas periódicas del `Timer` del
  proyecto.
- **Timer2** (pines 9, 10): tiene 7 prescalers en vez de 5 (agrega ÷32 y ÷128),
  así que la tabla daría bits que significan otro divisor.
- **Pines analógicos A0–A15**: no tienen timer asociado; `analogWrite()` ahí
  degrada a un `digitalWrite` binario. Es la razón por la que el PWM se mudó de
  A0 al pin 44.

**Asignación sugerida para las 4 bobinas**: 44, 45, 46 (Timer5) y 6 (Timer4).
Los dos timers se configuran a la misma frecuencia, así que el reparto entre
ellos es transparente. No hace falta ni DAC externo ni multiplexado.

## 6. Qué pasa con el relé

Deja de ser el modulador de energía y queda como **corte de seguridad**: un
interruptor general que habilita o corta toda la etapa. Sigue teniendo sentido
como relé mecánico real (la lógica de blanking de la clase `Relay` se
justifica ahí), pero ya no participa del control de intensidad.

## 7. Estado en firmware

**Ya existe:**
- Generación de la senoidal: `SignalGenerator` sobre AD9833, con
  `setSineWave(frecuencia)` llamado al arrancar el experimento.
- PWM de intensidad con frecuencia configurable: `PwmDriver`, ahora sobre
  cualquiera de los 4 timers de 16 bits.
- Lazo cerrado que mueve el duty: `FieldController` a partir de la lectura del
  magnetómetro.
- Corte general: un relé.

**Falta (tarjetas propias, no este documento):**
- `CoilExcitationManager` — el generador y el selector de modo de experimento
  (campo X / campo nulo), que es un unico GPIO hacia el multiplexor.
- `CoilChannel` — PWM individual + enable por bobina, ×4.
- Colapsar `RelayManager` a un interruptor general único.
- Control de intensidad individual por bobina y calibración del regulador
  proporcional.

**Corregido junto con este documento:** `PwmDriver::applyFrequency()` calculaba
la frecuencia como `fClk / (N · top)`, sin el factor 2 del modo phase correct.
El error hacía elegir el prescaler equivocado: pidiendo los 490 Hz del default
seleccionaba el divisor 256 (~122 Hz reales) en lugar del 64 que el timer ya
traía de fábrica, degradando en silencio una configuración correcta. El único
síntoma visible habría sido un zumbido más grave y, con el filtro RC en su
lugar, un rizado bastante mayor sobre la amplitud.

## 8. Requisitos para la etapa de potencia

Lo que quien arme la etapa tiene que resolver, y que este sistema **no**
provee:

1. **Acondicionar la senoidal del AD9833**: centrado y ganancia hasta el rango
   que necesite la bobina, con buffer capaz de manejar las cargas.
2. **Generar la rama invertida y seleccionarla**: op-amp inversor referenciado
   al offset (no a masa) mas un multiplexor analogico gobernado por un GPIO,
   con ambas ramas bufferadas por igual para que queden simetricas. Ver ADR-001.
3. **Filtrar el PWM**: red RC que convierta 3906 Hz / 8 bits en un nivel DC
   estable, con rizado tolerable y respuesta suficientemente rápida para el
   lazo de control (cadencia de corrección: una muestra de magnetómetro cada
   500 ms).
4. **Combinar ambas**: la etapa cuya ganancia o amplitud dependa de ese nivel
   DC (VCA, multiplicador o equivalente).
5. **Respetar el enable por bobina y el interruptor general** como cortes
   efectivos de la salida.
6. **Potencia y alimentación** acordes a la bobina real.

## 9. Abierto

- Nivel de salida real del AD9833 en el armado concreto — medir en banco.
- Topología del acondicionamiento (offset, ganancia) y elección concreta del
  op-amp y del multiplexor — pendiente desde las sesiones del 2026-09-07/09.
- Valores del filtro RC, que dependen de la etapa elegida.
- Datos de la bobina y del túnel de magnetoterapia del banco de pruebas.
