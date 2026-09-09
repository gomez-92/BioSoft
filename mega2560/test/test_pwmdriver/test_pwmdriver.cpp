#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/pwmdriver.hpp"

using namespace fakeit;

// =====================================================================
// PwmDriver no expone clamp()/outputToDuty() (son privados) -- se
// verifican indirectamente a traves de write(), inspeccionando el valor
// que efectivamente llega a analogWrite(). De applyFrequency() se cubren
// las dos decisiones puras que la preceden (PwmTiming, al final del
// archivo); la escritura del TCCRnB en si sigue sin ser observable en host
// -- ver MOD-010 en Trello, pendiente de medicion en banco con
// osciloscopio.
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

// =====================================================================
// PwmTiming -- seleccion de prescaler y mapeo pin->timer
//
// La escritura del registro TCCRnB en si no es observable en host, pero
// las dos decisiones que la preceden son funciones puras y si lo son. Los
// timers 1/3/4/5 corren en PWM phase correct de 8 bits (lo que deja el
// core de Arduino en wiring.c), donde f = 16MHz / (2 * N * 256), o sea:
//   N=1 -> 31250 Hz | N=8 -> 3906 Hz | N=64 -> 488 Hz
//   N=256 -> 122 Hz | N=1024 -> 30.5 Hz
// =====================================================================

namespace {
    constexpr uint8_t BitsDiv1    = 0b001;
    constexpr uint8_t BitsDiv8    = 0b010;
    constexpr uint8_t BitsDiv64   = 0b011;
    constexpr uint8_t BitsDiv256  = 0b100;
    constexpr uint8_t BitsDiv1024 = 0b101;
}

void test_frequencyFor_accounts_for_phase_correct_double_period(void) {
    // El factor 2 del modo phase correct: con divisor 64 y 8 bits el
    // resultado son ~488 Hz (el default de Arduino), no ~976.
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 488.28f, PwmTiming::frequencyFor(64, 8));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 3906.25f, PwmTiming::frequencyFor(8, 8));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 122.07f, PwmTiming::frequencyFor(256, 8));
}

// Regresion del bug corregido: sin el factor 2, pedir los 490 Hz del
// default hacia elegir el divisor 256 (~122 Hz reales) en vez del 64 que
// el timer ya traia, empeorando en silencio una configuracion correcta.
void test_selectPrescaler_default_490hz_keeps_div64(void) {
    TEST_ASSERT_EQUAL_UINT8(BitsDiv64, PwmTiming::selectPrescalerBits(490, 8));
}

void test_selectPrescaler_picks_nearest_option_across_the_table(void) {
    TEST_ASSERT_EQUAL_UINT8(BitsDiv1,    PwmTiming::selectPrescalerBits(31250, 8));
    TEST_ASSERT_EQUAL_UINT8(BitsDiv8,    PwmTiming::selectPrescalerBits(3906, 8));
    TEST_ASSERT_EQUAL_UINT8(BitsDiv256,  PwmTiming::selectPrescalerBits(122, 8));
    TEST_ASSERT_EQUAL_UINT8(BitsDiv1024, PwmTiming::selectPrescalerBits(30, 8));
}

// 3921 Hz es la frecuencia elegida para la excitacion de bobinas (ver
// docs/coil-excitation.md): la mas alta alcanzable sin cambiar el modo del
// timer que deja margen holgado para filtrar el PWM con un RC.
void test_selectPrescaler_coil_excitation_frequency_maps_to_div8(void) {
    TEST_ASSERT_EQUAL_UINT8(BitsDiv8, PwmTiming::selectPrescalerBits(3921, 8));
}

void test_selectPrescaler_resolution_shifts_the_whole_table(void) {
    // Con 10 bits de resolucion el periodo es 4x mas largo, asi que la
    // misma frecuencia pedida cae en un prescaler mas chico.
    TEST_ASSERT_EQUAL_UINT8(BitsDiv8, PwmTiming::selectPrescalerBits(976, 10));
}

void test_timerForPin_maps_the_four_supported_timers(void) {
    TEST_ASSERT_TRUE(PwmTimer::Timer1 == PwmTiming::timerForPin(11));
    TEST_ASSERT_TRUE(PwmTimer::Timer1 == PwmTiming::timerForPin(12));
    TEST_ASSERT_TRUE(PwmTimer::Timer3 == PwmTiming::timerForPin(3));
    TEST_ASSERT_TRUE(PwmTimer::Timer4 == PwmTiming::timerForPin(7));
    TEST_ASSERT_TRUE(PwmTimer::Timer5 == PwmTiming::timerForPin(44));
    TEST_ASSERT_TRUE(PwmTimer::Timer5 == PwmTiming::timerForPin(46));
}

// Timer0 (4, 13) cuenta millis()/delay() y Timer2 (9, 10) tiene 7
// prescalers en vez de 5 -- este driver no les toca la frecuencia.
void test_timerForPin_excludes_timer0_and_timer2(void) {
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(4));
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(13));
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(9));
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(10));
}

// Los pines analogicos no tienen timer: analogWrite() ahi degrada a un
// digitalWrite binario (por eso PWM_PIN dejo de ser A0).
void test_timerForPin_rejects_analog_and_non_pwm_pins(void) {
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(A0));
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(0));
    TEST_ASSERT_TRUE(PwmTimer::None == PwmTiming::timerForPin(47));
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

    RUN_TEST(test_frequencyFor_accounts_for_phase_correct_double_period);
    RUN_TEST(test_selectPrescaler_default_490hz_keeps_div64);
    RUN_TEST(test_selectPrescaler_picks_nearest_option_across_the_table);
    RUN_TEST(test_selectPrescaler_coil_excitation_frequency_maps_to_div8);
    RUN_TEST(test_selectPrescaler_resolution_shifts_the_whole_table);

    RUN_TEST(test_timerForPin_maps_the_four_supported_timers);
    RUN_TEST(test_timerForPin_excludes_timer0_and_timer2);
    RUN_TEST(test_timerForPin_rejects_analog_and_non_pwm_pins);

    return UNITY_END();
}
