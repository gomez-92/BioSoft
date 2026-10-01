#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/detectorconfigbuilder.hpp"
#include "../../src/safetymargins.hpp"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// Validacion (docs/config-schema.md, seccion 7)
//
// El criterio del contrato es RECHAZAR ENTERO lo que no cumple, no
// recortarlo al extremo mas cercano: una regla a medio aplicar corta (o
// deja de cortar) un experimento con numeros que nadie eligio.
// =====================================================================

void test_bufferSize_accepts_the_documented_range(void) {
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidBufferSize(8));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidBufferSize(32));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidBufferSize(20));
}

void test_bufferSize_rejects_outside_the_range(void) {
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidBufferSize(7));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidBufferSize(33));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidBufferSize(0));
}

// maxMissedSamples (tarjeta 25): 1 a 20. Con 0 una fuente nunca cortaria
// por silencio; con mas de 20, TEMP1 quedaria casi dos minutos ciega.
void test_maxMissedSamples_accepts_1_to_20_only(void) {
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidMaxMissedSamples(1));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidMaxMissedSamples(3));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidMaxMissedSamples(20));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidMaxMissedSamples(0));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidMaxMissedSamples(21));
}

void test_criticalMultiplier_accepts_the_documented_range(void) {
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidCriticalMultiplier(1.0f));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidCriticalMultiplier(1.25f));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidCriticalMultiplier(3.0f));
}

// Menor que 1.0 pondria el rango critico DENTRO del normal: una muestra
// seria critica antes de estar siquiera fuera de lo normal, invirtiendo la
// relacion que Source::addSample asume entre los dos rangos.
void test_criticalMultiplier_rejects_outside_the_range(void) {
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidCriticalMultiplier(0.99f));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidCriticalMultiplier(3.01f));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidCriticalMultiplier(0.0f));
}

namespace {
    Rule makeRule(uint16_t threshold, uint16_t cooldown, uint16_t maxEvents) {
        Rule rule;
        rule.threshold = threshold;
        rule.cooldown = cooldown;
        rule.maxEvents = maxEvents;
        return rule;
    }
}

void test_rule_accepts_valid_values(void) {
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(makeRule(1, 0, 1), 32));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(makeRule(16, 16, 3), 32));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(makeRule(32, 64, 10), 32));
}

// threshold 0 desactiva la regla de hecho (Rule::evaluate devuelve false
// con threshold==0), asi que se rechaza en vez de aceptar una regla que
// parece configurada pero nunca dispara.
void test_rule_rejects_threshold_out_of_bounds(void) {
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidRule(makeRule(0, 1, 2), 32));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidRule(makeRule(33, 1, 2), 32));
}

// El threshold se valida contra el bufferSize de SU fuente: 16 es valido
// con ventana de 32 y no lo es con ventana de 8, porque nunca podria
// juntar 16 muestras.
void test_rule_threshold_is_validated_against_its_own_buffer_size(void) {
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(makeRule(16, 1, 2), 32));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidRule(makeRule(16, 1, 2), 8));
}

void test_rule_rejects_cooldown_and_maxEvents_out_of_bounds(void) {
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidRule(makeRule(3, 65, 2), 32));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidRule(makeRule(3, 1, 0), 32));
    TEST_ASSERT_FALSE(DetectorConfigBuilder::isValidRule(makeRule(3, 1, 11), 32));
}

// =====================================================================
// defaultConfig()
// =====================================================================

void test_default_config_matches_the_compiled_placeholders(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();

    TEST_ASSERT_EQUAL_UINT16(3, config.critical.threshold);
    TEST_ASSERT_EQUAL_UINT16(1, config.critical.cooldown);
    TEST_ASSERT_EQUAL_UINT16(2, config.critical.maxEvents);

    TEST_ASSERT_EQUAL_UINT16(5, config.streak.threshold);
    TEST_ASSERT_EQUAL_UINT16(4, config.streak.cooldown);
    TEST_ASSERT_EQUAL_UINT16(5, config.streak.maxEvents);

    TEST_ASSERT_EQUAL_UINT16(16, config.frequency.threshold);
    TEST_ASSERT_EQUAL_UINT16(16, config.frequency.cooldown);
    TEST_ASSERT_EQUAL_UINT16(3, config.frequency.maxEvents);
}

