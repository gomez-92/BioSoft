# BioSoft

Proyecto final de Bioingeniería (Mario) — sistema de exposición controlada a
campo electromagnético de baja frecuencia para animales de experimentación,
en el marco de una investigación de la facultad.

Tres piezas, un mismo experimento:

- **`esp32/`** — pantalla táctil (LVGL) que muestra el estado del sistema,
  permite configurar los parámetros del experimento y publica telemetría por
  MQTT.
- **`mega2560/`** — control físico: genera el campo (bobina), mide campo /
  corriente / temperatura, y decide cuándo cortar el experimento (por
  tiempo cumplido, por peligro, o por pérdida de rigor científico).
- **`web/`** — monitor remoto: escucha esa telemetría, la guarda en MongoDB y
  la muestra en vivo y como histórico consultable. No toca el firmware, vive
  del otro lado del broker, y es de **solo lectura** por diseño.

Las dos placas se comunican por serial con un protocolo de frames propio
(`seriallink.hpp`, duplicado a propósito en cada placa). El monitor no habla
con ninguna: solo con el broker.

## Compilar

Cada carpeta de firmware es un proyecto PlatformIO independiente:

```
cd esp32 && pio run
cd mega2560 && pio run
```

El monitor web es un workspace npm aparte:

```
cd web && npm install
npm run infra:up      # Mongo y Mosquitto locales
npm run dev           # backend en :4000, front en :5173
npm test              # 81 tests del backend + 5 del front + 47 del generador
```

Sin la placa encendida, `node tools/simulador.js` publica una corrida entera
contra el broker local. Ver [`web/README.md`](web/README.md).

## Credenciales

`esp32/include/secrets.h` (WiFi + MQTT) no se versiona. Copiar
`esp32/include/secrets.example.h` como `secrets.h` y completar con los
valores reales antes de compilar.

## Documentación

| Documento | Qué cubre |
|---|---|
| [`docs/manual-de-usuario.html`](docs/manual-de-usuario.html) | Manual del operador: cableado, uso del equipo, monitor web, limitaciones vigentes. Es la **fuente única**; el PDF y el `.docx` se generan de ahí |
| [`docs/plan-monitor-web.md`](docs/plan-monitor-web.md) | Diseño del monitor web: contrato de telemetría, modelo de datos, fases |
| [`docs/despliegue-monitor-web.md`](docs/despliegue-monitor-web.md) | Cómo desplegarlo, con los fallos reales y su diagnóstico |
| [`docs/config-schema.md`](docs/config-schema.md) | El archivo de la tarjeta SD |
| [`docs/coil-excitation.md`](docs/coil-excitation.md) | Cadena de excitación de las bobinas |
| [`docs/protocolo-calibracion-intensidad.md`](docs/protocolo-calibracion-intensidad.md) | Procedimiento de calibración en banco |
| [`docs/etapa-de-fase.html`](docs/etapa-de-fase.html) | Esquemático del inversor de fase |
| [`docs/adr/`](docs/adr/) | Decisiones de arquitectura y por qué se tomaron |

Dos herramientas que se abren con doble clic, sin instalar nada:

- [`tools/generador-config.html`](tools/generador-config.html) — arma el
  `config.json` de la tarjeta SD.
- [`web/tools/generador-despliegue.html`](web/tools/generador-despliegue.html) —
  arma las variables y los comandos para desplegar el monitor.
