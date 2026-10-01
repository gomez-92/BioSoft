#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/controlmap.hpp"

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

namespace {
    ControlMap mapWithOnePoint() {
        ControlMap map;
        map.setCount(1);
        map.setPoint(0, 1.0f, 50.0f, 0.4f, 0.05f);
        return map;
    }
}

// =====================================================================
// ControlMap: guardar, validar, buscar
// =====================================================================

void test_empty_map_finds_nothing(void) {
    ControlMap map;
    TEST_ASSERT_NULL(map.find(1.0f, 50.0f));
    TEST_ASSERT_EQUAL_UINT8(0, map.validCount());
}

void test_find_matches_intensity_and_frequency(void) {
    ControlMap map = mapWithOnePoint();
    const ControlMapPoint* p = map.find(1.0f, 50.0f);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, p->duty);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, p->balance);
}

// El mismo campo a otra frecuencia (o al reves) es OTRO punto: el balance y
// el duty cambian con ambas.
void test_find_requires_both_intensity_and_frequency(void) {
    ControlMap map = mapWithOnePoint();
    TEST_ASSERT_NULL(map.find(1.0f, 10.0f));
    TEST_ASSERT_NULL(map.find(2.0f, 50.0f));
}

void test_find_does_not_interpolate(void) {
    ControlMap map = mapWithOnePoint();
    TEST_ASSERT_NULL(map.find(1.2f, 50.0f));
}

void test_invalid_point_is_rejected_whole_and_leaves_slot_empty(void) {
    ControlMap map = mapWithOnePoint();
    TEST_ASSERT_FALSE(map.setPoint(0, 1.0f, 50.0f, 1.5f, 0.0f));   // duty > 1
    TEST_ASSERT_NULL(map.find(1.0f, 50.0f));                       // tampoco queda el viejo
    TEST_ASSERT_FALSE(map.setPoint(0, 1.0f, 50.0f, 0.4f, 0.9f));   // balance > tope
    TEST_ASSERT_FALSE(map.setPoint(0, 0.0f, 50.0f, 0.4f, 0.0f));   // intensidad 0
    TEST_ASSERT_FALSE(map.setPoint(0, 1.0f, 0.0f, 0.4f, 0.0f));    // frecuencia 0
    TEST_ASSERT_FALSE(map.setPoint(0, 1.0f, 50.0f, 0.0f, 0.0f));   // duty 0
    TEST_ASSERT_FALSE(map.setPoint(ControlMap::MaxPoints, 1.0f, 50.0f, 0.4f, 0.0f));
}

void test_setCount_empties_slots_the_new_card_no_longer_carries(void) {
    ControlMap map;
    map.setPoint(0, 1.0f, 50.0f, 0.4f, 0.0f);
    map.setPoint(1, 2.0f, 50.0f, 0.6f, 0.0f);
    map.setCount(1);
    TEST_ASSERT_NOT_NULL(map.find(1.0f, 50.0f));
    TEST_ASSERT_NULL(map.find(2.0f, 50.0f));
    TEST_ASSERT_EQUAL_UINT8(1, map.validCount());
}

// =====================================================================
// planStart
// =====================================================================

void test_X_without_point_starts_from_zero_and_never_refuses(void) {
    ControlMap map;
    StartPlan plan = planStart(map, FieldMode::X, 1.0f, 50.0f, 0.2f);
    TEST_ASSERT_TRUE(plan.refusal == StartRefusal::None);
    TEST_ASSERT_FALSE(plan.openLoop);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plan.initialDuty);
}

void test_X_with_point_starts_from_the_mapped_duty_with_the_loop_on(void) {
    ControlMap map = mapWithOnePoint();
    StartPlan plan = planStart(map, FieldMode::X, 1.0f, 50.0f, 0.2f);
    TEST_ASSERT_FALSE(plan.openLoop);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, plan.initialDuty);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, plan.balance);   // el balance es solo del campo nulo
}

void test_null_without_point_is_refused(void) {
    ControlMap map;
    StartPlan plan = planStart(map, FieldMode::Null, 1.0f, 50.0f, 0.2f);
    TEST_ASSERT_TRUE(plan.refusal == StartRefusal::NoMappedPoint);
}

void test_null_for_another_frequency_is_refused(void) {
    ControlMap map = mapWithOnePoint();
    StartPlan plan = planStart(map, FieldMode::Null, 1.0f, 10.0f, 0.2f);
    TEST_ASSERT_TRUE(plan.refusal == StartRefusal::NoMappedPoint);
}

void test_null_with_point_runs_open_loop_with_mapped_duty_and_balance(void) {
    ControlMap map = mapWithOnePoint();
    StartPlan plan = planStart(map, FieldMode::Null, 1.0f, 50.0f, 0.2f);
    TEST_ASSERT_TRUE(plan.refusal == StartRefusal::None);
    TEST_ASSERT_TRUE(plan.openLoop);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, plan.initialDuty);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, plan.balance);
}

// Rechazar y no recortar: un balance mas alto que el tope operativo no se
// baja al tope, se rechaza el arranque.
void test_null_with_balance_above_operational_cap_is_refused(void) {
    ControlMap map = mapWithOnePoint();
    StartPlan plan = planStart(map, FieldMode::Null, 1.0f, 50.0f, 0.03f);
    TEST_ASSERT_TRUE(plan.refusal == StartRefusal::BalanceOutOfRange);
    TEST_ASSERT_FALSE(plan.openLoop);
}

void test_negative_balance_is_judged_by_magnitude(void) {
    ControlMap map;
    map.setCount(1);
    map.setPoint(0, 1.0f, 50.0f, 0.4f, -0.05f);
    TEST_ASSERT_TRUE(planStart(map, FieldMode::Null, 1.0f, 50.0f, 0.1f).openLoop);
    TEST_ASSERT_TRUE(planStart(map, FieldMode::Null, 1.0f, 50.0f, 0.03f).refusal == StartRefusal::BalanceOutOfRange);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_empty_map_finds_nothing);
    RUN_TEST(test_find_matches_intensity_and_frequency);
    RUN_TEST(test_find_requires_both_intensity_and_frequency);
    RUN_TEST(test_find_does_not_interpolate);
    RUN_TEST(test_invalid_point_is_rejected_whole_and_leaves_slot_empty);
    RUN_TEST(test_setCount_empties_slots_the_new_card_no_longer_carries);

    RUN_TEST(test_X_without_point_starts_from_zero_and_never_refuses);
    RUN_TEST(test_X_with_point_starts_from_the_mapped_duty_with_the_loop_on);
    RUN_TEST(test_null_without_point_is_refused);
    RUN_TEST(test_null_for_another_frequency_is_refused);
    RUN_TEST(test_null_with_point_runs_open_loop_with_mapped_duty_and_balance);
    RUN_TEST(test_null_with_balance_above_operational_cap_is_refused);
    RUN_TEST(test_negative_balance_is_judged_by_magnitude);

    return UNITY_END();
}
