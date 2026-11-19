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

// Arma una senal desde una lista plana [t0, n0, t1, n1, ...], por el mismo
// camino que config_scenario_pts.
static ScenarioSignal withPoints(const float* flat, uint8_t length) {
    ScenarioSignal s;
    TEST_ASSERT_TRUE(Scenario::PointsRefusal::None == Scenario::applyPoints(s, flat, length));
    return s;
}

void test_without_points_the_level_is_zero(void) {
    ScenarioSignal s;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Scenario::levelAt(s, 0.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Scenario::levelAt(s, 5000.0f, 0.0f));
}

// Una rampa de 0,5 a 1,5 entre 10 y 70 s: antes del primer punto y despues
// del ultimo se mantiene el extremo.
void test_points_interpolate_linearly_and_hold_the_ends(void) {
    const float flat[] = { 10, 0.5f, 70, 1.5f };
    ScenarioSignal s = withPoints(flat, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, Scenario::levelAt(s, 0.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, Scenario::levelAt(s, 10.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, Scenario::levelAt(s, 40.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, Scenario::levelAt(s, 70.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, Scenario::levelAt(s, 9999.0f, 0.0f));
}

// Dos puntos en el mismo segundo son un salto: antes vale el primero, desde
// ese segundo el segundo. Es como se dibuja "temperatura alta".
void test_two_points_on_the_same_second_are_a_step(void) {
    const float flat[] = { 0, 0, 60, 0, 60, 2.5f };
    ScenarioSignal s = withPoints(flat, 6);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, Scenario::levelAt(s, 59.99f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, Scenario::levelAt(s, 60.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, Scenario::levelAt(s, 300.0f, 0.0f));
}

// Sube, se mantiene y se recupera: la forma que salto+rampa no podia dar.
void test_points_can_go_up_and_come_back(void) {
    const float flat[] = { 0, 0, 60, 1.5f, 120, 1.5f, 180, 0 };
    ScenarioSignal s = withPoints(flat, 8);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, Scenario::levelAt(s, 30.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, Scenario::levelAt(s, 90.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, Scenario::levelAt(s, 150.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, Scenario::levelAt(s, 200.0f, 0.0f));
}

// =====================================================================
// Validacion de los puntos: rechazar entero, nunca a medias
// =====================================================================

static void assertRejectedKeepsPrevious(const float* flat, uint8_t length) {
    const float previous[] = { 0, 1.0f };
    ScenarioSignal s = withPoints(previous, 2);
    TEST_ASSERT_TRUE(Scenario::PointsRefusal::None != Scenario::applyPoints(s, flat, length));
    TEST_ASSERT_EQUAL_UINT8(1, s.pointCount);
    TEST_ASSERT_EQUAL_INT16(100, s.points[0].level100);
}

void test_points_odd_length_is_rejected(void) {
    const float flat[] = { 0, 1.0f, 60 };
    assertRejectedKeepsPrevious(flat, 3);
}

void test_points_out_of_order_are_rejected(void) {
    const float flat[] = { 60, 1.0f, 30, 2.0f };
    assertRejectedKeepsPrevious(flat, 4);
}

void test_points_fractional_or_negative_second_is_rejected(void) {
    const float half[] = { 0, 0, 30.5f, 2.0f };
    assertRejectedKeepsPrevious(half, 4);
    const float negative[] = { -1, 0 };
    assertRejectedKeepsPrevious(negative, 2);
}

void test_points_level_beyond_ten_is_rejected(void) {
    const float flat[] = { 0, 0, 60, 10.5f };
    assertRejectedKeepsPrevious(flat, 4);
}

void test_more_than_eight_points_are_rejected(void) {
    float flat[18];
    for (int i = 0; i < 9; i++) { flat[2 * i] = (float)(i * 10); flat[2 * i + 1] = 0.0f; }
    assertRejectedKeepsPrevious(flat, 18);
}

// Lista vacia: borra los puntos (nivel 0). Es lo que manda una tarjeta con
// `points: []`, y tiene que pisar lo que quedo de una conexion anterior.
void test_empty_points_clear_the_signal(void) {
    const float previous[] = { 0, 1.0f };
    ScenarioSignal s = withPoints(previous, 2);
    TEST_ASSERT_TRUE(Scenario::PointsRefusal::None == Scenario::applyPoints(s, previous, 0));
    TEST_ASSERT_EQUAL_UINT8(0, s.pointCount);
}

// Los niveles se guardan en centesimas: -2.35 no tiene que volverse -2.34
// por truncar en vez de redondear.
void test_point_levels_round_to_hundredths(void) {
    const float flat[] = { 0, -2.35f, 1, 1.25f };
    ScenarioSignal s = withPoints(flat, 4);
    TEST_ASSERT_EQUAL_INT16(-235, s.points[0].level100);
    TEST_ASSERT_EQUAL_INT16(125, s.points[1].level100);
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

    const float flat[] = { 5, 0, 5, 2.5f };
    ScenarioSignal s = withPoints(flat, 4);
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

    const float flat[] = { 0, -10.0f };   // perturbacion enorme hacia abajo
    ScenarioSignal s = withPoints(flat, 2);
    s.setpointDuty = 0.5f;
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

    RUN_TEST(test_without_points_the_level_is_zero);
    RUN_TEST(test_points_interpolate_linearly_and_hold_the_ends);
    RUN_TEST(test_two_points_on_the_same_second_are_a_step);
    RUN_TEST(test_points_can_go_up_and_come_back);

    RUN_TEST(test_points_odd_length_is_rejected);
    RUN_TEST(test_points_out_of_order_are_rejected);
    RUN_TEST(test_points_fractional_or_negative_second_is_rejected);
    RUN_TEST(test_points_level_beyond_ten_is_rejected);
    RUN_TEST(test_more_than_eight_points_are_rejected);
    RUN_TEST(test_empty_points_clear_the_signal);
    RUN_TEST(test_point_levels_round_to_hundredths);
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
