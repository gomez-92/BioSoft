#pragma once
#include <Arduino.h>

// Configuracion de la telemetria remota del ESP32 (no es parte del protocolo
// serie con el Mega -- ver seriallink.hpp/commands.hpp para eso). El monitor
// remoto es un dashboard propio que se suscribe como cliente MQTT del mismo
// broker. Es SOLO LECTURA: no hay comandos
// remotos. MySystem::onMessageReceived() solo atiende la configuracion
// remota (remoteconfig.hpp) y el ping (remoteping.hpp), ninguno de los dos
// actua en caliente.
//
// Ya NO son constantes: las pisa la seccion `telemetry` del archivo de la SD
// (ver docs/config-schema.md seccion 9 y configloader.hpp). Los valores de
// aca son los DEFAULTS COMPILADOS, vigentes si no hay tarjeta o si la seccion
// falta.
//
// ORGANIZADO EN GRUPOS, y cada grupo va a SU PROPIO TOPIC. La alternativa era
// un unico mensaje con todo, y no entra: el payload tiene un tope duro (ver
// MaxPayloadLength) y un `result` con su descripcion ya se come la mitad.
// Ademas cada grupo tiene su propia cadencia natural -- las mediciones
// cambian todo el tiempo, los objetivos una vez por experimento -- y
// separarlos permite habilitar y espaciar cada uno por su cuenta sin afectar
// a los demas.
//
// CUIDADO: los nombres de clave y los topics son el contrato con el
// dashboard. Renombrar cualquiera desde el archivo obliga a actualizar
// tambien el dashboard, o ese valor deja de llegar sin ningun error visible
// de este lado.
namespace Topics {
  constexpr size_t MaxTopicLength = 64;      // 63 caracteres + terminador
  constexpr size_t MaxFieldNameLength = 16;  // 15 caracteres + terminador

  // Tope del JSON de un mensaje. El limite real lo pone PubSubClient, que
  // arma el paquete entero (topic + cabecera + payload) en un solo buffer;
  // BrokerManager::begin() lo agranda a BrokerBufferSize. Este numero es el
  // presupuesto del payload solo, con lugar de sobra para el topic mas largo.
  constexpr size_t MaxPayloadLength = 384;

  // Un grupo de telemetria: que publica, a donde y cada cuanto.
  //
  // `interval` en 0 significa POR EVENTO, no "deshabilitado": son los grupos
  // que no tienen sentido en un timer porque ocurren una vez (los objetivos
  // al arrancar, una alerta cuando se levanta, el resultado al cortar).
  // Para apagar un grupo esta `enabled`.
  // `retain` le pide al broker que guarde el ultimo mensaje del topic y se
  // lo entregue a cualquiera que se suscriba despues. Es lo que hace que un
  // dashboard que se abre a mitad de un experimento vea el estado actual en
  // vez de una pantalla vacia hasta la proxima tanda.
  //
  // Va en true en todo lo que es ESTADO (lo ultimo conocido sigue siendo
  // cierto) y en false en `alerts`, que es un FLUJO DE EVENTOS: una alerta
  // retenida se le entregaria a cada nuevo suscriptor como si acabara de
  // ocurrir, mucho despues de que el experimento termino.
  struct TelemetryGroup {
    bool enabled;
    char topic[MaxTopicLength];
    unsigned long interval;
    bool retain;
  };

  // Campos con nombre configurable. Son los que ya formaban el contrato
  // antes de la division en grupos; el resto de las claves que
  // aparecieron con los grupos nuevos son posicionales o estructurales
  // ("c1".."d4", "REASON", "SRC"...) y no se configuran una por una: son
  // parte de la forma del mensaje, no valores que el operador elija.
  struct TelemetryField {
    bool enabled;
    char name[MaxFieldNameLength];
  };

  /* ---------------- GRUPOS PERIODICOS ---------------- */

  // Campo magnetico y temperatura: lo que mide el experimento.
  inline TelemetryGroup Measures = { true, "biosoft/telemetry/measures", 30000, true };
  // Corriente y duty aplicado de cada bobina registrada.
  inline TelemetryGroup Coils    = { true, "biosoft/telemetry/coils", 30000, true };
  // Salud, avance, tiempos, estado de la maquina y enlace con el Mega.
  inline TelemetryGroup Status   = { true, "biosoft/telemetry/status", 30000, true };

  /* ---------------- GRUPOS POR EVENTO ---------------- */

  // Los objetivos con los que arranco el experimento, una vez al entrar en
  // Running. Sin esto el dashboard no puede distinguir un grupo tratado de
  // un grupo control, que para el experimento es la diferencia que importa.
  inline TelemetryGroup Targets  = { true, "biosoft/telemetry/targets", 0, true };
  // Una por cada flag_data que llega del Mega.
  inline TelemetryGroup Alerts   = { true, "biosoft/telemetry/alerts", 0, false };
  // Al terminar el experimento, con el motivo del corte.
  inline TelemetryGroup Result   = { true, "biosoft/telemetry/result", 0, true };

  /* ---------------- CAMPOS CONFIGURABLES ---------------- */

  // Grupo `measures`.
  inline TelemetryField MagneticField = { true, "CEM1" };
  inline TelemetryField Temperature   = { true, "TEMP1" };

  // Grupo `status`.
  inline TelemetryField Health        = { true, "ESTADO" };
  inline TelemetryField Progress      = { true, "PROGRESS" };
  inline TelemetryField ElapsedTime   = { true, "ELAPSED_TIME" };
};
