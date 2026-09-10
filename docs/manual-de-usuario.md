# Manual de usuario — BioSoft

Guía para quien opera el equipo: cómo configurar, correr y supervisar una
exposición, qué significa cada cosa que muestra la pantalla, y qué hacer cuando
algo no sale como se espera.

No hace falta saber programar para usar el equipo. Sí hace falta entender qué
está midiendo y por qué corta cuando corta, porque de eso depende que un
experimento sea válido.

> **Estado del equipo.** BioSoft está en desarrollo. La sección 12 lista, sin
> vueltas, lo que todavía no está calibrado o no está disponible desde la
> pantalla. Leerla antes de usar el equipo con animales no es opcional.

---

## 1. Qué hace el equipo

BioSoft expone una muestra de animales de laboratorio a un **campo
electromagnético de frecuencia extremadamente baja (ELF)**, controlado y
sostenido durante un tiempo definido, mientras vigila que las condiciones se
mantengan dentro de lo que el experimento admite.

Hace tres cosas, y conviene tenerlas separadas en la cabeza:

1. **Genera** el campo: una señal senoidal de la frecuencia elegida, con la
   intensidad pedida, sobre cuatro bobinas.
2. **Mide** en vivo: intensidad de campo, temperatura y corriente.
3. **Corta** cuando corresponde: porque se cumplió el tiempo, porque detectó
   una condición peligrosa, o porque el operador lo pidió.

El punto 3 es el que justifica el equipo. Un generador de campo lo arma
cualquiera; lo que hace falta en un experimento con animales es que **algo
decida detener la exposición sin que haya una persona mirando**.

### Los dos grupos

El diseño experimental necesita un grupo tratado y un grupo control. El equipo
lo resuelve con dos **modos de experimento**:

- **Campo X** — las cuatro bobinas suman. Es la condición de exposición.
- **Campo nulo** — dos bobinas reciben la señal invertida 180°, así que el campo
  se cancela. Los animales del grupo control quedan sometidos a exactamente el
  mismo ruido, vibración y temperatura del equipo funcionando, pero **sin
  campo**.

El campo nulo es lo que hace comparable al grupo control. Un control "apagando
el equipo" no serviría: mediría el efecto de estar en una caja distinta, no el
del campo.

---

## 2. Partes del equipo

| Parte | Qué hace |
|---|---|
| **Pantalla táctil** (ESP32) | Todo lo que ve y toca el operador. Además publica la telemetría remota. |
| **Placa de control** (Mega2560) | Excita las bobinas, lee los sensores y decide cuándo cortar. |
| **Bobinas** (hasta 4) | Generan el campo. |
| **Sensores** | Campo magnético (1), temperatura (1) y corriente (hasta 4). |
| **Pulsador de emergencia** | Corte físico inmediato. |
| **Tarjeta SD** | Configuración del equipo (ver sección 11). |

Las dos placas se hablan por un cable serie. **La que decide sobre la seguridad
es la placa de control, no la pantalla**: si la pantalla se cuelga o se
desconecta, la placa de control sigue vigilando y sigue pudiendo cortar. La
pantalla refleja y retransmite; no decide.

---

## 3. Antes de encender

1. Bobinas conectadas y montadas en su posición.
2. Sensor de campo en su posición **registrada** — si se movió desde la última
   calibración, los valores de intensidad no son confiables.
3. Sensor de temperatura en contacto con lo que se quiere vigilar.
4. Tarjeta SD colocada, si se van a usar parámetros propios.
5. Muestra ubicada.
6. **Modo de experimento correcto** para la tanda que se va a correr (ver
   sección 12: hoy esto se define en la configuración del equipo, no en la
   pantalla).

---

## 4. Encendido y pantallas

Al energizar aparece la **pantalla de inicio** unos segundos mientras el equipo
arranca y establece contacto con la placa de control. Después pasa sola a la
pantalla **Principal**.

El equipo tiene seis pantallas:

