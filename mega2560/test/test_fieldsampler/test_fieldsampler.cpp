#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unity.h>

#include "../../src/fieldsampler.hpp"

// Sin ArduinoFake: fieldsampler.hpp es logica pura (IFieldBus la aisla del
// sensor y del reloj), asi que todo el sistema se prueba con un reloj y un
// sensor simulados.

void setUp(void) {}
void tearDown(void) {}

// Sensor + reloj simulados. Cada operacion de bus cuesta tiempo (como el I2C
// real) y el loop() entre polls tiene jitter, para que las pruebas vean lo que
// ve el firmware: muestras que no caen justo en la grilla.
class FakeBus : public IFieldBus {
public:
    uint32_t clock = 0;          // microsegundos, con vuelta
    uint32_t triggerCostUs = 400;
    uint32_t fetchCostUs = 900;
    uint32_t convUs = 1840;
    uint32_t overheadCost = 1300;

    float freqHz = 50.0f;        // la frecuencia VERDADERA de la senal
    float amp[3] = {1000, 0, 0}; // amplitud pico por eje (uT)
    float dc[3] = {0, 0, 0};
    float phase0 = 0.7f;
    float noise = 0;             // desvio de ruido blanco por eje (uT)
    int harmN = 0;               // armonico indeseado (0 = ninguno)
    float harmRatio = 0;         // su amplitud relativa a la de cada eje

    int failTriggerCount = 0;    // cuantos triggers consecutivos fallan
    int failFetchCount = 0;

    uint64_t elapsedUs = 0;      // tiempo absoluto sin vuelta, para la senal
    uint64_t startedAbs = 0;
    uint32_t rng = 12345;

    float uniform() {
        rng = rng * 1664525u + 1013904223u;
        return (float)(rng >> 8) / 16777216.0f;
    }
    float gauss() {
        float u = uniform() + uniform() + uniform() + uniform() - 2.0f;  // ~N(0, 0.58)
        return u * 1.7320508f;
    }

    void advance(uint32_t us) { clock += us; elapsedUs += us; }

    uint32_t nowUs() const override { return clock; }
    bool trigger() override {
        advance(triggerCostUs);
        if (failTriggerCount > 0) { failTriggerCount--; return false; }
        startedAbs = elapsedUs;
        return true;
    }
    bool fetch(float& x, float& y, float& z) override {
        advance(fetchCostUs);
        if (failFetchCount > 0) { failFetchCount--; return false; }
        // El sensor integra durante la conversion: el centro cae a mitad.
        double t = ((double)startedAbs + convUs / 2.0) * 1e-6;
        double w = 2.0 * 3.14159265358979 * freqHz;
        float* out[3] = {&x, &y, &z};
        for (int i = 0; i < 3; i++)
            *out[i] = (float)(dc[i] + amp[i] * sin(w * t + phase0)
                              + (harmN ? amp[i] * harmRatio * sin(harmN * w * t + 1.1) : 0.0))
                      + noise * gauss();
        return true;
    }
    uint32_t conversionUs() const override { return convUs; }
    uint32_t overheadUs() const override { return overheadCost; }
};

struct Run {
    float values[64];
    int count = 0;
};

// Corre el sampler `seconds` segundos con un loop() de duracion variable y
// devuelve los resultados de ventana que fue produciendo.
static Run runFor(FakeBus& bus, FieldSampler& sampler, float seconds,
                  uint32_t loopMinUs = 200, uint32_t loopMaxUs = 1500,
                  uint32_t stallEveryUs = 0, uint32_t stallUs = 0) {
    Run run;
    uint64_t end = bus.elapsedUs + (uint64_t)(seconds * 1e6f);
    uint64_t nextStall = bus.elapsedUs + stallEveryUs;
    while (bus.elapsedUs < end) {
        if (sampler.poll() && run.count < 64) run.values[run.count++] = sampler.rmsUt();
        uint32_t gap = loopMinUs + (uint32_t)(bus.uniform() * (float)(loopMaxUs - loopMinUs));
        if (stallEveryUs && bus.elapsedUs >= nextStall) {
            gap += stallUs;
            nextStall += stallEveryUs;
        }
        bus.advance(gap);
    }
    return run;
}

static float expectedRms(const FakeBus& bus) {
    float p = 0;
    for (int i = 0; i < 3; i++) p += bus.amp[i] * bus.amp[i] * 0.5f;
    return sqrtf(p);
}

static float worstError(const Run& run, float expected) {
    float worst = 0;
    for (int i = 0; i < run.count; i++) {
        float e = fabsf(run.values[i] - expected) / expected;
        if (e > worst) worst = e;
    }
    return worst;
}

