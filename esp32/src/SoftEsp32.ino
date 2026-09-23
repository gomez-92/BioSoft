#pragma once
#include "secrets.h"
#include "seriallink.hpp"
#include "wifimanager.hpp"
#include "timer.hpp"
#include "broker.hpp"
#include "display.hpp"
#include "screenmanager.hpp"
#include "screencontroller.hpp"
#include "sdstorage.hpp"
#include "configloader.hpp"
#include "mysystem.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_MAIN = true;

#define SERIAL_RX_BUFFER_SIZE 512
#define RXD2 22  // RX del ESP32 ← TX del Mega
#define TXD2 27  // TX del ESP32 → RX del Mega

// CA raiz del broker: DigiCert Global Root G2.
//
// VERIFICADO (2026-09-22) contra el certificado bajado de la consola de EMQX
// para esta instancia: mismo SHA-256
// CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F.
// Vale hasta 2038.
//
// Hubo una nota en CLAUDE.md que afirmaba que EMQX serverless firma con
// Let's Encrypt y que por eso fallaba el handshake. Era falso: la consola
// entrega exactamente este certificado. El "-9984 X509 - Certificate
// verification failed" venia del reloj -- un ESP32 arranca en 1970 y toda
// fecha de validez queda en el futuro -- y eso lo resuelve el NTP que
// MySystem sincroniza al conectar el WiFi.
static const char* ROOT_CA_CERT = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

// Banco: con true no se levanta WiFi ni broker y no se crea la tarea de
// comunicaciones. Sirve para aislar la UI de la red.
//
// Se puso en true mientras el reintento TLS fallido cada 5 s corria en el
// mismo core que LVGL y se veia como un parpadeo de la pantalla. Eso ya no
// pasa: communicationTask esta en el core 0 (ver mas abajo), asi que un
// handshake que falla no le roba tiempo al refresco. Queda como interruptor
// para aislar la UI de la red cuando haga falta, no como parche.
constexpr bool BENCH_SIN_RED = false;

SerialLink serial(Serial2);
Timer timer;
WiFiManager wiFiManager;
BrokerManager brokerManager;
DisplayDriver display;
ScreenManager screenManager;
SdStorage sdStorage;

MySystem mySystem(serial, timer, wiFiManager, brokerManager, display, screenManager, sdStorage);

// Cuanto se espera, ANTES de crear la interfaz, a tener WiFi + hora + broker.
// Vencido el plazo el arranque sigue igual: la UI aparece y el broker seguira
// reintentando (con menos chances, ver abajo).
constexpr unsigned long ESPERA_CONEXION_MS = 15000;

// Interruptor de la conexion temprana.
//
// EN true LA PLACA NO ARRANCA mientras las seis pantallas de LVGL se creen
// todas juntas: el handshake se queda con ~45 KB y LVGL necesita ~176 KB,
// contra los ~171 KB que hay libres en ese punto. LVGL pide memoria, malloc
// devuelve null y LV_ASSERT_MALLOC aborta -- reinicio en bucle, justo
// despues de inicializar el touch.
//
// Las dos mitades no son independientes: esto solo puede quedar en true
// cuando las pantallas se creen y destruyan por grupos (tarjeta 18 del
// board), que baja lo que ocupa LVGL de 176 KB a lo que pesen una o dos
// pantallas. Hasta entonces, false = arranca pero sin broker.
constexpr bool CONECTAR_ANTES_DE_LA_UI = false;

