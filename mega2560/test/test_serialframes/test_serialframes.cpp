#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/seriallink.hpp"
#include "../../src/commands.hpp"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// Presupuesto de tamaño de los frames de configuracion.
//
// El transporte tiene UN solo punto donde recorta, y lo hace en silencio:
// SerialLink::_sendFrame() arma {"command":...,"params":{...}} y lo
// serializa a un char[MAX_JSON_SIZE]. serializeJson() trunca si no entra y
// el CRC se calcula sobre el texto YA recortado -- asi que el frame llega
// al otro lado con CRC valido y menos claves, sin ningun error visible: el
// receptor no puede distinguirlo de un frame que nunca trajo esas claves,
// y se queda con sus defaults creyendo que la config no las traia.
//
// OJO con los numeros de seriallink.hpp: TxMessage::params es un
// StaticJsonDocument<128> y el envelope un StaticJsonDocument<256>, pero en
// ArduinoJson 7 esa clase es solo un JsonDocument elastico cuyo capacity()
// devuelve N sin limitar nada (ver compatibility.hpp de la libreria). El
// unico tope real es el buffer de serializacion.
//
// Estos tests fijan que cada frame que MySystem manda entra entero, con la
// misma logica que test_pwmdriver usa para la tabla de prescalers: lo que
// se rompe en silencio se pinea en un test.
// =====================================================================

namespace {

    // Reproduce el paso de _sendFrame() que puede recortar.
    size_t frameLength(const char* command, JsonDocument& params) {
        JsonDocument envelope;
        envelope["command"] = command;
        envelope["params"] = params;

        char json[MAX_JSON_SIZE];
        return serializeJson(envelope, json, sizeof(json));
    }

    // true si el frame viaja entero: se serializa sin recorte y, parseado
    // del otro lado (_processFrame), devuelve exactamente los mismos params.
    bool survivesTransport(const char* command, JsonDocument& params) {
        JsonDocument envelope;
        envelope["command"] = command;
        envelope["params"] = params;

        char json[MAX_JSON_SIZE];
        size_t len = serializeJson(envelope, json, sizeof(json));

        // serializeJson escribe como mucho sizeof-1 chars mas el terminador:
        // llegar a ese tope es la señal de que trunco.
        if (len >= MAX_JSON_SIZE - 1) return false;

        JsonDocument received;
        if (deserializeJson(received, json, len)) return false;

        char sent[MAX_JSON_SIZE];
        char back[MAX_JSON_SIZE];
        serializeJson(params, sent, sizeof(sent));
        serializeJson(received["params"], back, sizeof(back));

        return strcmp(sent, back) == 0;
    }

    void buildIntervals(JsonDocument& doc, unsigned long value) {
        doc["ping"] = value;
        doc["sendState"] = value;
        doc["measureTemperature"] = value;
        doc["measureCurrent"] = value;
        doc["measureMagneticField"] = value;
        doc["updateProgress"] = value;
        doc["sendFlags"] = value;
        doc["sendResult"] = value;
        doc["settlingTime"] = value;
    }

    void buildRule(JsonObject rule) {
        rule["threshold"] = 16;
        rule["cooldown"] = 16;
        rule["maxEvents"] = 10;
    }
}

// =====================================================================
// config_intervals -- el frame mas cargado de los que ya estan en uso
// (9 claves con los nombres largos del esquema)
// =====================================================================

void test_config_intervals_fits_with_default_values(void) {
    JsonDocument doc;
    buildIntervals(doc, 25000);   // el mayor de los defaults (sendFlags)

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigIntervals, doc));
}

// Un intervalo puede configurarse mucho mas largo que el default (una hora
// son 7 digitos). Si el margen no aguanta eso, hay que partir el frame.
void test_config_intervals_fits_with_seven_digit_values(void) {
    JsonDocument doc;
    buildIntervals(doc, 3600000);

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigIntervals, doc));
}

// =====================================================================
// config_control / config_coil
// =====================================================================