// =====================================================================
// Plan de muestreo
// =====================================================================

void test_plan_is_uniform_ten_per_period_when_the_sensor_allows(void) {
    SamplingPlan p = planSampling(10.0f, 3000);   // T = 100 ms
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_TRUE(p.uniform);
    TEST_ASSERT_EQUAL_UINT8(10, p.groupSize);
    TEST_ASSERT_EQUAL_UINT32(10000, p.spacingUs);
    TEST_ASSERT_EQUAL_UINT16(0, p.extraPeriods);
    TEST_ASSERT_EQUAL_UINT32(90000, p.spanUs);    // 9 intervalos: menos de un periodo
}

void test_plan_spreads_ten_samples_over_several_periods_above_the_uniform_limit(void) {
    SamplingPlan p = planSampling(100.0f, 3000);  // T = 10 ms: T/10 = 1 ms no entra
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_FALSE(p.uniform);
    TEST_ASSERT_EQUAL_UINT8(10, p.groupSize);     // ya no 3: sobran grados de libertad
    TEST_ASSERT_EQUAL_UINT16(0, p.extraPeriods);
    TEST_ASSERT_UINT32_WITHIN(5, 3820, p.spacingUs);   // 0.382 T
    TEST_ASSERT_UINT32_WITHIN(50, 34380, p.spanUs);    // ~3.4 periodos
}

void test_plan_spacing_is_never_a_submultiple_of_the_period(void) {
    // Si Ts = T/k las fases se repiten cada k muestras y el grupo vuelve a ser
    // un ajuste de k puntos disfrazado.
    for (float f = 34.0f; f <= 400.0f; f += 7.0f) {
        SamplingPlan p = planSampling(f, 3000);
        TEST_ASSERT_TRUE(p.ok);
        float periodUs = 1000000.0f / f;
        float cycles = (float)p.spacingUs / periodUs;
        float frac = cycles - floorf(cycles);
        char msg[64];
        snprintf(msg, sizeof(msg), "f=%.0f frac=%.3f", f, frac);
        TEST_ASSERT_TRUE_MESSAGE(fabsf(frac - 0.382f) < 0.01f || fabsf(frac - 0.618f) < 0.01f, msg);
        TEST_ASSERT_TRUE(p.spacingUs >= 3000);
    }
}

void test_plan_adds_whole_periods_when_the_period_is_shorter_than_the_bus(void) {
    SamplingPlan p = planSampling(400.0f, 3000);  // T = 2.5 ms: ni 0.382 T alcanza
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_EQUAL_UINT8(10, p.groupSize);
    TEST_ASSERT_TRUE(p.extraPeriods >= 1);
    TEST_ASSERT_TRUE(p.spacingUs >= 3000);
}

void test_plan_rejects_frequencies_out_of_range(void) {
    TEST_ASSERT_FALSE(planSampling(0.0f, 3000).ok);
    TEST_ASSERT_FALSE(planSampling(-5.0f, 3000).ok);
    TEST_ASSERT_FALSE(planSampling(5000.0f, 3000).ok);
}

void test_ad9833_actual_frequency_is_quantized(void) {
    // paso = 25e6 / 2^28 = 0.0931 Hz
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 50.0f, FieldSampling::ad9833ActualHz(50.0f));
    float oneHz = FieldSampling::ad9833ActualHz(1.0f);
    TEST_ASSERT_TRUE(fabsf(oneHz - 1.0f) > 0.01f);      // 1 Hz no es representable
    TEST_ASSERT_FLOAT_WITHIN(0.0932f, 1.0f, oneHz);
}

// =====================================================================
// Exactitud: senal limpia
// =====================================================================

static void checkSine(float f, float tol, float seconds = 12.0f) {
    FakeBus bus;
    bus.freqHz = f;
    bus.amp[0] = 800; bus.amp[1] = 300; bus.amp[2] = 150;
    bus.dc[0] = 40; bus.dc[1] = -25; bus.dc[2] = 15;
    FieldSampler sampler(bus);
    TEST_ASSERT_TRUE(sampler.begin(f, 2000));
    Run run = runFor(bus, sampler, seconds);
    TEST_ASSERT_TRUE_MESSAGE(run.count >= 3, "pocas ventanas");
    float err = worstError(run, expectedRms(bus));
    char msg[64];
    snprintf(msg, sizeof(msg), "f=%.1f Hz error=%.3f %%", f, err * 100.0f);
    TEST_ASSERT_TRUE_MESSAGE(err < tol, msg);
}