// Conecta con el heap todavia entero.
//
// Medido en la placa: al arrancar hay 253 KB libres con un bloque contiguo de
// 110 KB; despues de que LVGL crea las seis pantallas quedan 77 KB con un
// bloque maximo de 45 KB. mbedtls_ssl_setup() pide DOS buffers de ~16,7 KB
// (MBEDTLS_SSL_MAX_CONTENT_LEN=16384 en el SDK del core, sin buffers
// asimetricos ni variables) mas las estructuras de handshake: contra 45 KB no
// entra, y falla con -32512 SSL_ALLOC_FAILED. Contra 110 KB entra holgado.
//
// El costo es que la pantalla queda en negro estos segundos, y que una
// reconexion POSTERIOR encuentra el heap ya fragmentado y probablemente
// falle igual. Lo segundo se ataca destruyendo las pantallas que no estan a
// la vista; esto de aca resuelve el arranque.
static void conectarAntesDeLaInterfaz() {
  DEBUG_PRINTLN(DEBUG_MAIN, F("[MAIN] Conectando antes de crear la UI (pantalla en negro)"));

  unsigned long inicio = millis();
  while (millis() - inicio < ESPERA_CONEXION_MS) {
    // Reusa el camino real -- update de WiFi, gate de NTP y loop del broker --
    // en vez de duplicar la logica. MySystem ya se registro como listener de
    // los dos managers en su constructor, que corre antes de setup().
    mySystem.remoteUpdate();

    if (brokerManager.isConnected()) {
      DEBUG_PRINTF(DEBUG_MAIN, "[MAIN] Broker conectado en %lu ms\n", millis() - inicio);
      return;
    }
    delay(50);
  }

  DEBUG_PRINTLN(DEBUG_MAIN, F("[MAIN] Sin broker al vencer el plazo: se sigue arrancando"));
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(2000);

  // Referencia inicial: cuanto heap hay ANTES de crear nada. Contra la
  // medicion de despues de mySystem.begin() dice exactamente cuanto se
  // lleva LVGL, que es contra quien compite el handshake TLS.
  DEBUG_PRINTF(DEBUG_MAIN, "[MAIN] Heap al arrancar: libre %u, bloque mayor %u\n",
               (unsigned) ESP.getFreeHeap(), (unsigned) ESP.getMaxAllocHeap());

  if (!sdStorage.begin()) {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[SD] Continuando sin tarjeta SD"));
  }

  // Tiene que correr ANTES de que MySystem registre sus tareas y de que la
  // pantalla Configuracion arme sus dropdowns: pisa los intervalos y las
  // opciones que ambos leen. Si no hay archivo o esta mal, quedan los
  // defaults compilados y el arranque sigue igual -- no hay rama de error
  // que atender acá.
  ConfigLoader::load(sdStorage);

  if (!BENCH_SIN_RED) {
    // Redes: si la tarjeta trae alguna, REEMPLAZAN a las compiladas. No se
    // suman, porque entonces una red vieja de secrets.h no se podria sacar
    // nunca desde la tarjeta (ver ConfigLoader::loadWifi).
    WiFiConfig config1;
    if (ConfigLoader::wifiNetworkCount() > 0) {
      const ConfigLoader::WifiNetworkConfig* networks = ConfigLoader::wifiNetworks();
      for (uint8_t i = 0; i < ConfigLoader::wifiNetworkCount(); i++) {
        config1.add(networks[i].ssid, networks[i].password);
      }
    }
    else {
      config1.add(SECRET_WIFI_SSID_1, SECRET_WIFI_PASS_1); // agregar aca todas las redes
      config1.add(SECRET_WIFI_SSID_2, SECRET_WIFI_PASS_2); // agregar aca todas las redes
    }
    wiFiManager.begin(config1);

    // El broker, en cambio, se pisa clave por clave: lo que la tarjeta no
    // traiga se queda con lo de secrets.h. Asi se puede cambiar solo el host
    // sin repetir usuario y contrasena en el archivo.
    const ConfigLoader::BrokerConfig& sdBroker = ConfigLoader::brokerConfig();

    MqttConfig config;
    config.server   = sdBroker.server[0]   != '\0' ? sdBroker.server   : SECRET_MQTT_SERVER;
    config.port     = sdBroker.port        != 0     ? sdBroker.port     : SECRET_MQTT_PORT;
    config.user     = sdBroker.user[0]     != '\0' ? sdBroker.user     : SECRET_MQTT_USER;
    config.password = sdBroker.password[0] != '\0' ? sdBroker.password : SECRET_MQTT_PASSWORD;
    config.clientId = sdBroker.clientId[0] != '\0' ? sdBroker.clientId : SECRET_MQTT_CLIENT_ID;
    // El certificado sigue compilado: un PEM dentro de un JSON obliga a
    // escapar cada salto de linea, y es el unico dato de esta seccion que
    // no es una linea de texto.
    config.rootCA   = ROOT_CA_CERT;
    brokerManager.begin(config);
  }
  else {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[BENCH] BENCH_SIN_RED: WiFi y broker deshabilitados"));
  }

  // ANTES de mySystem.begin(), que es quien crea las pantallas de LVGL: el
  // handshake TLS necesita el heap sin fragmentar (ver el comentario de
  // conectarAntesDeLaInterfaz).
  if (!BENCH_SIN_RED && CONECTAR_ANTES_DE_LA_UI) {
    conectarAntesDeLaInterfaz();
  }

  mySystem.begin();

  // Referencia para diagnosticar el heap: begin() ya creo las seis pantallas
  // de LVGL, que con LV_USE_STDLIB_MALLOC en CLIB salen de este mismo heap y
  // no de un pool propio. Lo que quede aca es contra lo que compite el
  // handshake TLS, que necesita del orden de 40-50 KB contiguos.
  DEBUG_PRINTF(DEBUG_MAIN, "[MAIN] Heap tras inicializar la UI: libre %u, bloque mayor %u\n",
               (unsigned) ESP.getFreeHeap(), (unsigned) ESP.getMaxAllocHeap());

  if (!BENCH_SIN_RED) {
    // Core 0, NO core 1: en el 1 corren loop() y con el LVGL, y un handshake
    // TLS que falla es lo bastante pesado como para verse en pantalla --
    // cada reintento contra un broker que rechaza el certificado se notaba
    // como un parpadeo cada 5 s. El core 0 es ademas donde ya vive el stack
    // de WiFi, asi que la red queda toda de un lado y la UI del otro.
    xTaskCreatePinnedToCore(
      communicationTask,
      "CommunicationTask",
      8192,
      nullptr,
      1,          // prioridad
      nullptr,
      0           // core
    );
  }
}

void loop() {
  display.update();
  mySystem.update();
}


void communicationTask(void* parameter) {
  DEBUG_PRINTLN(DEBUG_MAIN, "[COMM TASK] iniciada");
  for (;;) {
    mySystem.remoteUpdate();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}



