# BioSoft — Monitor web remoto

Tablero web responsivo para seguir un experimento en curso desde afuera del
laboratorio y consultar los experimentos anteriores. El plan completo, con el
contrato de telemetria y las fases, esta en
[`docs/plan-monitor-web.md`](../docs/plan-monitor-web.md).

No toca el firmware: el ESP32 ya publica todo al broker MQTT. Esto es lo que
vive del otro lado.

```
ESP32 --MQTT/TLS--> EMQX --> backend Node --> MongoDB (historico)
                                  |
                                  +--Socket.IO--> navegador (tiempo real)
```

El navegador **nunca** habla con el broker: el backend es el unico cliente
MQTT. Asi las credenciales no salen del servidor y lo que se ve en vivo es
exactamente lo que se guardo.

## Estructura

```
web/
  server/        backend: ingesta MQTT, API REST de historicos, Socket.IO
  client/        frontend React + Vite
  tools/         simulador de la placa
  docker-compose.yml   Mongo + Mosquitto locales (solo desarrollo)
```

## Desarrollo

```
cd web
npm install
cp server/.env.example server/.env    # completar si se apunta al broker real
npm run infra:up                      # Mongo en 27017, Mosquitto en 1883
npm run dev                           # backend en :4000, front en :5173
```

Con eso, http://localhost:5173 muestra el estado de los tres enlaces y la
telemetria que va llegando. Para que llegue algo sin hardware:

```
node tools/simulador.js                    # corrida normal
node tools/simulador.js --reason critical   # corrida cortada por alerta
```

Diagnostico rapido de cual enlace se cayo:

```
curl http://localhost:4000/api/health
```

## Apuntar al broker real

En `server/.env`, `MQTT_URL=mqtts://<host>.emqxsl.com:8883` con el usuario y la
clave de `esp32/include/secrets.h`, y `MQTT_CA_PATH` al `.crt` descargado de la
consola de EMQX.

Dos advertencias que ya costaron tiempo en este proyecto:

- **`MQTT_CLIENT_ID` tiene que ser distinto del de la placa** y distinto entre
  local y produccion. Dos clientes con el mismo clientId se desconectan
  mutuamente en un bucle que desde afuera parece "el broker anda mal".
- **Nunca deshabilitar la verificacion del certificado.** Los fallos de TLS del
  ESP32 contra este broker eran de *reloj*, no de certificado (ver CLAUDE.md);
  en el servidor esa causa no existe, asi que un fallo de TLS aca es un problema
  real que hay que mirar.

## Estado

Fases 0 a 6 listas.

- **Fase 0** — workspace, infraestructura local, `/api/health`, Socket.IO,
  simulador y pantalla de diagnostico.
- **Fase 1** — modelos de Mongo e ingesta: `measures`, `coils` y `status` se
  guardan como time-series collections y `alerts` como coleccion normal, y cada
  una se emite en vivo **despues** de guardarse. Los retenidos no se guardan ni
  se emiten.

- **Fase 2** — correlacion de corridas: `targets` abre un `run`, `result` lo
  cierra, y cada muestra queda asociada a la corrida en curso. Una corrida sin
  `result` (backend caido, corte de luz) se marca `orphan` y se cierra con la
  fecha de su ultimo dato; un `result` sin corrida abierta crea igual una
  huerfana, porque el motivo del corte no lo reporta nadie mas. Al arrancar, el
  backend retoma la corrida que haya quedado abierta, asi que reiniciarlo a
  mitad de un experimento no lo parte en dos.

- **Fase 3** — tiempo real: el backend manda un `snapshot` con el estado
  completo apenas se conecta un cliente, y despues cada evento. La pantalla En
  curso muestra salud, modo, progreso, mediciones, bobinas, objetivos y
  alertas, envejece los datos que dejan de llegar y avisa cuando se cae
  cualquiera de los tres enlaces.

