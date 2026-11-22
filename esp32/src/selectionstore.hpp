#pragma once

#include <ArduinoJson.h>

#include "sdstorage.hpp"
#include "systemdata.hpp"
#include "configurationoptions.hpp"
#include "log.hpp"

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
      LOG_W(CONFIG, "seleccion %s=%u fuera del menu (%u opciones): se ignora", name, parsed, count);
      return;
    }
    target = parsed;
  }

  inline bool load(SdStorage& storage, ConfigurationData& configuration) {
    if (!storage.isReady()) {
      return false;
    }

    if (!storage.exists(SelectionPath)) {
      LOG_D(CONFIG, "no hay seleccion guardada: queda la de por defecto");
      return false;
    }

    char buffer[MaxFileSize];
    size_t length = 0;

    if (!storage.readFile(SelectionPath, buffer, MaxFileSize, &length)) {
      LOG_W(CONFIG, "seleccion guardada ilegible: queda la de por defecto");
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buffer, length);
    if (error) {
      LOG_W(CONFIG, "seleccion guardada con JSON invalido (%s): queda la de por defecto", error.c_str());
      return false;
    }

    // Mismo criterio que ConfigLoader: una version que no se conoce se
    // descarta ENTERA, no a medias. Un archivo de otra version puede tener
    // los mismos nombres de clave con otro significado.
    uint8_t version = doc["schemaVersion"] | 0;
    if (version != SchemaVersion) {
      LOG_W(CONFIG, "seleccion guardada schemaVersion=%u no soportada: queda la de por defecto", version);
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

    LOG_I(CONFIG, "seleccion guardada del operador aplicada");
    return true;
  }

  inline bool save(SdStorage& storage, const ConfigurationData& configuration) {
    if (!storage.isReady()) {
      LOG_W(CONFIG, "sin tarjeta SD: la seleccion vale para esta sesion pero no se guarda");
      return false;
    }

    // Primera vez sobre una tarjeta que nunca tuvo config.json: el
    // directorio tampoco existe, y SD.open(FILE_WRITE) no lo crea. Sin
    // esto el primer GUARDAR fallaria en una tarjeta virgen y recien se
    // notaria al reiniciar y ver la seleccion perdida.
    if (!storage.ensureDir(SelectionDir)) {
      LOG_E(CONFIG, "no se pudo crear %s: la seleccion no se guarda", SelectionDir);
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
      LOG_E(CONFIG, "la seleccion no entra en %u B: no se guarda", (unsigned)sizeof(buffer));
      return false;
    }

    if (!storage.writeFile(SelectionPath, buffer)) {
      LOG_E(CONFIG, "no se pudo escribir %s", SelectionPath);
      return false;
    }

    LOG_D(CONFIG, "seleccion guardada en la SD");
    return true;
  }

};
