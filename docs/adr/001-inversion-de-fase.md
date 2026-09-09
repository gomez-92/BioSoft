# ADR-001 — Cómo se genera la inversión de fase para el campo nulo

- **Estado**: aceptada (2026-09-09)
- **Decide**: inversión analógica con op-amp + selector de modo por multiplexor
- **Reemplaza**: la decisión del 2026-09-09 (dos AD9833 con fase por software)
- **Tarjetas**: "Segundo generador senoidal + control de fase 180°"
  (https://trello.com/c/FMGHpOph), "CoilExcitationManager"
  (https://trello.com/c/fS2rDihC)

## Contexto

El experimento necesita dos modos de trabajo sobre 4 bobinas, agrupadas de a
pares (1-2 y 3-4):

- **Campo X**: los dos pares en fase — todas las bobinas aportan al mismo campo.
- **Campo nulo**: el par 3-4 desfasado 180° respecto del par 1-2, de modo que
  los campos se cancelen. Es la condición del **grupo control**: los animales
  están en el mismo aparato, con los mismos ruidos, vibraciones y temperatura,
  pero sin campo neto.

La validez del grupo control depende enteramente de que esa cancelación sea
real y se sostenga durante todo el experimento (hasta 2 horas). Si el campo
nulo no es nulo, el grupo control deja de ser control y el experimento entero
pierde sentido — sin que nada en el sistema lo advierta.

## Opciones consideradas

### A. Inversor analógico + 2 relés selectores (propuesta 2026-09-08)

Un AD9833, un op-amp inversor, y dos relés electromecánicos: uno como on/off
global antes de la bifurcación, otro como selector S+/S− en la rama 3-4.

Descartada el 2026-09-09 en favor de B. **El motivo del descarte no quedó
registrado** — solo el hecho. Reconstruyéndolo: la molestia estaba en los dos
relés electromecánicos, no en la inversión analógica en sí.

### B. Dos AD9833 con fase por software (decisión 2026-09-09, ahora revertida)

Un AD9833 por par de bobinas, con la fase del segundo fijada digitalmente en 0°
o 180° según el modo. Atractiva porque el modo se elige por software, sin
hardware de conmutación.

**Problema que la invalida**: cada módulo AD9833 comercial trae su propio
oscilador de 25 MHz. La frecuencia de salida es `fMCLK × FREQREG/2²⁸`, así que
dos relojes que difieran en unas ppm producen dos senoidales que difieren en
esas mismas ppm — y **la fase relativa rota de forma continua**. Con la
tolerancia típica de 50–100 ppm entre cristales, a 50 Hz la fase relativa
completa una vuelta cada **200 a 400 segundos**.

En la práctica: el campo nulo se cumpliría en el instante del arranque, estaría
en cuadratura al minuto y medio, y **sumándose en fase** a los tres o cuatro
minutos. El grupo control recibiría un batido lento en lugar de campo nulo.

El registro de fase del AD9833 fija la fase **dentro de un mismo chip** (entre
sus dos canales de fase), no entre chips distintos.

Esto tiene arreglo — compartir el MCLK entre ambos chips — pero al costo de:
desoldar el oscilador del segundo módulo, rutear 25 MHz entre placas, y
alinear los acumuladores de fase con un reset cuyas dos escrituras SPI deben
ser consecutivas: a 50 Hz, **1 ms de separación entre ellas son 18° de error**.

### C. Inversor analógico + multiplexor de un pin (aceptada)

Un AD9833. Su salida alimenta el par 1-2 directamente y, a través de un op-amp
inversor, produce la señal opuesta. Un multiplexor analógico (74HC4053, ADG419
o equivalente) elige, con **un solo GPIO**, si el par 3-4 recibe la señal
directa (campo X) o la invertida (campo nulo).

Es la opción A sin los dos relés: mismo principio analógico, conmutación de
estado sólido y un único punto de control.

## Decisión

**Opción C.**

El criterio decisivo es de qué depende la exactitud de la fase:

- En C, la inversión es **exacta por construcción**. La salida del inversor es
  −V(t) porque así está cableado. No hay nada que alinear al arrancar ni que
  pueda desalinearse después.
- En B, la exactitud es una propiedad que hay que **establecer y sostener**:
  depende de que el MCLK esté efectivamente compartido, de que el reset alinee
  los acumuladores, y de que nada introduzca skew entre las dos escrituras.

Y el modo de falla de B es de la peor clase posible: un error de fase φ deja un
campo residual proporcional a `2·sin(φ/2)` — 1° de error da 1,7% de campo
residual; 18° dan 31%. Nada de eso aparece en los logs ni lo detecta el
sistema. El experimento se contamina en silencio y el resultado se descubre
inválido después, si es que se descubre.

Factores secundarios, todos en la misma dirección:

| | B (2× AD9833) | C (inversor + mux) |
|---|---|---|
| Exactitud de fase | reset + sincronía sostenida | por construcción |
| Puede derivar | sí | no |
| Hardware extra | 2º módulo, desoldar oscilador, rutear 25 MHz, 2º CS | ½ op-amp dual + 2 R + mux |
| Firmware | módulo con secuencia temporalmente crítica | un `digitalWrite` |
| Depuración | falla lenta e intermitente (minutos) | inmediata en el osciloscopio |

Además:

1. **Los op-amps ya están en el diseño**: la etapa de acondicionamiento (buffer
   + centrado/ganancia) ya era necesaria por generador. El inversor es la otra
   mitad de un integrado dual que igual había que poner.
2. **No hay que modificar un módulo comercial** ni llevar 25 MHz por cable
   entre placas — la clase de cosa que anda en el banco y falla en el gabinete.
3. **El experimento solo necesita 0° y 180°.** La única ventaja real de dos DDS
   independientes, poder fijar fases arbitrarias, no se usa.

## Consecuencias

**A favor:**
- El campo nulo no puede derivar. Es la propiedad que más importa.
- Menos firmware: desaparece la orquestación de dos chips y su secuencia
  crítica.
- Un solo AD9833 que mantener, alimentar y depurar.

**En contra:**
- Vuelve a haber un componente de conmutación en el camino de señal (el mux),
  aunque de estado sólido y con un solo punto de control.
- Fases distintas de 0°/180° dejan de ser posibles sin rediseño. Aceptado: el
  experimento no las pide.
- Las dos ramas recorren caminos físicos distintos, lo que exige cuidado de
  simetría (ver más abajo).

## Requisitos de diseño que se derivan

1. **La inversión es respecto del offset, no de masa.** La salida del AD9833 es
   unipolar: excursión positiva sobre un offset DC. Invertirla contra masa daría
   una señal negativa, no la opuesta. El inversor debe referenciarse al mismo
   nivel de offset sobre el que se centra la señal. Es el error más común en
   este circuito.
2. **Bufferar las dos ramas por igual.** Si una pasa por el inversor y la otra
   va directa, quedan con etapas distintas. Usando el dual completo —una rama
   inversora, la otra seguidora de ganancia unitaria— ambas ven la misma carga y
   el mismo retardo, y la simetría mejora.
3. **El mux va a bajo nivel**, antes de la etapa de potencia: señales chicas y
   carga de alta impedancia, donde el `Ron` del multiplexor es irrelevante.
4. **Apareamiento de resistencias**: la exactitud de amplitud del inversor
   depende del par de resistencias. Al 1% el desbalance es del 2%, que a efectos
   de cancelación pesa mucho menos que un error de fase equivalente.

## Lección de proceso

La opción A se descartó dejando registrado **qué** se descartaba pero no **por
qué**. Cuando la opción B resultó tener un problema físico serio, no se pudo
evaluar si aquel motivo seguía aplicando: hubo que reconstruirlo. Este ADR
existe para que la próxima revisión de esta decisión arranque del razonamiento,
no solo del resultado.

Las decisiones de arquitectura de este proyecto deberían registrar el criterio
que las sostiene y las alternativas que se dejaron de lado, no únicamente la
opción ganadora.

## Abierto

- Elección concreta del op-amp y del multiplexor, contra el rango de señal real.
- Nivel de salida del AD9833 en el armado concreto — medir en banco.
- Valores de la red de offset y ganancia (ver `docs/coil-excitation.md`).