| Pantalla | Cuándo aparece |
|---|---|
| **Inicio** | Al encender, y si se pierde el contacto con la placa de control. |
| **Principal** | Estado de reposo. Muestra la configuración cargada. |
| **Configuración** | Al tocar *Configuración*. |
| **Espera** | Mientras el equipo arranca o detiene un experimento. |
| **En curso** | Durante la exposición. |
| **Resultado** | Al terminar un experimento, por el motivo que sea. |

No se navega libremente entre ellas: la pantalla que se ve **refleja el estado
real del equipo**. Si el equipo está corriendo, la pantalla es "En curso" y no
hay forma de irse a otra sin detener el experimento.

### Pantalla Principal

Muestra los seis parámetros con los que va a correr el próximo experimento, y
dos botones: **Iniciar** y **Configuración**.

Vale la pena mirarla antes de cada tanda: es el resumen de lo que está a punto
de pasar.

---

## 5. Configurar un experimento

En **Configuración** hay seis listas desplegables. Al terminar, *Guardar* aplica
los cambios y vuelve a Principal; *Volver* sale sin aplicar nada.

| Parámetro | Qué define |
|---|---|
| **Intensidad de campo** | La consigna, en mT. El equipo regula solo para sostenerla. |
| **Frecuencia** | La frecuencia de la señal, en Hz. |
| **Duración** | Cuánto dura la exposición. Al cumplirse, corta solo. |
| **Tolerancia de campo** | Cuánto puede desviarse la intensidad real de la consigna antes de considerarse anormal. |
| **Rango de temperatura normal** | La franja esperada. Salirse genera alerta. |
| **Rango de temperatura crítica** | La franja límite. Salirse puede cortar el experimento. |

### Cómo se usan los dos rangos de temperatura

No son lo mismo y la diferencia importa:

- Fuera del rango **normal** pero dentro del **crítico** → el equipo lo anota
  como alerta y sigue. Es "esto no está como esperábamos".
- Fuera del rango **crítico** → es "esto es peligroso". Si se repite lo
  suficiente, corta.

Que una lectura crítica no corte al instante es deliberado: una sola lectura
rara puede ser ruido eléctrico, y abortar por eso arruinaría experimentos
válidos. El equipo exige que la condición **persista** antes de cortar.

### La tolerancia de campo

Además de definir cuándo la intensidad se considera anormal, de acá sale el
límite crítico de campo, aplicando un margen fijo sobre la tolerancia elegida.
Una tolerancia de 5 % da una franja normal de ±5 % y una crítica algo más
amplia.

---

## 6. Iniciar un experimento

Desde Principal, botón **Iniciar**.

La pantalla pasa a **Espera** ("Iniciando…") mientras la placa de control recibe
los parámetros, arranca el generador, habilita las bobinas y cierra el corte
general. Cuando confirma que está corriendo, la pantalla pasa sola a **En
curso**.

Si algo sale mal en el camino, la pantalla de Espera se rinde a los pocos
segundos y vuelve a Principal. **Que vuelva a Principal significa que el
experimento no arrancó**, no que arrancó y falló.

### Los primeros 10 segundos

Apenas arranca hay una **ventana de asentamiento**: el equipo mide y muestra
todo con normalidad, pero durante esos segundos **no puede generar alertas ni
cortar**.

Es a propósito. En ese rato el regulador todavía está llevando el campo a la
consigna, así que el sistema está legítimamente fuera de rango; sin esa ventana,
todo experimento se abortaría a sí mismo al arrancar.

---

## 7. Durante el experimento

La pantalla **En curso** tiene un indicador de salud siempre visible y varias
pestañas.

### Indicador de salud

| Estado | Qué significa |
|---|---|
| **Normal** | Sin alertas activas. |
| **Advertencia** | Hay alertas, ninguna crítica. Vale la pena mirar. |
| **Crítico** | Hay al menos una alerta crítica. El experimento puede cortar solo. |

### Pestañas

- **Objetivos** — los parámetros con los que arrancó. Sirven de referencia
  contra lo que se está midiendo.