// Los defaults compilados tienen que pasar su propia validacion: si no, una
// SD ausente dejaria al Detector con reglas que el propio firmware
// considera invalidas.
void test_default_config_passes_its_own_validation(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();

    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidBufferSize(config.bufferSize));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(config.critical, config.bufferSize));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(config.streak, config.bufferSize));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidRule(config.frequency, config.bufferSize));
    TEST_ASSERT_TRUE(DetectorConfigBuilder::isValidCriticalMultiplier(SafetyMargins::CemCriticalMultiplier));
}

void test_default_config_leaves_ranges_at_zero(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.normalMin);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.normalMax);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.criticalMin);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, config.criticalMax);
}

// =====================================================================
// applyCemRanges / applyTempRanges
// =====================================================================

void test_applyCemRanges_derives_normal_and_critical_from_target(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();

    DetectorConfigBuilder::applyCemRanges(config, 2.0f, 5, 1.25f);

    // normal: +-5% de 2.0 -> [1.90, 2.10]
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.90f, config.normalMin);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.10f, config.normalMax);
    // critico: +-6.25% (5% * 1.25) de 2.0 -> [1.875, 2.125]
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.875f, config.criticalMin);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.125f, config.criticalMax);
}

// La banda critica siempre tiene que envolver a la normal: Source trata
// "critico" como un subconjunto mas severo de "fuera de lo normal", y si se
// invirtieran, una muestra podria ser critica sin ser anormal.
void test_applyCemRanges_keeps_critical_band_wider_than_normal(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();

    DetectorConfigBuilder::applyCemRanges(config, 1.5f, 10, 2.0f);

    TEST_ASSERT_TRUE(config.criticalMin < config.normalMin);
    TEST_ASSERT_TRUE(config.criticalMax > config.normalMax);
}

void test_applyTempRanges_copies_the_operator_values_verbatim(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();

    DetectorConfigBuilder::applyTempRanges(config, 30.0f, 40.0f, 25.0f, 45.0f);

    TEST_ASSERT_EQUAL_FLOAT(30.0f, config.normalMin);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, config.normalMax);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, config.criticalMin);
    TEST_ASSERT_EQUAL_FLOAT(45.0f, config.criticalMax);
}

// La razon de separar plantilla y rangos: cada start reescribe los rangos
// sobre la plantilla, y las reglas que vinieron de la SD tienen que
// sobrevivir. Si esto se rompe, cada experimento arrancaria con los
// placeholders compilados y la configuracion del archivo no serviria de
// nada -- sin ningun sintoma visible.
void test_applying_ranges_does_not_touch_the_rules(void) {
    SourceConfig config = DetectorConfigBuilder::defaultConfig();
    config.bufferSize = 16;
    config.critical.threshold = 9;
    config.streak.cooldown = 7;
    config.frequency.maxEvents = 4;

    DetectorConfigBuilder::applyCemRanges(config, 2.0f, 5, 1.25f);
    DetectorConfigBuilder::applyTempRanges(config, 30.0f, 40.0f, 25.0f, 45.0f);

    TEST_ASSERT_EQUAL_size_t(16, config.bufferSize);
    TEST_ASSERT_EQUAL_UINT16(9, config.critical.threshold);
    TEST_ASSERT_EQUAL_UINT16(7, config.streak.cooldown);
    TEST_ASSERT_EQUAL_UINT16(4, config.frequency.maxEvents);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_bufferSize_accepts_the_documented_range);
    RUN_TEST(test_bufferSize_rejects_outside_the_range);
    RUN_TEST(test_criticalMultiplier_accepts_the_documented_range);
    RUN_TEST(test_maxMissedSamples_accepts_1_to_20_only);
    RUN_TEST(test_criticalMultiplier_rejects_outside_the_range);

    RUN_TEST(test_rule_accepts_valid_values);
    RUN_TEST(test_rule_rejects_threshold_out_of_bounds);
    RUN_TEST(test_rule_threshold_is_validated_against_its_own_buffer_size);
    RUN_TEST(test_rule_rejects_cooldown_and_maxEvents_out_of_bounds);

    RUN_TEST(test_default_config_matches_the_compiled_placeholders);
    RUN_TEST(test_default_config_passes_its_own_validation);
    RUN_TEST(test_default_config_leaves_ranges_at_zero);

    RUN_TEST(test_applyCemRanges_derives_normal_and_critical_from_target);
    RUN_TEST(test_applyCemRanges_keeps_critical_band_wider_than_normal);
    RUN_TEST(test_applyTempRanges_copies_the_operator_values_verbatim);
    RUN_TEST(test_applying_ranges_does_not_touch_the_rules);

    return UNITY_END();
}
