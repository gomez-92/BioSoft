# Plan de trabajo — Monitor web remoto (tiempo real + históricos)

Estado: propuesta, sin implementar. Fecha: 2026-09-23.

## 1. Qué se construye y qué ya existe

El ESP32 **ya publica** toda la telemetría al broker MQTT (EMQX Cloud serverless,
TLS 8883). Este plan **no toca el firmware**: lo que falta es todo lo que está
del otro lado del broker.

```
ESP32 ──MQTT/TLS 8883──> EMQX Cloud
                            │
                            │ (suscripción biosoft/telemetry/#)
                            v
                    ┌───────────────────┐
                    │  Backend Node     │
                    │  ingestor + API   │───> MongoDB Atlas (histórico)
                    └───────────────────┘
                            │ Socket.IO (WSS)
                            v
                    Frontend React (responsive)
```

Decisiones ya tomadas:

- **Stack**: Node + Express + TypeScript (mqtt.js, Socket.IO, Mongoose) y
  frontend React + Vite.
- **Tiempo real**: el backend es el **único cliente MQTT**. El navegador nunca
  habla con el broker. Las credenciales del broker no salen del servidor, y lo
  que se muestra en vivo es exactamente lo que se persistió.
- **Despliegue**: nube, con login simple (JWT, usuarios en Mongo). Mongo Atlas
  free tier.
- **Ubicación**: `web/` dentro de este repo, junto a `esp32/` y `mega2560/`.

## 2. El contrato de entrada (lo que realmente llega)

Fuente de verdad: `esp32/src/topics.hpp` y `MySystem::_publish*()`
(`esp32/src/mysystem.hpp`). Seis topics, tres periódicos y tres por evento.
Los nombres de topic y los cinco campos configurables (`CEM1`, `TEMP1`,
`ESTADO`, `PROGRESS`, `ELAPSED_TIME`) **los puede renombrar la tarjeta SD**
(sección `telemetry` de `docs/config-schema.md`); el resto de las claves son
estructurales y fijas.

| Topic | Cadencia | Retain | Payload |
|---|---|---|---|
| `biosoft/telemetry/measures` | 30 s (config.) mientras Running | sí | `{CEM1: float mT, TEMP1: float °C}` |
| `biosoft/telemetry/coils` | 30 s (config.) mientras Running | sí | `{c1..c4: float A, d1..d4: float %}` — **solo las bobinas que reportaron** |
| `biosoft/telemetry/status` | 30 s (config.) mientras Running | sí | `{ESTADO, PROGRESS int %, ELAPSED_TIME "hh:mm:ss", REMAINING seg, STATE, MEGA bool}` |
| `biosoft/telemetry/targets` | 1× al entrar en Running | sí | `{MODE, CEM, FREQ, DUR, TOL, TNMIN, TNMAX, TCMIN, TCMAX}` |
| `biosoft/telemetry/alerts` | por evento (`flag_data`) | **no** | `{SRC, TYPE, COUNT, LIMIT}` |
| `biosoft/telemetry/result` | 1× al cortar | sí | `{REASON, DESC, PROGRESS, ELAPSED "hh:mm:ss", MEAN?, SRC?, TYPE?, COUNT?, LIMIT?}` |

Valores: `MODE` ∈ {campo X, campo nulo} (texto del menú), `REASON` ∈
{`completed`, `critical`, `stopped`}, `TYPE` ∈ {`critical`, `streak`,
`frequency`}, `ESTADO` ∈ {`normal`, `warning`, `critical`}, `STATE` ∈ {`idle`,
`ready`, `starting`, `running`, `stopping`}.

### Cinco cosas del contrato que condicionan el diseño

1. **Ningún payload trae timestamp.** El sello de tiempo lo pone el ingestor al
   recibir. Consecuencia directa: la exactitud del histórico depende del reloj
   del servidor, no del de la placa, y un mensaje retenido entregado al
   suscribirse tiene *fecha de entrega*, no de medición.
