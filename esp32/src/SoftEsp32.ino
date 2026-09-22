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
// comunicaciones. Sirve para aislar la UI de la red -- hoy el reintento TLS
// contra EMQX (falla cada 5 s por el CA/NTP pendiente) corre en el mismo
// core que LVGL y se ve como un parpadeo. Volver a false para produccion.
constexpr bool BENCH_SIN_RED = true;

SerialLink serial(Serial2);
Timer timer;
WiFiManager wiFiManager;
BrokerManager brokerManager;
DisplayDriver display;
ScreenManager screenManager;
SdStorage sdStorage;

MySystem mySystem(serial, timer, wiFiManager, brokerManager, display, screenManager, sdStorage);

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(2000);

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
    WiFiConfig config1;
    config1.add(SECRET_WIFI_SSID_1, SECRET_WIFI_PASS_1); // agregar aca todas las redes
    config1.add(SECRET_WIFI_SSID_2, SECRET_WIFI_PASS_2); // agregar aca todas las redes
    wiFiManager.begin(config1);

    MqttConfig config;
    config.server   = SECRET_MQTT_SERVER;
    config.port     = SECRET_MQTT_PORT;   // TLS
    config.user     = SECRET_MQTT_USER;
    config.password = SECRET_MQTT_PASSWORD;
    config.clientId = SECRET_MQTT_CLIENT_ID;
    config.rootCA   = ROOT_CA_CERT;
    brokerManager.begin(config);
  }
  else {
    DEBUG_PRINTLN(DEBUG_MAIN, F("[BENCH] BENCH_SIN_RED: WiFi y broker deshabilitados"));
  }

  mySystem.begin();

  if (!BENCH_SIN_RED) {
    xTaskCreatePinnedToCore(
      communicationTask,
      "CommunicationTask",
      8192,
      nullptr,
      1,
      nullptr,
      1
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