void test_rms_is_exact_across_the_whole_band_with_loop_jitter(void) {
    // Con el campo continuo presente y un loop() que tarda entre 0.2 y 1.5 ms.
    float freqs[] = {1.0f, 2.0f, 5.0f, 10.0f, 25.0f, 50.0f, 75.0f, 100.0f};
    for (float f : freqs) checkSine(f, 0.005f);
}

void test_dc_ambient_does_not_change_the_reading_and_needs_no_tare(void) {
    FakeBus a, b;
    a.freqHz = b.freqHz = 20.0f;
    a.amp[0] = b.amp[0] = 500;
    b.dc[0] = 300; b.dc[1] = -200; b.dc[2] = 100;   // ambiente grande
    FieldSampler sa(a), sb(b);
    sa.begin(20.0f, 2000);
    sb.begin(20.0f, 2000);
    Run ra = runFor(a, sa, 6.0f);
    Run rb = runFor(b, sb, 6.0f);
    TEST_ASSERT_TRUE(ra.count >= 2 && rb.count >= 2);
    TEST_ASSERT_FLOAT_WITHIN(500.0f * 0.7071f * 0.005f, ra.values[1], rb.values[1]);
}

void test_start_phase_does_not_matter(void) {
    for (float ph = 0.0f; ph < 6.28f; ph += 0.9f) {
        FakeBus bus;
        bus.freqHz = 100.0f;
        bus.phase0 = ph;
        FieldSampler sampler(bus);
        sampler.begin(100.0f, 2000);
        Run run = runFor(bus, sampler, 5.0f);
        TEST_ASSERT_TRUE(run.count >= 1);
        TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.005f);
    }
}

// =====================================================================
// Robustez: lo que en banco pasa de verdad
// =====================================================================

void test_a_frequency_error_stays_small(void) {
    // Cuarzo del Mega y del AD9833 desviados: la frecuencia real es 0.5 % distinta
    // de la que se asume. Un grupo dura unos pocos periodos, asi que no se acumula.
    float freqs[] = {1.0f, 10.0f, 50.0f, 100.0f};
    for (float f : freqs) {
        FakeBus bus;
        bus.freqHz = f * 1.005f;
        bus.amp[0] = 600;
        FieldSampler sampler(bus);
        sampler.begin(f, 2000);
        Run run = runFor(bus, sampler, 10.0f);
        TEST_ASSERT_TRUE(run.count >= 2);
        char msg[64];
        float err = worstError(run, expectedRms(bus));
        snprintf(msg, sizeof(msg), "f=%.0f error=%.3f %%", f, err * 100.0f);
        TEST_ASSERT_TRUE_MESSAGE(err < 0.01f, msg);
    }
}

void test_stalled_loop_discards_groups_but_keeps_the_reading(void) {
    FakeBus bus;
    bus.freqHz = 40.0f;
    bus.amp[0] = 700; bus.amp[1] = 200;
    FieldSampler sampler(bus);
    sampler.begin(40.0f, 2000);
    // cada ~0.37 s el loop se frena 60 ms (por ejemplo un envio por el serial)
    Run run = runFor(bus, sampler, 12.0f, 200, 1500, 370000, 60000);
    TEST_ASSERT_TRUE(run.count >= 3);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.005f);
}

void test_noise_adds_little_to_a_real_field(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    bus.amp[0] = 1000;         // ~707 uT eficaces
    bus.noise = 3.0f;          // 3 uT de ruido por muestra
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    Run run = runFor(bus, sampler, 10.0f);
    TEST_ASSERT_TRUE(run.count >= 3);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.01f);
}

void test_no_signal_reads_near_zero_not_garbage(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    bus.amp[0] = 0;
    bus.dc[0] = 200;           // solo ambiente
    bus.noise = 2.0f;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    Run run = runFor(bus, sampler, 8.0f);
    TEST_ASSERT_TRUE(run.count >= 2);
    for (int i = 0; i < run.count; i++) {
        TEST_ASSERT_TRUE(!isnan(run.values[i]));
        TEST_ASSERT_TRUE(run.values[i] < 15.0f);   // piso de ruido, no el ambiente
    }
}

void test_works_across_the_micros_rollover(void) {
    FakeBus bus;
    bus.clock = 0xFFFFFFFFu - 3000000u;     // vuelve a 0 a los 3 s
    bus.freqHz = 30.0f;
    bus.amp[0] = 900;
    FieldSampler sampler(bus);
    sampler.begin(30.0f, 2000);
    Run run = runFor(bus, sampler, 10.0f);
    TEST_ASSERT_TRUE(run.count >= 4);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.005f);
}

// =====================================================================
// Ventana
// =====================================================================

