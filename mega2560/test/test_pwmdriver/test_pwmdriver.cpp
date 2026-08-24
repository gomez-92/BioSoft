#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/pwmdriver.hpp"

using namespace fakeit;

// =====================================================================
// PwmDriver no expone clamp()/outputToDuty() (son privados) -- se
// verifican indirectamente a traves de write(), inspeccionando el valor
// que efectivamente llega a analogWrite(). applyFrequency() (Timer5,
// TCCR5B) queda fuera de estos tests: es registro AVR real, no
// observable en host -- ver MOD-010 en Trello, sigue pendiente de
// medicion en banco con osciloscopio.
// =====================================================================

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
    When(Method(ArduinoFake(), pinMode)).AlwaysReturn();
    When(Method(ArduinoFake(), analogWrite)).AlwaysReturn();
}

void tearDown(void) {}

// =====================================================================
// Constructor
// =====================================================================

void test_constructor_configures_output_pin_and_starts_at_zero(void) {
    PwmDriver driver(9);

    Verify(Method(ArduinoFake(), pinMode).Using(9, OUTPUT)).Once();
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 0)).Once();
    TEST_ASSERT_FALSE(driver.isEnabled());
}

// =====================================================================
// write() sin enable() -- siempre analogWrite(pin, 0), sin importar output
// =====================================================================

void test_write_without_enable_always_forces_zero(void) {
    PwmDriver driver(9);

    driver.write(0.5f);
    driver.write(1.0f);
    driver.write(-3.0f);

    Verify(Method(ArduinoFake(), analogWrite).Using(9, 0)).Exactly(4); // 1 del constructor + 3 write()
}

// =====================================================================
// write() habilitado -- duty = clamp(output) x (2^resolution - 1), truncado
// =====================================================================

void test_write_enabled_applies_output_to_duty_default_resolution(void) {
    PwmDriver driver(9); // resolution=8 -> maxDuty=255
    driver.enable();

    driver.write(1.0f);
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 255)).Once();

    driver.write(0.0f);
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 0)).AtLeast(1);

    driver.write(0.5f); // 0.5 * 255 = 127.5 -> truncado a 127
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 127)).Once();
}

void test_write_enabled_respects_custom_resolution(void) {
    PwmDriver driver(9, 490, 10); // resolution=10 -> maxDuty=1023
    driver.enable();

    driver.write(0.5f); // 0.5 * 1023 = 511.5 -> truncado a 511
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 511)).Once();
}

void test_write_enabled_clamps_values_above_one(void) {
    PwmDriver driver(9);
    driver.enable();

    driver.write(2.0f);
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 255)).Once();
}

void test_write_enabled_clamps_values_below_zero(void) {
    PwmDriver driver(9);
    driver.enable();

    driver.write(-1.0f);
    Verify(Method(ArduinoFake(), analogWrite).Using(9, 0)).AtLeast(1);
}

// =====================================================================
// disable() -- fuerza analogWrite(pin, 0) ya mismo, ademas de isEnabled()=false
// =====================================================================

void test_disable_forces_zero_immediately_and_clears_enabled_flag(void) {
    PwmDriver driver(9);
    driver.enable();
    driver.write(1.0f); // deja algo distinto de 0 en la salida

    driver.disable();

    Verify(Method(ArduinoFake(), analogWrite).Using(9, 0)).AtLeast(1);
    TEST_ASSERT_FALSE(driver.isEnabled());
}

void test_write_after_disable_keeps_forcing_zero(void) {
    PwmDriver driver(9);
    driver.enable();
    driver.disable();

    driver.write(0.8f);

    Verify(Method(ArduinoFake(), analogWrite).Using(9, 0)).AtLeast(1);
}

// =====================================================================
// isEnabled() -- refleja enable()/disable()
// =====================================================================

void test_isEnabled_reflects_enable_and_disable(void) {
    PwmDriver driver(9);
    TEST_ASSERT_FALSE(driver.isEnabled());

    driver.enable();
    TEST_ASSERT_TRUE(driver.isEnabled());

    driver.disable();
    TEST_ASSERT_FALSE(driver.isEnabled());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_constructor_configures_output_pin_and_starts_at_zero);

    RUN_TEST(test_write_without_enable_always_forces_zero);

    RUN_TEST(test_write_enabled_applies_output_to_duty_default_resolution);
    RUN_TEST(test_write_enabled_respects_custom_resolution);
    RUN_TEST(test_write_enabled_clamps_values_above_one);
    RUN_TEST(test_write_enabled_clamps_values_below_zero);

    RUN_TEST(test_disable_forces_zero_immediately_and_clears_enabled_flag);
    RUN_TEST(test_write_after_disable_keeps_forcing_zero);

    RUN_TEST(test_isEnabled_reflects_enable_and_disable);

    return UNITY_END();
}
