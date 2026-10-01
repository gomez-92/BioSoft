#include <unity.h>
#include <math.h>

#include "../../src/fieldtare.hpp"

void setUp(void) {}
void tearDown(void) {}

static TareConfig config(uint8_t samples = 4, float spread = 10.0f, float ambient = 300.0f) {
    TareConfig c;
    c.samples = samples;
    c.maxSpread = spread;
    c.maxAmbient = ambient;
    return c;
}

// =====================================================================
// Sin tara
// =====================================================================

void test_without_tare_apply_is_the_plain_magnitude(void) {
    FieldTare tare;
    TEST_ASSERT_EQUAL(TareStatus::Idle, tare.status());
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 5.0f, tare.apply(3.0f, 4.0f, 0.0f));
}

void test_samples_are_ignored_when_not_collecting(void) {
    FieldTare tare;
    TEST_ASSERT_EQUAL(TareStatus::Idle, tare.addSample(1, 2, 3));
    TEST_ASSERT_EQUAL(0, tare.sampleCount());
}

// =====================================================================
// Toma de la tara
// =====================================================================

void test_collects_until_the_last_sample_and_then_is_ready(void) {
    FieldTare tare;
    tare.begin(config(4));
    TEST_ASSERT_EQUAL(TareStatus::Collecting, tare.addSample(20, 30, -40));
    TEST_ASSERT_EQUAL(TareStatus::Collecting, tare.addSample(21, 29, -41));
    TEST_ASSERT_EQUAL(TareStatus::Collecting, tare.addSample(19, 31, -39));
    TEST_ASSERT_EQUAL(TareStatus::Ready, tare.addSample(20, 30, -40));
    TEST_ASSERT_TRUE(tare.isReady());
}

void test_ambient_is_the_average_of_the_samples(void) {
    FieldTare tare;
    tare.begin(config(4));
    tare.addSample(18, 30, -40);
    tare.addSample(22, 30, -40);
    tare.addSample(18, 30, -40);
    tare.addSample(22, 30, -40);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 20.0f, tare.ambientX());
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 30.0f, tare.ambientY());
    TEST_ASSERT_FLOAT_WITHIN(1e-4, -40.0f, tare.ambientZ());
}

void test_extra_samples_after_ready_do_not_move_the_ambient(void) {
    FieldTare tare;
    tare.begin(config(2));
    tare.addSample(10, 0, 0);
    tare.addSample(10, 0, 0);
    tare.addSample(500, 500, 500);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 10.0f, tare.ambientX());
    TEST_ASSERT_EQUAL(2, tare.sampleCount());
}

void test_fewer_than_two_samples_requested_still_asks_for_two(void) {
    FieldTare tare;
    tare.begin(config(1));
    TEST_ASSERT_EQUAL(TareStatus::Collecting, tare.addSample(1, 1, 1));
    TEST_ASSERT_EQUAL(TareStatus::Ready, tare.addSample(1, 1, 1));
}

// =====================================================================
// Rechazo (rechazar, no recortar)
// =====================================================================

void test_rejects_when_samples_disagree_on_any_axis(void) {
    FieldTare tare;
    tare.begin(config(4, 10.0f));
    tare.addSample(20, 30, -40);
    tare.addSample(20, 30, -40);
    tare.addSample(20, 30, -40);
    // Solo el eje z se mueve 12 uT: alcanza para rechazar.
    TEST_ASSERT_EQUAL(TareStatus::Failed, tare.addSample(20, 30, -28));
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 12.0f, tare.worstSpread());
}

void test_a_rejected_tare_does_not_discount_anything(void) {
    FieldTare tare;
    tare.begin(config(2, 1.0f));
    tare.addSample(0, 0, 0);
    tare.addSample(50, 0, 0);
    TEST_ASSERT_EQUAL(TareStatus::Failed, tare.status());
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 5.0f, tare.apply(3, 4, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.0f, tare.ambientMagnitude());
}

void test_rejects_an_ambient_too_large_to_be_ambient(void) {
    FieldTare tare;
    tare.begin(config(2, 10.0f, 300.0f));
    tare.addSample(400, 0, 0);
    TEST_ASSERT_EQUAL(TareStatus::Failed, tare.addSample(400, 0, 0));
}

void test_rejects_a_nan_sample_at_once(void) {
    FieldTare tare;
    tare.begin(config(4));
    TEST_ASSERT_EQUAL(TareStatus::Failed, tare.addSample(NAN, 0, 0));
}

void test_a_tare_at_the_spread_limit_is_accepted(void) {
    FieldTare tare;
    tare.begin(config(2, 10.0f));
    tare.addSample(0, 0, 0);
    TEST_ASSERT_EQUAL(TareStatus::Ready, tare.addSample(10, 0, 0));
}

// =====================================================================
// Aplicacion: resta vectorial
// =====================================================================

static FieldTare readyWith(float x, float y, float z) {
    FieldTare tare;
    tare.begin(config(2));
    tare.addSample(x, y, z);
    tare.addSample(x, y, z);
    return tare;
}

void test_reading_the_ambient_back_gives_zero(void) {
    FieldTare tare = readyWith(20, 30, -40);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.0f, tare.apply(20, 30, -40));
}

void test_coil_field_parallel_to_ambient_is_recovered(void) {
    FieldTare tare = readyWith(30, 0, 40);  // |A| = 50
    // Bobina de 1000 uT en la misma direccion: el total es 1050 uT.
    float k = 1000.0f / 50.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-1, 1000.0f, tare.apply(30 + 30 * k, 0, 40 + 40 * k));
}

