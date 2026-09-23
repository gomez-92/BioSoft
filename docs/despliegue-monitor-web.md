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
  consola de EMQX, el mismo que compila el ESP32. Si se omite se usa el almacén
  de CAs del sistema, que para EMQX Cloud alcanza.
  Nunca deshabilitar la verificación: los fallos de TLS del ESP32 contra este
  broker eran de **reloj**, no de certificado (ver `CLAUDE.md`), y esa causa no
  existe en un servidor. Un fallo de TLS acá es un problema real.

Conviene crear **una credencial propia para el backend** en EMQX, distinta de la
de la placa: si alguna se filtra, se revoca una sola.

## 3. Variables de entorno

Todas se cargan en el panel de la plataforma. `.env` no se sube nunca.

| Variable | Obligatoria | Qué es |
|---|---|---|
| `NODE_ENV` | sí | `production` — activa los chequeos de arranque |
| `MONGO_URI` | sí | la URI de Atlas, con `/biosoft` al final |
| `MQTT_URL` | sí | `mqtts://<host>.emqxsl.com:8883` |
| `MQTT_USER`, `MQTT_PASSWORD` | sí | credencial del broker para el backend |
| `MQTT_CLIENT_ID` | sí | distinto del de la placa |
| `MQTT_CA` | no | el PEM del certificado, pegado entero |
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

```
fly launch --no-deploy            # usa el fly.toml del repo
fly secrets set MONGO_URI="..." MQTT_URL="..." MQTT_USER="..." \
  MQTT_PASSWORD="..." MQTT_CLIENT_ID="biosoft-backend-prod" \
  JWT_SECRET="$(openssl rand -hex 48)" SEED_USER="..." SEED_PASSWORD="..."
fly deploy
```

### Render

Conectar el repositorio; [`render.yaml`](../web/render.yaml) define el servicio.
El resto de las variables se cargan a mano en el panel, porque son credenciales.
Como el repo tiene el monitor en `web/`, hay que poner ese directorio como raíz
del servicio.

HTTPS lo dan las dos plataformas sin configurar nada.

## 5. Después de desplegar

```
curl https://<tu-dominio>/api/ping            # {"ok":true}
curl https://<tu-dominio>/api/runs            # 401: bien, está cerrado
```

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