void test_a_new_value_arrives_about_every_window(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    Run run = runFor(bus, sampler, 10.5f);
    TEST_ASSERT_INT_WITHIN(1, 5, run.count);
}

void test_no_value_before_the_first_window_closes(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    Run run = runFor(bus, sampler, 1.5f);
    TEST_ASSERT_EQUAL_INT(0, run.count);
}

void test_window_follows_the_requested_length(void) {
    FakeBus bus;
    bus.freqHz = 20.0f;
    FieldSampler sampler(bus);
    sampler.begin(20.0f, 1000);
    Run run = runFor(bus, sampler, 10.5f);
    TEST_ASSERT_INT_WITHIN(1, 10, run.count);
}

// =====================================================================
// Salud del bus
// =====================================================================

void test_consecutive_bus_failures_make_it_unhealthy_and_it_recovers(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    bus.amp[0] = 500;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    TEST_ASSERT_TRUE(sampler.healthy());

    // Se corre solo hasta agotar las fallas: despues el bus responde y se recupera.
    bus.failFetchCount = FieldSampling::MaxConsecutiveFailures;
    for (int i = 0; i < 100000 && bus.failFetchCount > 0; i++) {
        sampler.poll();
        bus.advance(300);
    }
    TEST_ASSERT_FALSE(sampler.healthy());

    // El bus vuelve solo: el sensor se recupera sin intervencion.
    Run run = runFor(bus, sampler, 6.0f);
    TEST_ASSERT_TRUE(sampler.healthy());
    TEST_ASSERT_TRUE(run.count >= 1);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.01f);
}

void test_isolated_failures_do_not_make_it_unhealthy(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    bus.amp[0] = 500;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    for (int i = 0; i < 10; i++) {
        bus.failTriggerCount = 1;
        runFor(bus, sampler, 0.4f);
        TEST_ASSERT_TRUE(sampler.healthy());
    }
}

void test_a_failed_fetch_does_not_poison_the_group(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    bus.amp[0] = 800;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    Run run;
    for (int i = 0; i < 6; i++) {
        bus.failFetchCount = 1;
        Run r = runFor(bus, sampler, 1.7f);
        for (int j = 0; j < r.count && run.count < 64; j++) run.values[run.count++] = r.values[j];
    }
    TEST_ASSERT_TRUE(run.count >= 1);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.01f);
}

// =====================================================================
// Ciclo de vida
// =====================================================================

void test_begin_refuses_an_impossible_frequency_and_stays_stopped(void) {
    FakeBus bus;
    FieldSampler sampler(bus);
    TEST_ASSERT_FALSE(sampler.begin(0.0f, 2000));
    TEST_ASSERT_FALSE(sampler.running());
    TEST_ASSERT_FALSE(sampler.poll());
}

void test_stop_halts_polling_and_begin_restarts_clean(void) {
    FakeBus bus;
    bus.freqHz = 10.0f;
    bus.amp[0] = 500;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    runFor(bus, sampler, 1.0f);
    sampler.stop();
    TEST_ASSERT_FALSE(sampler.poll());
    sampler.begin(10.0f, 2000);
    Run run = runFor(bus, sampler, 6.0f);
    TEST_ASSERT_TRUE(run.count >= 2);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.01f);
}

void test_poll_does_one_bus_transaction_per_call(void) {
    // El tope de bloqueo de cada poll() es lo que protege al boton de
    // emergencia: una llamada no puede encadenar trigger + espera + fetch.
    FakeBus bus;
    bus.freqHz = 10.0f;
    FieldSampler sampler(bus);
    sampler.begin(10.0f, 2000);
    uint32_t worst = 0;
    for (int i = 0; i < 20000; i++) {
        uint32_t before = bus.clock;
        sampler.poll();
        uint32_t spent = bus.clock - before;
        if (spent > worst) worst = spent;
        bus.advance(300);
    }
    TEST_ASSERT_TRUE(worst <= bus.fetchCostUs + 10);
}

// =====================================================================
// Precision: grupo de varios periodos con fases repartidas
// =====================================================================

static float harmonicLeak(int h, float f, float ratio) {
    FakeBus bus;
    bus.freqHz = f;
    bus.amp[0] = 800; bus.amp[1] = 300; bus.amp[2] = 150;
    bus.harmN = h;
    bus.harmRatio = ratio;
    FieldSampler sampler(bus);
    sampler.begin(f, 2000);
    Run run = runFor(bus, sampler, 10.0f);
    TEST_ASSERT_TRUE(run.count >= 3);
    return worstError(run, expectedRms(bus));
}