void test_config_control_fits(void) {
    JsonDocument doc;
    doc["kp"] = 0.123456f;
    doc["maxStep"] = 0.123456f;
    doc["deadBand"] = 0.123456f;
    doc["enabled"] = true;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigControl, doc));
}

void test_config_coil_fits_with_max_length_name(void) {
    JsonDocument doc;
    doc["name"] = "123456789012345";   // tope del esquema: 15 caracteres
    doc["enabled"] = true;
    doc["calibrationFactor"] = 1.234567f;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigCoil, doc));
}

// config_detector: el interruptor general del Detector. Es chico, pero es
// el que decide si algo puede cortar el experimento: si se truncara,
// llegaria sin `enabled` y el Mega se quedaria con el valor anterior.
void test_config_detector_fits(void) {
    JsonDocument doc;
    doc["enabled"] = false;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigDetector, doc));
}

// config_scenario: la senal sintetica de una fuente, con las 9 claves y
// valores de 7 cifras significativas. Si se truncara, la fuente se quedaria
// con parte del escenario anterior -- un "temperatura alta" sin su salto.
void test_config_scenario_fits_with_every_key(void) {
    JsonDocument doc;
    doc["name"] = "123456789012345";
    doc["b"] = -1.234567f;
    doc["n"] = 1.234567f;
    doc["r"] = -1.234567f;
    doc["sa"] = 12345.67f;
    doc["s"] = -1.234567f;
    doc["oa"] = 1.234567f;
    doc["op"] = 12345.67f;
    doc["da"] = 12345.67f;
    doc["d"] = 0.1234567f;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigScenario, doc));
}

// =====================================================================
// config_source / config_rule -- la fragmentacion de detector.sources
// =====================================================================

void test_config_source_header_fits(void) {
    JsonDocument doc;
    doc["name"] = "123456789012345";
    doc["enabled"] = true;
    doc["sensor"] = "mlx90393";
    doc["bufferSize"] = 32;
    doc["criticalMultiplier"] = 1.234567f;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigSource, doc));
}

void test_config_rule_fits(void) {
    JsonDocument doc;
    doc["source"] = "123456789012345";
    doc["rule"] = "frequency";
    doc["threshold"] = 32;
    doc["cooldown"] = 64;
    doc["maxEvents"] = 10;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigRule, doc));
}

// =====================================================================
// config_current — el frame mas cargado del protocolo (9 claves)
// =====================================================================

void test_config_current_fits(void) {
    JsonDocument doc;
    doc["name"] = "123456789012345";   // tope del esquema: 15 caracteres
    doc["enabled"] = true;
    doc["address"] = 73;               // 0x49, el segundo modulo
    doc["channel"] = 1;
    doc["ratedCurrent"] = 20.123456f;
    doc["ratedVoltage"] = 1.123456f;
    doc["calibration"] = 0.775123f;
    doc["sampleRate"] = 860;
    doc["integrationTimeMs"] = 200;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ConfigCurrent, doc));
}

// =====================================================================
// start -- no es un frame de configuracion, pero comparte el mismo buffer
// y es el unico que crecio despues de que se fijaron los presupuestos: al
// sumarle el modo de experimento paso de 8 claves a 9. Si algun dia no
// entra, el sintoma NO seria un error: el Mega recibiria un start con CRC
// valido y sin `mode`, y correria campo X creyendo que la pantalla nunca
// pidio otra cosa -- que es exactamente lo que hace con una pantalla vieja.
// Un grupo control expuesto a campo real, sin una sola linea de log que lo
// delate.
// =====================================================================

void test_start_fits_with_field_mode(void) {
    JsonDocument doc;
    doc["cem"] = 2.123456f;
    doc["freq"] = 50;
    doc["dur"] = 7200000UL;
    doc["tol"] = 10;
    doc["tnmin"] = 28.123456f;
    doc["tnmax"] = 42.123456f;
    doc["tcmin"] = 23.123456f;
    doc["tcmax"] = 47.123456f;
    doc["mode"] = "null";   // el mas largo de los dos valores posibles

    TEST_ASSERT_TRUE(survivesTransport(Commands::Start, doc));
}

