#pragma once

#include <ArduinoJson.h>

#include "sdstorage.hpp"
#include "systemdata.hpp"
#include "configurationoptions.hpp"
#include "debugconfig.hpp"

// Interruptor de logs de debug de ESTE modulo (ver debugconfig.hpp para
// el interruptor maestro).
constexpr bool DEBUG_SELECTIONSTORE = true;

// Persistencia de lo que el operador ELIGIO en la pantalla Configuraciones
// (un indice por menu), para que sobreviva a un reinicio.
//
// Archivo aparte de /biosoft/config.json a proposito: config.json lo escribe
// el generador (tools/generador-config.html) y define CUALES son las opciones
// de cada menu; esto de aca lo escribe el firmware y dice CUAL de esas
// opciones esta elegida. Mezclarlos haria que guardar desde la pantalla
// pisara el archivo del generador.
//
// Sin tarjeta SD todo sigue funcionando igual: load() no encuentra nada y
// quedan los defaults compilados (indice 0 de cada menu), save() devuelve
// false y lo deja en el log. Ni uno ni otro son errores que haya que
// atender -- son un arranque perfectamente valido, el mismo que habia
// antes de que existiera este archivo.
namespace SelectionStore {

  constexpr const char* SelectionPath = "/biosoft/seleccion.json";
  constexpr const char* SelectionDir = "/biosoft";
  constexpr uint8_t SchemaVersion = 1;

  // Un archivo de 7 enteros chicos no pasa de ~200 bytes; 512 deja lugar
  // para que alguien lo abra y lo indente a mano sin que deje de entrar.
  // Es un buffer local (no static como el de ConfigLoader): medio KB en el
  // stack de loopTask no es problema, 8 KB si lo serian.
  constexpr size_t MaxFileSize = 512;

  // Un indice guardado solo sirve si sigue existiendo en el menu de HOY: el
  // config.json pudo haber cambiado entre un arranque y el siguiente, y un
  // menu que se achico dejaria el indice apuntando a una opcion que ya no
  // esta. Fuera de rango se descarta esa clave y queda el default compilado,
  // que es el mismo criterio de ConfigLoader: nunca dejar el sistema en un
  // valor que nadie eligio.
  inline void readIndex(JsonVariantConst value, uint8_t count, uint8_t& target, const char* name) {
    if (!value.is<uint8_t>()) return;

    uint8_t parsed = value.as<uint8_t>();
    if (parsed >= count) {
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, F("[SELECCION] "));
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, name);
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, F(" fuera de rango ("));
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, parsed);
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, F(" >= "));
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, count);
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("): se ignora"));
      return;
    }
    target = parsed;
  }

  inline bool load(SdStorage& storage, ConfigurationData& configuration) {
    if (!storage.isReady()) {
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] sin tarjeta SD: queda la seleccion por defecto"));
      return false;
    }

    if (!storage.exists(SelectionPath)) {
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] no hay seleccion guardada: queda la de por defecto"));
      return false;
    }

    char buffer[MaxFileSize];
    size_t length = 0;

    if (!storage.readFile(SelectionPath, buffer, MaxFileSize, &length)) {
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] no se pudo leer el archivo: queda la de por defecto"));
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buffer, length);
    if (error) {
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, F("[SELECCION] JSON invalido: "));
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, error.c_str());
      return false;
    }

    // Mismo criterio que ConfigLoader: una version que no se conoce se
    // descarta ENTERA, no a medias. Un archivo de otra version puede tener
    // los mismos nombres de clave con otro significado.
    uint8_t version = doc["schemaVersion"] | 0;
    if (version != SchemaVersion) {
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, F("[SELECCION] schemaVersion no soportada: "));
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, version);
      return false;
    }

    readIndex(doc["fieldMode"], ConfigurationOptions::countFieldMode,
              configuration.fieldModeOption, "fieldMode");
    readIndex(doc["fieldIntensity"], ConfigurationOptions::countFieldIntensity,
              configuration.targetFieldIntensityOption, "fieldIntensity");
    readIndex(doc["frequency"], ConfigurationOptions::countFrequency,
              configuration.targetFieldFrequencyOption, "frequency");
    readIndex(doc["duration"], ConfigurationOptions::countDuration,
              configuration.targetDurationOption, "duration");
    readIndex(doc["tolFieldIntensity"], ConfigurationOptions::countTolFieldIntensity,
              configuration.fieldIntensityToleranceOption, "tolFieldIntensity");
    readIndex(doc["rangeNormalTemperature"], ConfigurationOptions::countRangeNormalTemperature,
              configuration.normalTemperatureRangeOption, "rangeNormalTemperature");
    readIndex(doc["rangeCriticalTemperature"], ConfigurationOptions::countRangeCriticalTemperature,
              configuration.criticalTemperatureRangeOption, "rangeCriticalTemperature");

    DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] seleccion guardada aplicada"));
    return true;
  }

  inline bool save(SdStorage& storage, const ConfigurationData& configuration) {
    if (!storage.isReady()) {
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] sin tarjeta SD: no se persiste (la sesion sigue igual)"));
      return false;
    }

    // Primera vez sobre una tarjeta que nunca tuvo config.json: el
    // directorio tampoco existe, y SD.open(FILE_WRITE) no lo crea. Sin
    // esto el primer GUARDAR fallaria en una tarjeta virgen y recien se
    // notaria al reiniciar y ver la seleccion perdida.
    if (!storage.ensureDir(SelectionDir)) {
      DEBUG_PRINT(DEBUG_SELECTIONSTORE, F("[SELECCION] no se pudo crear "));
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, SelectionDir);
      return false;
    }

    JsonDocument doc;
    doc["schemaVersion"] = SchemaVersion;
    doc["fieldMode"] = configuration.fieldModeOption;
    doc["fieldIntensity"] = configuration.targetFieldIntensityOption;
    doc["frequency"] = configuration.targetFieldFrequencyOption;
    doc["duration"] = configuration.targetDurationOption;
    doc["tolFieldIntensity"] = configuration.fieldIntensityToleranceOption;
    doc["rangeNormalTemperature"] = configuration.normalTemperatureRangeOption;
    doc["rangeCriticalTemperature"] = configuration.criticalTemperatureRangeOption;

    char buffer[MaxFileSize];
    // serializeJson trunca en silencio si no entra, como en
    // SerialLink::_sendFrame: un archivo truncado seria JSON invalido y el
    // proximo arranque lo descartaria sin decir por que. Por eso se mira el
    // valor de retorno y no se escribe nada si no entro entero.
    size_t written = serializeJson(doc, buffer, sizeof(buffer));
    if (written == 0 || written >= sizeof(buffer) - 1) {
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] la seleccion no entra en el buffer: no se escribe"));
      return false;
    }

    if (!storage.writeFile(SelectionPath, buffer)) {
      DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] no se pudo escribir el archivo"));
      return false;
    }

    DEBUG_PRINTLN(DEBUG_SELECTIONSTORE, F("[SELECCION] seleccion guardada en la SD"));
    return true;
  }

};
