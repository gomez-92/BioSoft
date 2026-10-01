#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/scenariosensors.hpp"

using namespace fakeit;

// Escenarios sinteticos (tarjeta 22). Lo que se fija aca falla en silencio
// si se rompe: un escenario de "temperatura alta" que no llega a la zona
// critica no corta nada, y la corrida de prueba termina "completed" como si
// la logica de corte se hubiera validado.

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

static ScenarioRanges ranges(float nmin, float nmax, float cmin, float cmax) {
    ScenarioRanges r;
    r.normalMin = nmin; r.normalMax = nmax; r.criticalMin = cmin; r.criticalMax = cmax;
    return r;
}

// Los rangos de fabrica de temperatura: normal 30~40, critico 25~45.
static const ScenarioRanges Temp = ranges(30.0f, 40.0f, 25.0f, 45.0f);

// Objetivo 1.5 mT, tolerancia 10%, criticalMultiplier 1.25
// (DetectorConfigBuilder::applyCemRanges).
static const ScenarioRanges Cem = ranges(1.35f, 1.65f, 1.3125f, 1.6875f);

// =====================================================================
// Nivel -> valor
// =====================================================================

void test_level_anchors_map_to_range_edges(void) {
    TEST_ASSERT_EQUAL_FLOAT(35.0f, Scenario::levelToValue(Temp, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(40.0f, Scenario::levelToValue(Temp, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(30.0f, Scenario::levelToValue(Temp, -1.0f));
    TEST_ASSERT_EQUAL_FLOAT(45.0f, Scenario::levelToValue(Temp, 2.0f));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, Scenario::levelToValue(Temp, -2.0f));
}

void test_level_is_linear_between_anchors_and_beyond(void) {
    TEST_ASSERT_EQUAL_FLOAT(37.5f, Scenario::levelToValue(Temp, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(42.5f, Scenario::levelToValue(Temp, 1.5f));
    // Mas alla de 2, con el mismo paso que de 1 a 2 (5 grados por nivel).
    TEST_ASSERT_EQUAL_FLOAT(47.5f, Scenario::levelToValue(Temp, 2.5f));
    TEST_ASSERT_EQUAL_FLOAT(22.5f, Scenario::levelToValue(Temp, -2.5f));
}

// Por eso los niveles: el mismo escenario corta con cualquier par de rangos
// que se elija en pantalla.
void test_same_level_lands_in_the_same_zone_with_any_range(void) {
    const ScenarioRanges wide = ranges(26.0f, 44.0f, 20.0f, 50.0f);
    TEST_ASSERT_TRUE(Scenario::levelToValue(Temp, 2.5f) > Temp.criticalMax);
    TEST_ASSERT_TRUE(Scenario::levelToValue(wide, 2.5f) > wide.criticalMax);
    TEST_ASSERT_TRUE(Scenario::levelToValue(wide, 1.5f) > wide.normalMax);
    TEST_ASSERT_TRUE(Scenario::levelToValue(wide, 1.5f) < wide.criticalMax);
    TEST_ASSERT_TRUE(Scenario::levelToValue(Cem, 2.5f) > Cem.criticalMax);
}

// Sin franja entre normal y critico, el paso de 1 a 2 se toma como medio
// rango normal: si no, todo nivel > 1 caeria en el borde y nunca cruzaria.
void test_level_still_crosses_when_critical_equals_normal(void) {
    const ScenarioRanges flush = ranges(30.0f, 40.0f, 30.0f, 40.0f);
    TEST_ASSERT_TRUE(Scenario::levelToValue(flush, 1.5f) > 40.0f);
    TEST_ASSERT_TRUE(Scenario::levelToValue(flush, -1.5f) < 30.0f);
}

// =====================================================================
// Nivel en el tiempo
// =====================================================================

void test_levelAt_combines_base_ramp_and_step(void) {
    ScenarioSignal s;
    s.base = 0.5f;
    s.ramp = 1.0f;      // un nivel por minuto
    s.stepAt = 30.0f;
    s.step = 2.0f;

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, Scenario::levelAt(s, 0.0f, 0.0f));
    // Justo antes del salto: base + medio minuto de rampa.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, Scenario::levelAt(s, 29.99f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, Scenario::levelAt(s, 30.0f, 0.0f));
}

void test_levelAt_oscillation_peaks_at_a_quarter_period(void) {
    ScenarioSignal s;
    s.oscAmp = 1.5f;
    s.oscPeriod = 8.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, Scenario::levelAt(s, 2.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.5f, Scenario::levelAt(s, 6.0f, 0.0f));
}

void test_oscillation_needs_a_period(void) {
    ScenarioSignal s;
    s.oscAmp = 1.5f;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Scenario::levelAt(s, 2.0f, 0.0f));
}

void test_noise_stays_within_its_amplitude(void) {
    ScenarioSignal s;
    s.noise = 0.3f;
    uint32_t state = 42;
    for (int i = 0; i < 1000; i++) {
        float u = Scenario::nextUnitNoise(state);
        TEST_ASSERT_TRUE(u >= -1.0f && u <= 1.0f);
        float level = Scenario::levelAt(s, 0.0f, u);
        TEST_ASSERT_TRUE(level >= -0.3f && level <= 0.3f);
    }
}

void test_signal_drops_at_dropAt(void) {
    ScenarioSignal s;
    s.dropAt = 60.0f;
    TEST_ASSERT_TRUE(Scenario::validAt(s, 59.9f));
    TEST_ASSERT_FALSE(Scenario::validAt(s, 60.0f));
    ScenarioSignal never;
    TEST_ASSERT_TRUE(Scenario::validAt(never, 1e6f));
}

// =====================================================================
// Planta de CEM1
// =====================================================================

void test_plant_reaches_target_at_setpoint_duty(void) {
    ScenarioSignal s;
    s.setpointDuty = 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, Scenario::fieldValue(s, Cem, 0.0f, 0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, Scenario::fieldValue(s, Cem, 0.0f, 0.25f));
}

// La perturbacion se suma a lo que producen las bobinas: el lazo la puede
// compensar bajando el duty, como haria con bobinas reales.
void test_plant_adds_the_disturbance_of_the_level(void) {
    ScenarioSignal s;
    s.setpointDuty = 0.5f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.65f, Scenario::fieldValue(s, Cem, 1.0f, 0.5f));
}

void test_without_plant_the_field_is_the_level(void) {
    ScenarioSignal s;   // setpointDuty = 0
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.6875f, Scenario::fieldValue(s, Cem, 2.0f, 0.9f));
}

// =====================================================================
// Sensores
// =====================================================================

static float fixedDuty() { return 0.5f; }

void test_thermometer_scenario_reports_the_mapped_value(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(10000);
    Scenario::restart(0);   // t = 10 s

    ScenarioSignal s;
    s.stepAt = 5.0f;
    s.step = 2.5f;
    ThermometerScenario sensor("TEMP1");
    sensor.setSignal(s);
    sensor.setRanges(Temp);
    sensor.begin();

    sensor.update();

    TEST_ASSERT_TRUE(sensor.isValid());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 47.5f, sensor.getTemperature());
}