2. **Los mensajes retenidos se reentregan en cada reconexión.** Cinco de los
   seis grupos van con `retain: true`. Si el backend se reinicia tres veces,
   recibe tres veces el mismo `targets` y el mismo `result` del experimento
   anterior. El ingestor **tiene que mirar `packet.retain`** y tratar esos
   mensajes como *snapshot inicial*, nunca como evento nuevo: no abren ni
   cierran experimentos y no se insertan como muestra.
3. **No hay identificador de equipo en el topic.** Hoy hay una sola placa. El
   esquema de datos lleva igual un `deviceId` (constante `"biosoft-01"` por
   configuración), para no tener que migrar la base el día que haya dos. Si eso
   pasa, el cambio limpio es mover a `biosoft/<deviceId>/telemetry/...` y tocar
   `topics.hpp` + el generador de config.
4. **`ELAPSED_TIME` y `ELAPSED` son texto `"hh:mm:ss"`, no números.** Se guardan
   parseados a segundos (y también crudos). Para gráficos conviene `PROGRESS` y
   `REMAINING`, que sí son numéricos.
5. **QoS 0 y sin cola.** Si el backend está caído, esa tanda se pierde y no
   vuelve. El histórico va a tener huecos legítimos: los gráficos deben mostrar
   cortes, no interpolar una línea recta sobre 40 minutos sin datos. El backend
   se suscribe con **QoS 1** y `clean: false` con un `clientId` fijo, que mejora
   el caso "backend reiniciado un instante" pero no el de un corte largo.

## 3. Modelo de datos (MongoDB)

Colección central: **`runs`** (un experimento). El resto son muestras que
apuntan a ella.

```js
// runs
{ _id, deviceId, startedAt, endedAt,
  state: "running" | "finished" | "orphan",
  targets: { mode, cem, freq, dur, tol, tnmin, tnmax, tcmin, tcmax },
  result:  { reason, description, progressPercent, elapsed, elapsedSeconds,
             meanMagneticField?, source?, type?, count?, limit? },
  stats:   { measureCount, alertCount, lastStatus } }

// measures  (time series: timeField ts, metaField runId)
{ ts, runId, deviceId, cem1, temp1 }

// coils
{ ts, runId, deviceId, coils: [{ n: 1, current, duty }, ...] }

// statuses
{ ts, runId, deviceId, health, progress, elapsedSeconds, remainingSeconds, state, megaOk }

// alerts
{ ts, runId, deviceId, source, type, count, limit }

// users
{ _id, username, passwordHash, role }
```

`measures`, `coils` y `statuses` como **time-series collections** de Mongo
(mejor compresión y consultas por rango) con índice por `runId`. `alerts` y
`runs` como colecciones normales.

### Correlación de experimentos (la parte no obvia)

El firmware no manda un id de corrida. Se infiere en el ingestor:

- `targets` **no retenido** → abre un `run` nuevo (`startedAt = now`,
  `state: "running"`), cerrando antes cualquier run abierto que haya quedado
  colgado (`state: "orphan"`, `endedAt` = último dato recibido).
- `result` **no retenido** → cierra el run abierto de ese `deviceId`
  (`endedAt = now`, `state: "finished"`, copia el resultado). Si no hay ninguno
  abierto (backend arrancado a mitad de experimento), crea uno `orphan` con solo
  el resultado, para que el corte no se pierda.
- `measures`/`coils`/`statuses`/`alerts` se adjuntan al run abierto; si no hay
  ninguno, se guardan con `runId: null` y quedan visibles en el vivo pero fuera
  de cualquier experimento.
- Un run abierto sin datos por más de N minutos (configurable, default 2× el
  intervalo de `status`) lo cierra un barrido periódico como `orphan`.

## 4. Backend — módulos