void test_coil_field_perpendicular_to_ambient_is_recovered(void) {
    FieldTare tare = readyWith(30, 0, 40);
    // Aca restar magnitudes daria sqrt(50^2+1000^2) - 50 = ~950, no 1000:
    // por eso se resta el vector.
    TEST_ASSERT_FLOAT_WITHIN(1e-1, 1000.0f, tare.apply(30, 1000, 40));
}

void test_coil_field_opposite_to_ambient_is_recovered(void) {
    FieldTare tare = readyWith(30, 0, 40);
    // Bobina de 1000 uT en contra: el sensor lee el ambiente menos 1000.
    float k = -1000.0f / 50.0f;
    TEST_ASSERT_FLOAT_WITHIN(1e-1, 1000.0f, tare.apply(30 + 30 * k, 0, 40 + 40 * k));
}

// =====================================================================
// Reinicio
// =====================================================================

void test_begin_discards_the_previous_tare(void) {
    FieldTare tare = readyWith(20, 30, -40);
    tare.begin(config(2));
    TEST_ASSERT_EQUAL(TareStatus::Collecting, tare.status());
    // Mientras junta, todavia no descuenta nada.
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 5.0f, tare.apply(3, 4, 0));
}

void test_clear_returns_to_no_tare(void) {
    FieldTare tare = readyWith(20, 30, -40);
    tare.clear();
    TEST_ASSERT_EQUAL(TareStatus::Idle, tare.status());
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 5.0f, tare.apply(3, 4, 0));
}

void test_tare_timeout_stays_below_the_esp32_busy_timeout(void) {
    // Pantalla Procesando de la ESP32: 6 s (BusyController).
    TEST_ASSERT_TRUE(TareTiming::TimeoutMs < 6000);
    // Y alcanza para las lecturas de la configuracion por defecto a 500 ms.
    TareConfig c;
    TEST_ASSERT_TRUE(TareTiming::SettleMs + (unsigned long)c.samples * 500UL < TareTiming::TimeoutMs);
}

// =====================================================================
// Limites de los umbrales que llegan de la SD
// =====================================================================

void test_limits_accept_the_factory_defaults(void) {
    TareConfig factory;
    TEST_ASSERT_TRUE(TareLimits::isValidSamples(factory.samples));
    TEST_ASSERT_TRUE(TareLimits::isValidSpread(factory.maxSpread));
    TEST_ASSERT_TRUE(TareLimits::isValidAmbient(factory.maxAmbient));
}

void test_limits_reject_values_outside_the_range(void) {
    TEST_ASSERT_FALSE(TareLimits::isValidSamples(1));
    TEST_ASSERT_FALSE(TareLimits::isValidSamples(7));
    TEST_ASSERT_FALSE(TareLimits::isValidSamples(-3));
    TEST_ASSERT_TRUE(TareLimits::isValidSamples(2));
    TEST_ASSERT_TRUE(TareLimits::isValidSamples(6));
    TEST_ASSERT_FALSE(TareLimits::isValidSpread(0.0f));
    TEST_ASSERT_FALSE(TareLimits::isValidSpread(100.5f));
    TEST_ASSERT_FALSE(TareLimits::isValidAmbient(10.0f));
    TEST_ASSERT_FALSE(TareLimits::isValidAmbient(5000.0f));
}

void test_limits_reject_nan(void) {
    TEST_ASSERT_FALSE(TareLimits::isValidSpread(NAN));
    TEST_ASSERT_FALSE(TareLimits::isValidAmbient(NAN));
}

// El tope de lecturas tiene que entrar en el vencimiento con la cadencia de
// fabrica de CEM1 (500 ms): settle + (N-1) lecturas.
void test_max_samples_fit_in_the_timeout_at_the_default_cadence(void) {
    unsigned long needed = TareTiming::SettleMs + (TareLimits::MaxSamples - 1) * 500UL;
    TEST_ASSERT_TRUE(needed < TareTiming::TimeoutMs);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_without_tare_apply_is_the_plain_magnitude);
    RUN_TEST(test_samples_are_ignored_when_not_collecting);
    RUN_TEST(test_collects_until_the_last_sample_and_then_is_ready);
    RUN_TEST(test_ambient_is_the_average_of_the_samples);
    RUN_TEST(test_extra_samples_after_ready_do_not_move_the_ambient);
    RUN_TEST(test_fewer_than_two_samples_requested_still_asks_for_two);
    RUN_TEST(test_rejects_when_samples_disagree_on_any_axis);
    RUN_TEST(test_a_rejected_tare_does_not_discount_anything);
    RUN_TEST(test_rejects_an_ambient_too_large_to_be_ambient);
    RUN_TEST(test_rejects_a_nan_sample_at_once);
    RUN_TEST(test_a_tare_at_the_spread_limit_is_accepted);
    RUN_TEST(test_reading_the_ambient_back_gives_zero);
    RUN_TEST(test_coil_field_parallel_to_ambient_is_recovered);
    RUN_TEST(test_coil_field_perpendicular_to_ambient_is_recovered);
    RUN_TEST(test_coil_field_opposite_to_ambient_is_recovered);
    RUN_TEST(test_begin_discards_the_previous_tare);
    RUN_TEST(test_clear_returns_to_no_tare);
    RUN_TEST(test_tare_timeout_stays_below_the_esp32_busy_timeout);
    RUN_TEST(test_limits_accept_the_factory_defaults);
    RUN_TEST(test_limits_reject_values_outside_the_range);
    RUN_TEST(test_limits_reject_nan);
    RUN_TEST(test_max_samples_fit_in_the_timeout_at_the_default_cadence);
    return UNITY_END();
}