void test_harmonics_do_not_leak_into_the_fundamental_at_100_hz(void) {
    // Con 3 muestras por periodo el 2do (y 4to, 5to) armonico cae EXACTO sobre la
    // fundamental: un 20 % de 2do armonico daba ~20 % de error. Con el grupo
    // repartido en varios periodos, el filtro de minimos cuadrados lo rechaza.
    for (int h = 2; h <= 7; h++) {
        float err = harmonicLeak(h, 100.0f, 0.2f);
        char msg[64];
        snprintf(msg, sizeof(msg), "h=%d error=%.3f %%", h, err * 100.0f);
        TEST_ASSERT_TRUE_MESSAGE(err < 0.03f, msg);
    }
}

void test_harmonics_are_also_rejected_at_50_and_60_hz(void) {
    float freqs[] = {50.0f, 60.0f, 40.0f};
    for (float f : freqs)
        for (int h = 2; h <= 5; h++) {
            float err = harmonicLeak(h, f, 0.2f);
            char msg[64];
            snprintf(msg, sizeof(msg), "f=%.0f h=%d error=%.3f %%", f, h, err * 100.0f);
            TEST_ASSERT_TRUE_MESSAGE(err < 0.03f, msg);
        }
}

void test_a_group_spans_several_periods_and_still_closes_in_a_window(void) {
    FakeBus bus;
    bus.freqHz = 100.0f;
    FieldSampler sampler(bus);
    sampler.begin(100.0f, 2000);
    TEST_ASSERT_TRUE(sampler.plan().spanUs > 3 * 10000);
    Run run = runFor(bus, sampler, 10.0f);
    TEST_ASSERT_TRUE(run.count >= 4);
    TEST_ASSERT_TRUE(sampler.lastGroups() >= 20);    // decenas de grupos por ventana
    TEST_ASSERT_TRUE(sampler.lastDiscarded() <= 2);  // el jitter normal no los descarta
}

void test_noise_is_averaged_down_at_100_hz_with_spread_groups(void) {
    FakeBus bus;
    bus.freqHz = 100.0f;
    bus.amp[0] = 1000;
    bus.noise = 20.0f;      // 2 % del pico, por muestra
    FieldSampler sampler(bus);
    sampler.begin(100.0f, 2000);
    Run run = runFor(bus, sampler, 12.0f);
    TEST_ASSERT_TRUE(run.count >= 4);
    TEST_ASSERT_TRUE(worstError(run, expectedRms(bus)) < 0.02f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_plan_is_uniform_ten_per_period_when_the_sensor_allows);
    RUN_TEST(test_plan_spreads_ten_samples_over_several_periods_above_the_uniform_limit);
    RUN_TEST(test_plan_spacing_is_never_a_submultiple_of_the_period);
    RUN_TEST(test_plan_adds_whole_periods_when_the_period_is_shorter_than_the_bus);
    RUN_TEST(test_plan_rejects_frequencies_out_of_range);
    RUN_TEST(test_ad9833_actual_frequency_is_quantized);
    RUN_TEST(test_rms_is_exact_across_the_whole_band_with_loop_jitter);
    RUN_TEST(test_dc_ambient_does_not_change_the_reading_and_needs_no_tare);
    RUN_TEST(test_start_phase_does_not_matter);
    RUN_TEST(test_a_frequency_error_stays_small);
    RUN_TEST(test_stalled_loop_discards_groups_but_keeps_the_reading);
    RUN_TEST(test_noise_adds_little_to_a_real_field);
    RUN_TEST(test_no_signal_reads_near_zero_not_garbage);
    RUN_TEST(test_works_across_the_micros_rollover);
    RUN_TEST(test_a_new_value_arrives_about_every_window);
    RUN_TEST(test_no_value_before_the_first_window_closes);
    RUN_TEST(test_window_follows_the_requested_length);
    RUN_TEST(test_consecutive_bus_failures_make_it_unhealthy_and_it_recovers);
    RUN_TEST(test_isolated_failures_do_not_make_it_unhealthy);
    RUN_TEST(test_a_failed_fetch_does_not_poison_the_group);
    RUN_TEST(test_begin_refuses_an_impossible_frequency_and_stays_stopped);
    RUN_TEST(test_stop_halts_polling_and_begin_restarts_clean);
    RUN_TEST(test_poll_does_one_bus_transaction_per_call);
    RUN_TEST(test_harmonics_do_not_leak_into_the_fundamental_at_100_hz);
    RUN_TEST(test_harmonics_are_also_rejected_at_50_and_60_hz);
    RUN_TEST(test_a_group_spans_several_periods_and_still_closes_in_a_window);
    RUN_TEST(test_noise_is_averaged_down_at_100_hz_with_spread_groups);
    return UNITY_END();
}