void test_thermometer_scenario_goes_silent_after_dropAt(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(61000);
    Scenario::restart(0);

    ScenarioSignal s;
    s.dropAt = 60.0f;
    ThermometerScenario sensor("TEMP1");
    sensor.setSignal(s);
    sensor.setRanges(Temp);
    sensor.begin();

    sensor.update();

    TEST_ASSERT_FALSE(sensor.isValid());
}

void test_magnetometer_scenario_reads_duty_from_its_source(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    Scenario::restart(0);

    ScenarioSignal s;
    s.setpointDuty = 0.5f;
    MagnetometerScenario sensor("CEM1", fixedDuty);
    sensor.setSignal(s);
    sensor.setRanges(Cem);
    sensor.begin();

    sensor.update();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, sensor.getMagneticField());
}

void test_magnetometer_scenario_never_reports_negative_field(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    Scenario::restart(0);

    ScenarioSignal s;
    s.setpointDuty = 0.5f;
    s.base = -20.0f;    // perturbacion enorme hacia abajo
    MagnetometerScenario sensor("CEM1", nullptr);   // sin fuente: duty 0
    sensor.setSignal(s);
    sensor.setRanges(Cem);
    sensor.begin();

    sensor.update();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensor.getMagneticField());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_level_anchors_map_to_range_edges);
    RUN_TEST(test_level_is_linear_between_anchors_and_beyond);
    RUN_TEST(test_same_level_lands_in_the_same_zone_with_any_range);
    RUN_TEST(test_level_still_crosses_when_critical_equals_normal);

    RUN_TEST(test_levelAt_combines_base_ramp_and_step);
    RUN_TEST(test_levelAt_oscillation_peaks_at_a_quarter_period);
    RUN_TEST(test_oscillation_needs_a_period);
    RUN_TEST(test_noise_stays_within_its_amplitude);
    RUN_TEST(test_signal_drops_at_dropAt);

    RUN_TEST(test_plant_reaches_target_at_setpoint_duty);
    RUN_TEST(test_plant_adds_the_disturbance_of_the_level);
    RUN_TEST(test_without_plant_the_field_is_the_level);

    RUN_TEST(test_thermometer_scenario_reports_the_mapped_value);
    RUN_TEST(test_thermometer_scenario_goes_silent_after_dropAt);
    RUN_TEST(test_magnetometer_scenario_reads_duty_from_its_source);
    RUN_TEST(test_magnetometer_scenario_never_reports_negative_field);

    return UNITY_END();
}
