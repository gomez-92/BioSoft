#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/magnetometervoltagesim.hpp"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// begin / isValid
// =====================================================================

void test_begin_marks_valid_without_reading_the_pin(void) {
    MagnetometerVoltageSim sensor("CEM1", A0);

    sensor.begin();

    TEST_ASSERT_TRUE(sensor.isValid());
    VerifyNoOtherInvocations(Method(ArduinoFake(Function), analogRead));
}

void test_isValid_stays_true_after_update_even_with_floating_pin_reading_zero(void) {
    // Documenta el gap de diseno de la tarjeta MOD-007: a diferencia de
    // MOD-005/006, el driver simulado no valida rango -- un A0 flotante en
    // 0 se toma como una lectura igual de valida que cualquier otra.
    MagnetometerVoltageSim sensor("CEM1", A0);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(0);

    sensor.update();

    TEST_ASSERT_TRUE(sensor.isValid());
}

// =====================================================================
// update -- mapeo lineal raw -> voltios -> mT
// =====================================================================

void test_update_maps_zero_raw_to_zero_field(void) {
    MagnetometerVoltageSim sensor("CEM1", A0, 3.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(0);

    sensor.update();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor.getMagneticField());
}

void test_update_maps_max_raw_to_max_field(void) {
    MagnetometerVoltageSim sensor("CEM1", A0, 3.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(1023);

    sensor.update();

    TEST_ASSERT_EQUAL_FLOAT(3.0f, sensor.getMagneticField());
}

void test_update_maps_midpoint_raw_proportionally(void) {
    MagnetometerVoltageSim sensor("CEM1", A0, 3.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(511);

    sensor.update();

    // referenceVoltage se cancela en el calculo (voltage/referenceVoltage),
    // asi que el resultado es simplemente (raw/1023) * maxFieldMt.
    float expected = (511.0f / 1023.0f) * 3.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, expected, sensor.getMagneticField());
}

void test_update_respects_custom_maxFieldMt(void) {
    MagnetometerVoltageSim sensor("CEM1", A0, 10.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(1023);

    sensor.update();

    TEST_ASSERT_EQUAL_FLOAT(10.0f, sensor.getMagneticField());
}

void test_update_reads_the_configured_analog_pin(void) {
    MagnetometerVoltageSim sensor("CEM1", A3, 3.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(0);

    sensor.update();

    Verify(Method(ArduinoFake(Function), analogRead).Using(A3)).Once();
}

// =====================================================================
// getName
// =====================================================================

void test_getName_returns_the_configured_name(void) {
    MagnetometerVoltageSim sensor("CEM1", A0);
    TEST_ASSERT_EQUAL_STRING("CEM1", sensor.getName());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_begin_marks_valid_without_reading_the_pin);
    RUN_TEST(test_isValid_stays_true_after_update_even_with_floating_pin_reading_zero);

    RUN_TEST(test_update_maps_zero_raw_to_zero_field);
    RUN_TEST(test_update_maps_max_raw_to_max_field);
    RUN_TEST(test_update_maps_midpoint_raw_proportionally);
    RUN_TEST(test_update_respects_custom_maxFieldMt);
    RUN_TEST(test_update_reads_the_configured_analog_pin);

    RUN_TEST(test_getName_returns_the_configured_name);

    return UNITY_END();
}