```
web/server/src/
  mqtt/ingestor.ts       # conexión, suscripción, dispatch por topic
  mqtt/handlers/*.ts     # uno por grupo: valida, normaliza, persiste, emite
  domain/runTracker.ts   # apertura/cierre de runs, huérfanos
  models/*.ts            # esquemas Mongoose
  api/routes/*.ts        # REST de históricos
  realtime/socket.ts     # Socket.IO: snapshot al conectar + broadcast
  auth/*.ts              # JWT, login, middleware
  config.ts              # env vars (broker, Mongo, JWT, deviceId)
```

Regla: **un solo camino de escritura**. El handler persiste y recién después
emite por Socket.IO, con el mismo objeto normalizado que guardó. Nada se emite
sin haberse guardado, así lo que se ve en vivo y lo que se consulta después no
pueden diferir.

### REST (todo bajo JWT)

```
POST /api/auth/login
GET  /api/runs?from&to&reason&mode&page          # lista paginada
GET  /api/runs/:id                               # cabecera + targets + result
GET  /api/runs/:id/measures?bucket=1m            # serie agregada ($group por bucket)
GET  /api/runs/:id/coils?bucket=1m
GET  /api/runs/:id/statuses
GET  /api/runs/:id/alerts
GET  /api/runs/:id/export.csv                    # descarga para análisis
GET  /api/live/snapshot                          # último estado conocido
GET  /api/health                                 # broker conectado? mongo? último msg
```

El `bucket` no es adorno: a 30 s por muestra, un experimento de 8 h son ~960
puntos por serie, y el día que el intervalo baje a 5 s son 5760. La agregación
por bucket en Mongo evita mandar todo al navegador.

### Socket.IO

- Evento `snapshot` al conectar: último `status`, `measures`, `coils`,
  `targets`, últimas N `alerts` y el run abierto. **Esto reemplaza al `retain`
  del broker para el navegador**, que no ve MQTT.
- Eventos `telemetry:measures`, `telemetry:coils`, `telemetry:status`,
  `telemetry:targets`, `telemetry:alert`, `telemetry:result`, `run:opened`,
  `run:closed`, `link:status` (broker arriba/abajo).
- Handshake autenticado con el mismo JWT del REST.

## 5. Frontend — pantallas

Responsive de verdad: la pantalla que más se va a usar es el celular mirando un
experimento en curso desde afuera del laboratorio.

1. **En vivo** — estado del experimento (chip normal/advertencia/crítico), barra
   de progreso + transcurrido/restante, campo e intensidad actuales, tabla de
   bobinas (corriente y duty, una fila por bobina que reportó), objetivos del
   run, lista de alertas recientes. Banner cuando el enlace con el Mega
   (`MEGA: false`) o con el broker se cae — que desde afuera es la diferencia
   entre "va bien" y "hace rato que no sabemos nada".
2. **Historial** — tabla de corridas con filtros (fecha, modo campo X/nulo,
   motivo de corte) y badges por `reason`.
3. **Detalle de corrida** — objetivos vs. medido, gráficos de campo y
   temperatura con las bandas normal/crítica dibujadas, gráfico de corriente y
   duty por bobina, línea de tiempo de alertas, panel de resultado (motivo +
   descripción + campo medio), botón de exportar CSV.
4. **Login**.

Gráficos con Recharts (o uDplot si el volumen molesta). Los huecos de datos se
dibujan como huecos.

## 6. Fases