- **Fase 4** — historicos: API REST (`/api/runs`, series agregadas por bucket,
  alertas, export CSV) y las pantallas de Historial (con filtros por modo y por
  motivo) y Detalle, con graficos de campo, temperatura, corriente y duty.

- **Fase 5** — autenticacion: login con JWT, contraseñas con scrypt, y todo
  cerrado detras del token, **el WebSocket incluido** (es la otra puerta al
  mismo dato). Lo unico publico es `/api/ping`.

- **Fase 6** — despliegue: imagen Docker con backend + front en un solo
  contenedor y un solo origen, chequeos que hacen fallar el arranque si la
  configuracion de produccion esta mal, y el runbook en
  [`docs/despliegue-monitor-web.md`](../docs/despliegue-monitor-web.md).

**Desplegado y andando** en Fly.io desde el 2026-09-24, con MongoDB Atlas y el
mismo broker EMQX al que publica la placa. Una corrida simulada contra el
broker real llego de punta a punta.

Lo que queda: la fase 7 (pulido -- retencion de datos, filtro por fecha en el
Historial, avisos cuando se corta la ingesta) y, sobre todo, **la primera
corrida con la placa real**. Todo lo verificado hasta ahora salio del
simulador; el error de MODE (ver mas abajo) es lo que esa diferencia produce
cuando muerde.

Para desplegar hay un asistente que arma el archivo de variables y los
comandos: abrir `tools/generador-despliegue.html` con doble clic. Sus
validaciones se verifican con `node tools/test-generador-despliegue.js`.

**Antes de desplegar, leer el runbook**: la primera seccion explica por que
este servicio no puede dormir, que descarta el plan gratuito de Render para
tomar datos reales.

### Usuarios

El primer usuario sale de `SEED_USER`/`SEED_PASSWORD` del `.env`, y solo se
crea si la base no tiene ninguno (como **administrador**). Despues, los
usuarios se gestionan desde la pagina **Usuarios** del monitor (solo
administradores). Dos roles:

- **Administrador**: envia configuracion a la placa, borra corridas y gestiona
  usuarios.
- **Solo lectura**: ve todo lo demas.

Las cuentas anteriores a los roles pasan a administrador al arrancar. Siempre
queda al menos un administrador, y nadie se quita el rol ni se borra a si mismo.

Al crear una cuenta sin contraseña se genera un **enlace de invitacion** (72 h,
un solo uso) para que la persona elija la suya. "¿Olvidaste tu contraseña?" en
el login manda un enlace por correo (2 h) si el servidor tiene `SMTP_URL` y
`PUBLIC_URL` y la cuenta tiene correo; si no, un administrador genera el enlace
desde Usuarios ("Enlace de clave") y lo pasa a mano. Cambiar o restablecer una
contraseña, o borrar una cuenta, **cierra al instante** sus sesiones abiertas
(tabla de revocacion en memoria, `server/src/auth/sesiones.ts`).

La consola sigue como camino de ultimo recurso (crea administradores):

```
npm run usuario --workspace server -- <usuario> <contraseña>
```

`JWT_SECRET` es opcional en desarrollo (se genera al azar y se avisa) y
**obligatorio en produccion**: sin el, cada despliegue cierra todas las
sesiones abiertas.

El snapshot tambien esta como endpoint, para mirarlo sin navegador:

```
curl http://localhost:4000/api/live/snapshot
```

## Tests

```
npm test --workspace server
```

79 tests. Los de parseo corren solos; los de ingesta escriben de verdad en
Mongo y **se saltean con un aviso si el compose no esta levantado**, en vez de
fallar como si el codigo estuviera roto. Cada archivo usa SU PROPIA base
(`biosoft_test_<nombre>`): vitest corre los archivos en paralelo y con una sola
base el `deleteMany` de uno borra lo que el otro acaba de escribir, lo que da
fallas que aparecen y desaparecen segun el orden de los workers.
