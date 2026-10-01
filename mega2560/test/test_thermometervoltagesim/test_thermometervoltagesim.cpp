#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/thermometervoltagesim.hpp"

using namespace fakeit;

// TEMP1 simulado: lo que permite provocar en banco el corte por temperatura
// critica sin calentar nada. Si el mapeo se corre, los umbrales de los menus
// (20-50 C) dejan de corresponder con la perilla y el corte que se quiere
// provocar no llega -- o llega a destiempo.

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

void test_begin_marks_valid_without_reading_the_pin(void) {
    ThermometerVoltageSim sensor("TEMP1", A1);

    sensor.begin();

    TEST_ASSERT_TRUE(sensor.isValid());
    VerifyNoOtherInvocations(Method(ArduinoFake(Function), analogRead));
}

void test_update_maps_zero_raw_to_zero_degrees(void) {
    ThermometerVoltageSim sensor("TEMP1", A1, 50.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(0);

    sensor.update();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor.getTemperature());
}

void test_update_maps_max_raw_to_max_temperature(void) {
    ThermometerVoltageSim sensor("TEMP1", A1, 50.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(1023);

    sensor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f, sensor.getTemperature());
}

// 2.5 V -> 25 C: con 0-5 V -> 0-50 C cada volt son 10 grados, que es lo que
// hace facil ubicar en banco las zonas de los menus (30~40 normal, 25~45
// critico de fabrica) con un multimetro en la entrada.
void test_update_maps_midpoint_proportionally(void) {
    ThermometerVoltageSim sensor("TEMP1", A1, 50.0f, 5.0f);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(512);

    sensor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.0f, sensor.getTemperature());
}

void test_update_reads_the_configured_analog_pin(void) {
    ThermometerVoltageSim sensor("TEMP1", A1);
    sensor.begin();
    When(Method(ArduinoFake(Function), analogRead)).Return(0);

    sensor.update();

    Verify(Method(ArduinoFake(Function), analogRead).Using(A1)).Once();
}

void test_getName_returns_the_configured_name(void) {
    ThermometerVoltageSim sensor("TEMP1", A1);
    TEST_ASSERT_EQUAL_STRING("TEMP1", sensor.getName());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_begin_marks_valid_without_reading_the_pin);
    RUN_TEST(test_update_maps_zero_raw_to_zero_degrees);
    RUN_TEST(test_update_maps_max_raw_to_max_temperature);
    RUN_TEST(test_update_maps_midpoint_proportionally);
    RUN_TEST(test_update_reads_the_configured_analog_pin);
    RUN_TEST(test_getName_returns_the_configured_name);

    return UNITY_END();
}