| Fase | Entregable | Criterio de aceptación |
|---|---|---|
| **0. Andamiaje** | `web/` con server + client, Docker Compose (Mongo local), `.env.example`, README | `npm run dev` levanta los dos y `/api/health` responde |
| **1. Ingesta** | ingestor MQTT + modelos + persistencia, sin UI | Con el ESP32 publicando (o `mosquitto_pub` simulando), los 6 topics aterrizan en Mongo con el shape de arriba |
| **2. Runs** | `runTracker` + barrido de huérfanos | Un experimento completo (targets → N measures → result) queda como **un** run cerrado; reiniciar el backend a mitad no duplica ni pierde el run (test con retenidos) |
| **3. Tiempo real** | Socket.IO + pantalla En vivo | Un `mosquitto_pub` se ve en el navegador en < 1 s; recargar la página reconstruye el estado vía `snapshot` |
| **4. Históricos** | REST + Historial + Detalle | Se puede abrir una corrida de ayer y ver sus series y su resultado |
| **5. Auth** | login JWT + guardas en REST y socket | Sin token no hay dato: ni REST ni WebSocket |
| **6. Despliegue** | Atlas + backend y front publicados, HTTPS | Se monitorea un experimento real desde afuera de la red del laboratorio |
| **7. Pulido** | export CSV, responsive fino, reconexión, retención de datos | Checklist de la sección 8 |

Fases 1 y 2 son el corazón; 3 y 4 son mecánicas una vez que el dato está bien
guardado. Se puede trabajar 1–2 sin el hardware encendido usando el simulador
de la sección siguiente.

## 7. Pruebas

Mismo criterio que el resto del repo — **fijar lo que se rompe en silencio**:

- `web/server/test/` con Vitest + `mongodb-memory-server`:
  - parseo de cada payload, incluyendo `coils` parcial (solo `c1`/`d1`) y
    `result` sin los campos opcionales;
  - `"hh:mm:ss"` → segundos, incluyendo > 24 h;
  - **retenidos**: un `targets` con `retain: true` no abre un run; un `result`
    retenido no cierra el que está en curso;
  - dos `targets` seguidos sin `result` en el medio → el primero queda `orphan`,
    no se pierde;
  - un `result` sin run abierto → run `orphan` con resultado.
- `web/tools/simulador.js`: publica una corrida entera contra un broker local
  (Mosquitto en Docker) con tiempos comprimidos. Es lo que permite probar sin
  placa y lo que hace reproducible el caso "experimento cortado por alerta
  crítica".

## 8. Riesgos y decisiones abiertas

- **TLS contra EMQX serverless.** El backend necesita el mismo CA que la placa.
  Nota del repo: los fallos de TLS en el ESP32 eran de **reloj**, no de
  certificado; en el servidor no aplica, pero conviene reusar el `.crt`
  descargado de la consola en vez de `rejectUnauthorized: false`.
- **Dos clientes MQTT con el mismo `clientId` se desconectan mutuamente.** El
  backend usa un `clientId` propio, distinto del de la placa
  (`SECRET_MQTT_CLIENT_ID`), y distinto entre entorno local y producción — si no,
  local y nube se van a estar pateando la conexión todo el día.
- **Free tier de Atlas: 512 MB.** A 30 s por muestra y 3 colecciones, ~8 MB por
  cada 100 h de experimento; entra cómodo, pero conviene definir desde ahora un
  TTL sobre las muestras crudas (ej. 1 año) dejando `runs` sin vencimiento. La
  decisión de cuánto se conserva es del experimento, no técnica: **preguntar
  antes de poner un TTL.**
- **Credenciales.** El `.env` del backend lleva usuario y password del broker y
  la URI de Atlas. No va al repo; `.env.example` con placeholders, igual que
  `secrets.example.h`.
- **Este monitor es de solo lectura, por diseño.** No se agregan comandos
  remotos: un `start` remoto energizaría bobinas con animales adentro y sin
  nadie en la sala, y el paro de emergencia solo sirve a quien ya está ahí. Si
  alguna vez se discute, es una decisión de seguridad del experimento, no una
  funcionalidad pendiente.
- **Renombrar campos desde la SD rompe el dashboard en silencio.** Los cinco
  campos configurables llegan con el nombre que diga la tarjeta. El backend los
  lee por nombre configurable (mismo `.env`), y ante una clave desconocida
  **loguea el payload entero** en vez de descartarlo callado: es la única pista
  de que alguien renombró algo.
