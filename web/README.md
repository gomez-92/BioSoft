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

Fase 0 (andamiaje) lista: workspace, infraestructura local, backend que se
conecta a Mongo y al broker, `/api/health`, Socket.IO reemitiendo lo que llega
y una pantalla de diagnostico. **Todavia no persiste nada**: eso es la fase 1.