- **Progreso** — porcentaje y tiempo transcurrido.
- **Mediciones** — campo (mT), temperatura (°C) y corriente (A), cada una con
  el momento de su última actualización. Si dice **"Sin datos"**, ese sensor no
  está reportando: puede no estar conectado, o estar deshabilitado en la
  configuración. No es lo mismo que medir cero.
- **Alertas** — hasta tres alertas, la más reciente arriba.

### Cómo leer las alertas

Cada alerta trae de qué sensor viene, de qué tipo es y un contador. **El
contador es lo importante**: dice cuántas veces se acumuló esa condición y
cuántas hacen falta para cortar. Un `2/4` es un aviso; un `3/4` es que falta
una.

Si la misma condición se repite en el mismo sensor, la alerta **no se duplica**:
se actualiza el contador y sube al tope de la lista. Así que las tres que se ven
son tres condiciones distintas, no las últimas tres veces que pasó algo.

### Detener a mano

Botón **Detener**. La pantalla pasa a Espera ("Deteniendo…") y después a
Resultado. El experimento queda registrado como detenido por el operador, que a
efectos de los datos no es lo mismo que uno completado.

---

## 8. Cómo termina un experimento

Termina de tres maneras, y la pantalla **Resultado** dice cuál fue:

| Resultado | Qué pasó |
|---|---|
| **Exitoso** | Se cumplió la duración configurada. |
| **Falla** | El equipo cortó por una condición peligrosa sostenida. |
| **Detenido** | Lo detuvo una persona, desde la pantalla o el pulsador. |

En **Falla**, el detalle dice qué sensor lo provocó y con qué cuenta —por
ejemplo, que la temperatura alcanzó el límite de alertas críticas. Es la
información que hay que anotar: dice si el experimento se perdió por el equipo,
por la muestra o por el ambiente.

La pantalla también congela el progreso, el tiempo transcurrido y la salud **en
el instante en que terminó**. No siguen corriendo.

De Resultado se sale solo con **Volver**. El equipo no navega solo desde acá, a
propósito: el resultado tiene que poder leerse y anotarse sin apuro.

---

## 9. Parada de emergencia

El pulsador físico **corta el experimento en curso de inmediato**. Es el camino
más corto y no depende de la pantalla.

Dos cosas a saber:

- **Solo actúa si hay un experimento corriendo.** Apretarlo con el equipo en
  reposo no hace nada, y es deliberado: evita que quede registrado un resultado
  de un experimento que nunca existió.
- Deja el experimento como **Detenido**, igual que el botón de la pantalla.

El corte general de potencia es un relé, y tiene una protección que ignora
cambios de estado a menos de 200 ms uno de otro. En uso normal es invisible.

---

## 10. Monitoreo remoto

Con WiFi configurado, el equipo publica telemetría a un panel remoto mientras
hay un experimento corriendo: campo, temperatura, corriente, salud, progreso y
tiempo transcurrido.

Tres advertencias:

- **Solo publica durante un experimento.** Fuera de una corrida no hay datos, y
  eso no es una falla.
- **Es de supervisión, no de control.** No se puede iniciar ni detener nada
  desde el panel remoto. Lo que se ve es un reflejo.
- **Si se cae el WiFi, el experimento sigue.** La conectividad corre aparte y no
  frena la exposición ni la vigilancia. Se pierden los datos remotos de ese
  rato, nada más.

---

## 11. La tarjeta SD

El equipo lee `/biosoft/config.json` de la tarjeta al **encender**. Ahí viven
las opciones que ofrecen las listas de Configuración, cada cuánto se mide, los
parámetros del regulador, qué bobinas están habilitadas y con qué corrección,
qué vigila el detector y con qué reglas, y qué se publica remotamente.

Tres reglas que conviene tener claras:

1. **Sin tarjeta el equipo funciona.** Arranca con sus valores de fábrica y lo
   deja anotado. No hay un modo "bloqueado por falta de configuración".
2. **Un archivo mal formado se descarta entero**, no a medias. Se vuelve a los
   valores de fábrica. Nunca queda media configuración aplicada.
3. **Se lee solo al encender.** Cambiar el archivo con el equipo prendido no
   hace nada: hay que reiniciar.