// result_data en su caso mas largo: corte por limite, con la descripcion
// completa que arma Engine::onFlag MAS los campos crudos que el ESP32 usa
// para redactar el texto de la pantalla. Si dejara de entrar, el frame
// llegaria truncado con CRC valido y la pantalla Resultado se quedaria sin
// fuente ni limite -- mostrando el motivo de otra corrida.
void test_resultdata_fits_with_critical_fields(void) {
    JsonDocument doc;
    doc["reason"] = "critical";
    doc["description"] = "SCT013-1: limite de alertas frequency alcanzado (250/250)";
    doc["source"] = "SCT013-1";
    doc["type"] = "frequency";   // el mas largo de los tres tipos de regla
    doc["count"] = 250;
    doc["limit"] = 250;

    TEST_ASSERT_TRUE(survivesTransport(Commands::ResultData, doc));
}

// coil_data lleva duty Y corriente de las 4 bobinas en un solo frame. El
// caso peor es el de arriba: 8 claves con el valor mas largo que el Engine
// puede producir despues de redondear (100.0 de duty, corrientes de dos
// decimales). Si dejara de entrar, serializeJson truncaria en silencio y la
// pantalla En curso mostraria menos bobinas de las que hay -- con CRC
// valido, indistinguible de un Mega que registro menos canales.
void test_coildata_fits_with_four_coils(void) {
    JsonDocument doc;
    for (uint8_t i = 1; i <= 4; i++) {
        char key[4];
        snprintf(key, sizeof(key), "d%u", i);
        doc[key] = 100.0f;
        snprintf(key, sizeof(key), "c%u", i);
        doc[key] = 88.88f;
    }

    TEST_ASSERT_TRUE(survivesTransport(Commands::CoilData, doc));
}

// Este es el test que justifica la fragmentacion: una fuente entera con la
// forma del esquema (cabecera + las 3 reglas anidadas) NO entra en un frame.
// docs/config-schema.md 10.1 asumia que si; por eso detector.sources viaja
// como 1 config_source + 3 config_rule y no como un unico frame. Si alguien
// intenta volver a unirlos, este test lo frena.
void test_whole_source_in_one_frame_does_not_fit(void) {
    JsonDocument doc;
    doc["name"] = "CEM1";
    doc["enabled"] = true;
    doc["sensor"] = "mlx90393";
    doc["bufferSize"] = 32;
    doc["criticalMultiplier"] = 1.25f;
    buildRule(doc["critical"].to<JsonObject>());
    buildRule(doc["streak"].to<JsonObject>());
    buildRule(doc["frequency"].to<JsonObject>());

    TEST_ASSERT_FALSE(survivesTransport(Commands::ConfigSource, doc));
}

// El comando mas largo del protocolo entra en MAX_COMMAND_SIZE: strncpy en
// sendCommand() trunca sin avisar, y un comando truncado no matchea ningun
// strcmp del otro lado -- el frame se descarta entero, en silencio.
void test_every_command_name_fits_in_the_command_field(void) {
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigIntervals) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigControl) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigCoil) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigSource) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigRule) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigCurrent) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigDetector) + 1);
    TEST_ASSERT_LESS_THAN(MAX_COMMAND_SIZE, strlen(Commands::ConfigScenario) + 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_config_intervals_fits_with_default_values);
    RUN_TEST(test_config_intervals_fits_with_seven_digit_values);

    RUN_TEST(test_config_control_fits);
    RUN_TEST(test_config_coil_fits_with_max_length_name);
    RUN_TEST(test_config_detector_fits);
    RUN_TEST(test_config_scenario_fits_with_every_key);

    RUN_TEST(test_config_current_fits);

    RUN_TEST(test_config_source_header_fits);
    RUN_TEST(test_config_rule_fits);
    RUN_TEST(test_whole_source_in_one_frame_does_not_fit);

    RUN_TEST(test_start_fits_with_field_mode);

    RUN_TEST(test_resultdata_fits_with_critical_fields);
    RUN_TEST(test_coildata_fits_with_four_coils);

    RUN_TEST(test_every_command_name_fits_in_the_command_field);

    return UNITY_END();
}
