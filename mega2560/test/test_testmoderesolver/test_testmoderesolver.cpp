#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/testmoderesolver.hpp"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// Eje sensor: sim (1-3) vs real (4-6)
// =====================================================================

void test_modes_1_to_3_use_sim_sensor(void) {
    TEST_ASSERT_FALSE(resolveTestMode(1).useRealSensor);
    TEST_ASSERT_FALSE(resolveTestMode(2).useRealSensor);
    TEST_ASSERT_FALSE(resolveTestMode(3).useRealSensor);
}

void test_modes_4_to_6_use_real_sensor(void) {
    TEST_ASSERT_TRUE(resolveTestMode(4).useRealSensor);
    TEST_ASSERT_TRUE(resolveTestMode(5).useRealSensor);
    TEST_ASSERT_TRUE(resolveTestMode(6).useRealSensor);
}

// =====================================================================
// Eje detector (lazo cerrado): solo pasos 3 y 6
// =====================================================================

void test_only_modes_3_and_6_are_closed_loop(void) {
    TEST_ASSERT_FALSE(resolveTestMode(1).closedLoop);
    TEST_ASSERT_FALSE(resolveTestMode(2).closedLoop);
    TEST_ASSERT_TRUE(resolveTestMode(3).closedLoop);
    TEST_ASSERT_FALSE(resolveTestMode(4).closedLoop);
    TEST_ASSERT_FALSE(resolveTestMode(5).closedLoop);
    TEST_ASSERT_TRUE(resolveTestMode(6).closedLoop);
}

// =====================================================================
// Eje control PWM: pasos "solo sensado" (1,4) lo dejan apagado
// =====================================================================

void test_monitor_only_modes_disable_control_loop(void) {
    TEST_ASSERT_FALSE(resolveTestMode(1).controlLoopEnabled);
    TEST_ASSERT_FALSE(resolveTestMode(4).controlLoopEnabled);
}

void test_open_and_closed_loop_modes_enable_control_loop(void) {
    TEST_ASSERT_TRUE(resolveTestMode(2).controlLoopEnabled);
    TEST_ASSERT_TRUE(resolveTestMode(3).controlLoopEnabled);
    TEST_ASSERT_TRUE(resolveTestMode(5).controlLoopEnabled);
    TEST_ASSERT_TRUE(resolveTestMode(6).controlLoopEnabled);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_modes_1_to_3_use_sim_sensor);
    RUN_TEST(test_modes_4_to_6_use_real_sensor);

    RUN_TEST(test_only_modes_3_and_6_are_closed_loop);

    RUN_TEST(test_monitor_only_modes_disable_control_loop);
    RUN_TEST(test_open_and_closed_loop_modes_enable_control_loop);

    return UNITY_END();
}
