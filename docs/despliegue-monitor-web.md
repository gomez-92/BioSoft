# Despliegue del monitor web

Cómo poner [`web/`](../web/) en la nube: base de datos, broker, servicio y
credenciales. El plan general y el contrato de telemetría están en
[`plan-monitor-web.md`](plan-monitor-web.md).

El servicio es **un solo contenedor**: el backend sirve también el front ya
compilado, desde el mismo origen. No hay que desplegar dos cosas ni configurar
CORS, y cambiar de dominio no obliga a recompilar el front.

---

## Lo primero, porque condiciona la elección de plataforma

**Este servicio no puede dormir.**

No es una web que solo hace falta cuando alguien la visita: es el **único
cliente MQTT** que escucha la telemetría de la placa. Si la plataforma lo
suspende por falta de visitas HTTP, deja de escuchar — y como la placa publica
con **QoS 0 y sin cola**, lo que se publique mientras duerme no se recupera
nunca. Un experimento de ocho horas monitoreado por un servicio que duerme
produce un histórico con agujeros permanentes, y nada en la pantalla dice que
faltan datos: se ven como tramos sin medición.

Por eso:

| Plataforma | Qué usar | Qué NO |
|---|---|---|
| **Fly.io** | `min_machines_running = 1` y `auto_stop_machines = false` (ya está en [`fly.toml`](../web/fly.toml)) | dejar el autostop por defecto |
| **Render** | plan `starter` (pago) | el plan gratuito: **duerme a los 15 min sin visitas** |
| **Railway** | plan con servicio siempre activo | el modo que escala a cero |

El plan gratuito de Render sirve para **probar que el despliegue funciona**, no
para tomar datos de un experimento real.

**Las dos piden tarjeta para lo que sirve.** Fly terminó su período de prueba
gratuito: sin tarjeta las máquinas se apagan a los cinco minutos y en algún
momento deja de desplegar del todo. Render no pide tarjeta para el plan Free,
que duerme. No hay alojamiento gratuito de un proceso encendido 24/7, porque es
lo que cuesta plata. Las alternativas sin tarjeta, con sus contras:

- **GitHub Student Developer Pack**, si se puede verificar la condición de
  estudiante: da créditos reales de proveedores que sí alojan esto.
- **Una PC del laboratorio + un túnel de Cloudflare**: gratis y sin tarjeta, con
  URL pública y HTTPS. No duerme mientras esa PC esté encendida — y durante un
  experimento lo está.

---

## 1. Base de datos — MongoDB Atlas

