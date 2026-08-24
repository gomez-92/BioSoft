#include <math.h>
#include <unity.h>

#include "../../src/rmscalculator.hpp"

// Estos tests no necesitan ArduinoFake: RmsAccumulator no incluye Arduino.h,
// solo math.h/stdint.h -- por diseño, ver rmscalculator.hpp.

void setUp(void) {}
void tearDown(void) {}

// =====================================================================
// result() sin muestras
// =====================================================================

void test_result_returns_false_when_no_samples_added(void) {
    RmsAccumulator rms;
    float out = -1.0f;

    TEST_ASSERT_FALSE(rms.result(out));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, out); // no se toca out
}

// =====================================================================
// RMS de una señal sin componente DC (onda cuadrada balanceada)
// =====================================================================

void test_result_computes_amplitude_for_dc_free_square_wave(void) {
    RmsAccumulator rms;
    const float amplitude = 2.0f;
    const uint16_t samples = 1000;

    for (uint16_t i = 0; i < samples; i++) {
        rms.addSample((i % 2 == 0) ? amplitude : -amplitude);
    }

    float out = 0.0f;
    TEST_ASSERT_TRUE(rms.result(out));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, amplitude, out);
}

// =====================================================================
// El offset DC se remueve antes de elevar al cuadrado
// =====================================================================

void test_result_removes_dc_offset_before_squaring(void) {
    RmsAccumulator withDc;
    RmsAccumulator withoutDc;
    const float amplitude = 2.0f;
    const float dcOffset = 5.0f;
    const uint16_t samples = 2000;

    for (uint16_t i = 0; i < samples; i++) {
        float ac = (i % 2 == 0) ? amplitude : -amplitude;
        withDc.addSample(ac + dcOffset);
        withoutDc.addSample(ac);
    }

    float rmsWithDc = 0.0f;
    float rmsWithoutDc = 0.0f;
    TEST_ASSERT_TRUE(withDc.result(rmsWithDc));
    TEST_ASSERT_TRUE(withoutDc.result(rmsWithoutDc));

    // Si el filtro DC no funcionara, rmsWithDc sería ~sqrt(dcOffset^2 +
    // amplitude^2) (~5.4), muy por encima de la amplitud real.
    TEST_ASSERT_FLOAT_WITHIN(0.05f, rmsWithoutDc, rmsWithDc);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, amplitude, rmsWithDc);
}

// =====================================================================
// Señal constante (DC puro, sin componente AC) -> RMS converge a ~0
// =====================================================================

void test_result_converges_to_zero_for_pure_dc_signal(void) {
    RmsAccumulator rms;
    const uint16_t samples = 500;

    for (uint16_t i = 0; i < samples; i++) {
        rms.addSample(3.3f);
    }

    float out = -1.0f;
    TEST_ASSERT_TRUE(rms.result(out));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, out);
}

// =====================================================================
// NaN / Inf no se propagan como resultado válido
// =====================================================================

void test_result_returns_false_when_input_contains_nan(void) {
    RmsAccumulator rms;
    rms.addSample(1.0f);
    rms.addSample(NAN);
    rms.addSample(1.0f);

    float out = 0.0f;
    TEST_ASSERT_FALSE(rms.result(out));
}

void test_result_returns_false_when_input_contains_inf(void) {
    RmsAccumulator rms;
    rms.addSample(1.0f);
    rms.addSample(INFINITY);
    rms.addSample(1.0f);

    float out = 0.0f;
    TEST_ASSERT_FALSE(rms.result(out));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_result_returns_false_when_no_samples_added);

    RUN_TEST(test_result_computes_amplitude_for_dc_free_square_wave);
    RUN_TEST(test_result_removes_dc_offset_before_squaring);
    RUN_TEST(test_result_converges_to_zero_for_pure_dc_signal);

    RUN_TEST(test_result_returns_false_when_input_contains_nan);
    RUN_TEST(test_result_returns_false_when_input_contains_inf);

    return UNITY_END();
}