Para editarlo, usar `tools/generador-config.html`: se abre con doble clic, no
necesita instalar nada y **valida antes de dejar guardar**. Vale la pena aunque
uno sepa escribir JSON, porque hay errores que el equipo no puede avisar — por
ejemplo, un valor entero escrito con coma decimal se descarta en silencio y, en
las listas de la pantalla, queda en cero.

---

## 12. Lo que todavía no está

Esta sección es la que hay que revisar antes de usar el equipo con animales.

- **El campo no está calibrado.** Los parámetros del regulador y los factores de
  corrección de cada bobina son valores de arranque, no medidos contra las
  bobinas reales. **Un factor en 1,0 significa "todavía sin medir"**, no
  "medido y sin desvío". Hasta que se ejecute
  `docs/protocolo-calibracion-intensidad.md`, la intensidad indicada no está
  verificada contra el campo real.
- **El modo de experimento no se elige desde la pantalla.** El equipo lo acepta
  y lo aplica, pero el selector todavía no existe en Configuración: hoy se
  define en el archivo de la tarjeta. Por defecto es **campo X**, nunca campo
  nulo — porque un campo nulo por accidente se vería igual que un experimento
  normal.
- **La inversión de fase depende de hardware que falta.** Hasta que esté
  montada, el modo campo nulo no cancela nada. Verificarlo antes de confiar en
  él: el paso 4 del protocolo de calibración dice cómo.
- **La vigilancia de campo viene apagada de fábrica.** El detector vigila la
  temperatura; el campo se mide y se muestra, pero por defecto no genera alertas
  ni corta. Se habilita desde el archivo de configuración, y conviene hacerlo
  recién con el sensor real montado y calibrado.
- **Los umbrales de campo son provisorios.** Los valores con los que se decide
  qué lectura de campo es anormal están copiados de los de temperatura, sin
  calibrar contra campo real.
- **La corriente no corta el experimento.** Se mide, se muestra y se publica,
  pero por diseño no es una condición de corte.

---

## 13. Qué hacer si…

| Síntoma | Qué significa | Qué hacer |
|---|---|---|
| Vuelve sola a la pantalla de Inicio | Se perdió el contacto con la placa de control | Revisar el cable serie y la alimentación de la placa de control |
| *Iniciar* deja la pantalla en Espera y vuelve a Principal | El experimento no arrancó | Revisar el enlace con la placa de control; reintentar |
| Una medición dice "Sin datos" | Ese sensor no reporta | Revisar conexión; confirmar que está habilitado en la configuración |
| El campo medido queda muy por debajo de la consigna | El regulador no llega | Ver si hay avisos de recorte: puede que una bobina no tenga margen. Requiere revisar el hardware o recalibrar |
| Corta apenas terminan los primeros segundos | Una condición fuera de rango desde el arranque | Ver la pestaña Alertas en la pantalla Resultado: dice qué sensor fue |
| Cambié el archivo de la SD y no pasó nada | Se lee solo al encender | Reiniciar la pantalla |
| Cambié la configuración y el panel remoto dejó de mostrar un valor | Se renombró un campo de telemetría | El panel remoto espera los nombres originales: revertirlos o actualizar el panel |
| El pulsador de emergencia no hace nada | No hay experimento corriendo | Es el comportamiento esperado |

---

## 14. Buenas prácticas

- **Mirar la pantalla Principal antes de arrancar.** Es el último punto donde se
  ve la configuración completa antes de exponer animales.
- **Anotar el resultado, no solo si terminó.** Un experimento que cortó por
  falla y uno que se completó no producen los mismos datos, y el detalle de la
  pantalla Resultado dice cuál fue la causa.
- **No cambiar parámetros entre tandas de la misma serie** sin registrarlo. El
  equipo no lleva historial: lo único que queda es lo que anote el operador.
- **Verificar el campo nulo antes de cada serie con grupo control.** Un control
  mal cancelado es un grupo tratado sin que nadie se entere, y es el error más
  caro que puede cometer este equipo: invalida la comparación entera sin dar
  ningún síntoma.