1. Crear un cluster gratuito (M0) en [Atlas](https://cloud.mongodb.com).
2. **Database Access** → crear un usuario con permiso de lectura/escritura sobre
   la base `biosoft`.
3. **Network Access** → agregar `0.0.0.0/0`.
   Sí, es abrir la base a todo internet a nivel de red: Render y Fly no dan IPs
   fijas en los planes chicos, así que no hay lista blanca posible. Lo que
   protege la base es la credencial, que por eso tiene que ser larga y única.
   Si más adelante el servicio queda en una IP fija, esto se puede cerrar.
4. Copiar la URI de conexión y agregarle el nombre de la base:
   `mongodb+srv://usuario:clave@cluster.xxxxx.mongodb.net/biosoft`
5. **Probarla antes de desplegar.** Tarda tres segundos y evita descubrir el
   problema después de un build completo:

   ```
   node tools/probar-mongo.js "<la uri>"
   ```

   Comprueba por separado el DNS, la conexión, la credencial y el **permiso de
   escritura**. Ese último es el que más engaña: con un usuario de sólo lectura
   el monitor arranca, no da ningún error y no guarda nada.

Dos confusiones que cuestan tiempo, las dos vistas en el primer despliegue:

- **El usuario de base de datos no es la cuenta de Atlas.** Si entraste al panel
  con Google, esa cuenta es para administrar; la que va en la URI es otra, la de
  Database Access, con su propia contraseña.
- **El hostname del cluster hay que copiarlo, no escribirlo.** Un carácter
  distinto en el identificador del proyecto da `querySrv ENOTFOUND`, que parece
  un problema de red y no lo es.

**Retención.** El plan M0 son 512 MB. A 30 s por muestra, unos 8 MB por cada
100 h de experimento — entra cómodo, pero conviene decidir cuánto se conserva
*antes* de que se llene, no después. Esa decisión es del experimento, no
técnica: qué se archiva y qué se borra lo define quien usa los datos.

## 2. Broker — el mismo EMQX de la placa

No hay nada que crear: el backend se suscribe al broker al que **ya publica** el
ESP32 (`esp32/include/secrets.h`).

Tres cosas, y las tres ya costaron tiempo en este proyecto:

- **`MQTT_CLIENT_ID` tiene que ser distinto del de la placa**, y distinto entre
  el entorno local y el de producción. Dos clientes con el mismo `clientId` se
  desconectan mutuamente en un bucle que desde afuera parece "el broker anda
  mal".
- **TLS siempre** (`mqtts://...:8883`). El servidor **se niega a arrancar** con
  un broker sin TLS cuando `NODE_ENV=production`; no es un aviso que se pueda
  pasar por alto.
- **El certificado va en `MQTT_CA`**, pegado como variable de entorno (en la
  nube no hay dónde dejar un archivo suelto). Es el `.crt` que se descarga de la
  consola de EMQX, el mismo que compila el ESP32 — está en
  [`web/emqxsl-ca.crt`](../web/emqxsl-ca.crt).
  **No es opcional en la práctica**: sin él, el contenedor falla con
  `unable to get local issuer certificate`, porque el almacén de CAs de una
  imagen Alpine mínima no alcanza para completar la cadena de este broker. La
  placa no sufre esto porque lleva el certificado compilado adentro.
  El valor viaja con los saltos de línea escapados (`\n`), que es la única
  forma de meter un PEM en una variable de entorno; el servidor los devuelve a
  saltos reales al leerlo.
  Nunca deshabilitar la verificación: los fallos de TLS del ESP32 contra este
  broker eran de **reloj**, no de certificado (ver `CLAUDE.md`), y esa causa no
  existe en un servidor. Un fallo de TLS acá es un problema real.

Conviene crear **una credencial propia para el backend** en EMQX, distinta de la
de la placa: si alguna se filtra, se revoca una sola.

**Regla de acceso de la configuración remota (tarjeta 24).** El monitor puede
reescribir la tarjeta SD de la placa publicando en `biosoft/config/set`. En la
consola de EMQX (Access Control → Authorization) hay que dejar que **solo la
credencial del backend** publique en ese topic, y que la de la placa solo se
suscriba a él:

| Credencial | `biosoft/config/set` | `biosoft/config/status`, `biosoft/config/current/#` |
|---|---|---|
| backend | publicar | suscribirse |
| placa | suscribirse | publicar |
| cualquier otra | denegar | — |

Sin esa regla, cualquiera con una credencial del broker (el simulador, otra
placa) puede mandarle una configuración. No hay forma de configurarla desde el
repositorio: es un paso manual, una sola vez.

## Atajo: el asistente

`web/tools/generador-despliegue.html` se abre con doble clic (sin build, sin
dependencias, sin internet) y hace los pasos 3 y 4 por vos: completás los
campos y te da el archivo `biosoft.env` listo y los comandos exactos, con las
validaciones que atrapan los errores que si no aparecen **recién desplegado**
— el `<password>` sin reemplazar en la URI de Atlas, el nombre de base
faltante, un broker sin TLS, o el Client ID repetido con el de la placa.

No ejecuta nada: una página web no puede correr comandos en tu máquina, y está
bien que no pueda. Copiás y pegás.

Las secciones 1 y 2 (Atlas y broker) siguen siendo a mano, porque son cuentas
de terceros.

## 3. Variables de entorno

Todas se cargan en el panel de la plataforma. `.env` no se sube nunca.

| Variable | Obligatoria | Qué es |
|---|---|---|
| `NODE_ENV` | sí | `production` — activa los chequeos de arranque |
| `MONGO_URI` | sí | la URI de Atlas, con `/biosoft` al final |
| `MQTT_URL` | sí | `mqtts://<host>.emqxsl.com:8883` |
| `MQTT_USER`, `MQTT_PASSWORD` | sí | credencial del broker para el backend |
| `MQTT_CLIENT_ID` | sí | distinto del de la placa |
| `MQTT_CA` | **en la práctica sí** | el PEM del certificado en una línea. Sin él: `unable to get local issuer certificate` |
| `JWT_SECRET` | **sí** | `openssl rand -hex 48`. Sin esto el servidor no arranca en producción |
| `JWT_EXPIRES_IN` | no | `7d` por defecto |
| `SEED_USER`, `SEED_PASSWORD` | la primera vez | crean el primer usuario, y **solo** si la base no tiene ninguno |
| `DEVICE_ID` | no | `biosoft-01` por defecto |
| `RUN_STALE_SECONDS` | no | 300 s sin datos y la corrida se cierra como huérfana |
| `CORS_ORIGINS` | no | vacío = solo el mismo origen. Solo hace falta si el front se sirve desde otro dominio |

`JWT_SECRET` es obligatorio por una razón concreta: sin él se genera una clave
al azar en cada arranque, y entonces **cada despliegue cierra todas las sesiones
abiertas**.

`SEED_PASSWORD` **no pisa una cuenta existente**. Dejarla puesta en el entorno
no revierte un cambio de contraseña hecho después; solo actúa si la base no
tiene ningún usuario.

## 4. Desplegar

### Fly.io

Primero, la CLI. **No se instala con npm** — es un binario propio, y el paquete
llamado `fly` en npm es una librería sin relación:

```
winget install Fly-io.flyctl                  # Windows
curl -L https://fly.io/install.sh | sh        # macOS / Linux
```

Hay que **cerrar y volver a abrir la terminal** para que tome el PATH. El
comando queda como `flyctl`; el alias `fly` puede no estar.

```
flyctl version
flyctl auth login        # o signup, abre el navegador
```

Y después:

```
cd web
flyctl launch --no-deploy            # el comando es flyctl, no fly
git checkout fly.toml                # ver la advertencia de abajo
Get-Content biosoft.env | flyctl secrets import    # PowerShell
flyctl deploy
```

**`flyctl launch` reescribe `fly.toml`** con sus valores por defecto, y lo que
pisa es justamente lo que no puede perderse: deja `auto_stop_machines = 'stop'`
y `min_machines_running = 0`, con lo cual la máquina se apaga cuando nadie
visita el sitio. Además borra el health check y sube la memoria a 1 GB. Por eso
el `git checkout` inmediatamente después.

En bash el import es `flyctl secrets import < biosoft.env`. **PowerShell no
soporta `<`** («El operador '<' está reservado para uso futuro»), de ahí el
`Get-Content`.

Los secretos se importan **desde un archivo**, no con `fly secrets set VAR="..."`.
No es preferencia: una contraseña que contenga `$`, `"`, `'` o `!` la interpreta
el shell antes de que llegue a `fly`. En el mejor caso el comando falla; en el
peor guarda un valor distinto del que escribiste — `$HOME` se expande y lo que
queda en el secreto es otra cosa, sin ningún error. Después el servicio no se
conecta al broker y no hay nada en los logs que diga por qué.

### Render

Conectar el repositorio; [`render.yaml`](../web/render.yaml) define el servicio.
El resto de las variables se cargan a mano en el panel, porque son credenciales.
Como el repo tiene el monitor en `web/`, hay que poner ese directorio como raíz
del servicio.

HTTPS lo dan las dos plataformas sin configurar nada.

## 5. Después de desplegar

```
curl https://<tu-dominio>/api/ping    # {"ok":true,"mongo":true,"mqtt":true}
curl https://<tu-dominio>/api/runs    # 401: bien, está cerrado
```

**Los dos booleanos son la verificación, no el 200.** El servidor responde 200
igual con los dos enlaces caídos: `ok` sólo dice que el proceso está vivo. Ese
endpoint es público a propósito — con la base caída no se puede iniciar sesión,
así que si el diagnóstico estuviera detrás del login sería inalcanzable justo
cuando hace falta.

Entrar con el usuario sembrado y **cambiar la contraseña enseguida**, porque la
inicial quedó escrita en las variables de entorno:

```
npm run usuario --workspace server -- <usuario> <contraseña-nueva>
```

Y después, la prueba que de verdad importa: **correr un experimento y ver que
aparece**. Si no hay placa disponible, el simulador publica contra el broker
real desde cualquier máquina:

```
node tools/simulador.js --url "mqtts://usuario:clave@<host>.emqxsl.com:8883"
```

Ojo con esto último: **son datos falsos entrando al histórico real**, y cinco de
los seis topics van con `retain`, así que el broker le va a entregar ese
`result` inventado a cualquiera que se suscriba después — el dashboard real
incluido — hasta que una corrida verdadera lo pise. Para una prueba de
despliegue está bien; después conviene borrar esa corrida de Atlas.

## 6. Qué mirar cuando algo no llega

`/api/health` (pide token) dice cuál de los tres enlaces se cayó y cuándo llegó
el último mensaje de cada grupo de telemetría. En orden:

1. **`mqtt.connected` en `false`** → credencial, `clientId` duplicado con la
   placa, o TLS.
2. **`mqtt.connected` en `true` pero `lastMessageAt` viejo** → el problema está
   del lado de la placa: no está publicando. Mirar el `status` del ESP32.
3. **`mongo` desconectado** → la URI o el `0.0.0.0/0` de Atlas. Con Mongo caído
   el backend **no guarda y por lo tanto tampoco emite en vivo**: la pantalla se
   queda con el último dato, atenuado.
4. **Todo en verde y la pantalla vacía** → sesión vencida; el front manda al
   login solo.

Y los mensajes concretos que aparecieron en el primer despliegue, con su causa:

| En los logs | Qué es |
|---|---|
| `querySrv ENOTFOUND ...mongodb.net` | El hostname del cluster está mal |
| `bad auth : authentication failed` | La contraseña guardada no es la del usuario de base |
| `ETIMEDOUT` / `ServerSelection` | Falta `0.0.0.0/0` en Network Access |
| `unable to get local issuer certificate` | Falta `MQTT_CA` |
| `Connection refused: Not authorized` | La credencial del broker no existe o está mal |
| `no se pudo suscribir a biosoft/...` | A esa credencial le falta permiso de *Subscribe* |
| `Trial machine stopping` | Fly, sin tarjeta |

Una advertencia sobre cargar secretos con `flyctl secrets set VAR="..."` en
PowerShell: **las comillas dobles expanden `$`**. Si la contraseña tiene un
`$`, se guarda un valor distinto del que escribiste, sin ningún error, y el
síntoma es un `bad auth` que parece de Atlas. Por eso el asistente entrega un
archivo y el runbook usa `import`.

---

## Nota sobre la construcción local de la imagen

`docker build` de [`web/Dockerfile`](../web/Dockerfile) **falla en la máquina de
desarrollo actual**, y no por el Dockerfile: **Avast Web/Mail Shield intercepta
TLS**, el contenedor no conoce su certificado raíz y npm no puede verificar el
registry (`UNABLE_TO_VERIFY_LEAF_SIGNATURE`, que npm reporta como el inútil
`Exit handler never called!`).

No afecta al despliegue: Render y Fly construyen la imagen en su propia
infraestructura. Para construirla localmente hay que desactivar el escaneo HTTPS
de Avast, o agregar su CA raíz al contenedor.
