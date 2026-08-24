#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/currentmanager.hpp"

using namespace fakeit;

// =====================================================================
// FakeCurrentSensor -- doble de prueba de ICurrentSensor, sin dependencia
// de hardware real (a diferencia de CurrentSensorSct013, que depende de
// Adafruit_ADS1X15 y no compila en el entorno native, ver CLAUDE.md/tarjeta
// MOD-006). Deja controlar begin()/update() call counts, nombre, corriente
// y validez de forma directa desde cada test.
// =====================================================================

class FakeCurrentSensor : public ICurrentSensor {
  public:
    const char* name;
    float current = 0.0f;
    bool valid = false;
    uint8_t beginCalls = 0;
    uint8_t updateCalls = 0;

    explicit FakeCurrentSensor(const char* name) : name(name) {}

    void begin() override { beginCalls++; }
    void update() override { updateCalls++; }
    float getCurrent() const override { return current; }
    bool isValid() const override { return valid; }
    const char* getName() const override { return name; }
};

class RecordingCurrentSensorListener : public ICurrentSensorListener {
  public:
    ICurrentSensor* lastSample = nullptr;
    uint8_t sampleCount = 0;

    void onCurrentSensorSample(ICurrentSensor* currentSensor) override {
        lastSample = currentSensor;
        sampleCount++;
    }
};

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// addCurrentSensor
// =====================================================================

void test_addCurrentSensor_assigns_sequential_ids_and_calls_begin(void) {
    CurrentSensorsManager manager;
    FakeCurrentSensor s0("SCT013-1");
    FakeCurrentSensor s1("SCT013-2");

    TEST_ASSERT_EQUAL_UINT8(0, manager.addCurrentSensor(&s0));
    TEST_ASSERT_EQUAL_UINT8(1, manager.addCurrentSensor(&s1));
    TEST_ASSERT_EQUAL_UINT8(1, s0.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(1, s1.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(2, manager.currentSensorsCount());
}

void test_addCurrentSensor_rejects_null_without_adding(void) {
    CurrentSensorsManager manager;
    TEST_ASSERT_EQUAL_UINT8(0xFF, manager.addCurrentSensor(nullptr));
    TEST_ASSERT_EQUAL_UINT8(0, manager.currentSensorsCount());
}

void test_addCurrentSensor_rejects_when_full_returns_0xFF(void) {
    CurrentSensorsManager manager;
    FakeCurrentSensor sensors[MAX_CURRENT_SENSORS] = {
        FakeCurrentSensor("S0"), FakeCurrentSensor("S1"), FakeCurrentSensor("S2"),
        FakeCurrentSensor("S3"), FakeCurrentSensor("S4"), FakeCurrentSensor("S5"),
        FakeCurrentSensor("S6"), FakeCurrentSensor("S7"), FakeCurrentSensor("S8"),
        FakeCurrentSensor("S9"),
    };
    for (uint8_t i = 0; i < MAX_CURRENT_SENSORS; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, manager.addCurrentSensor(&sensors[i]));
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_CURRENT_SENSORS, manager.currentSensorsCount());

    FakeCurrentSensor overflow("OVERFLOW");
    TEST_ASSERT_EQUAL_UINT8(0xFF, manager.addCurrentSensor(&overflow));
    TEST_ASSERT_EQUAL_UINT8(0, overflow.beginCalls); // ni siquiera se inicializa
    TEST_ASSERT_EQUAL_UINT8(MAX_CURRENT_SENSORS, manager.currentSensorsCount());
}

// =====================================================================
// clearCurrentSensors
// =====================================================================

void test_clearCurrentSensors_resets_count_and_forgets_all(void) {
    CurrentSensorsManager manager;
    FakeCurrentSensor s0("SCT013-1");
    manager.addCurrentSensor(&s0);
    TEST_ASSERT_EQUAL_UINT8(1, manager.currentSensorsCount());

    manager.clearCurrentSensors();

    TEST_ASSERT_EQUAL_UINT8(0, manager.currentSensorsCount());
    TEST_ASSERT_NULL(manager.findByName("SCT013-1"));
}

// =====================================================================
// findByName
// =====================================================================

void test_findByName_returns_match_or_nullptr(void) {
    CurrentSensorsManager manager;
    FakeCurrentSensor s0("SCT013-1");
    FakeCurrentSensor s1("SCT013-2");
    manager.addCurrentSensor(&s0);
    manager.addCurrentSensor(&s1);

    TEST_ASSERT_EQUAL_PTR(&s1, manager.findByName("SCT013-2"));
    TEST_ASSERT_NULL(manager.findByName("NOPE"));
}

// =====================================================================
// updateAll
// =====================================================================

void test_updateAll_calls_update_on_every_registered_sensor(void) {
    CurrentSensorsManager manager;
    FakeCurrentSensor s0("SCT013-1");
    FakeCurrentSensor s1("SCT013-2");
    manager.addCurrentSensor(&s0);
    manager.addCurrentSensor(&s1);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(1, s0.updateCalls);
    TEST_ASSERT_EQUAL_UINT8(1, s1.updateCalls);
}

void test_updateAll_does_not_notify_listener_when_sample_invalid(void) {
    CurrentSensorsManager manager;
    RecordingCurrentSensorListener listener;
    manager.setCurrentSensorListener(&listener);

    FakeCurrentSensor s0("SCT013-1");
    s0.valid = false;
    manager.addCurrentSensor(&s0);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(0, listener.sampleCount);
}

void test_updateAll_notifies_listener_once_per_valid_sample(void) {
    CurrentSensorsManager manager;
    RecordingCurrentSensorListener listener;
    manager.setCurrentSensorListener(&listener);

    FakeCurrentSensor s0("SCT013-1");
    s0.valid = true;
    FakeCurrentSensor s1("SCT013-2");
    s1.valid = false;
    manager.addCurrentSensor(&s0);
    manager.addCurrentSensor(&s1);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(1, listener.sampleCount); // solo SCT013-1 (valida)
    TEST_ASSERT_EQUAL_PTR(&s0, listener.lastSample);
}

// =====================================================================
// publishMeasures
// =====================================================================

void test_publishMeasures_includes_every_registered_name_regardless_of_validity(void) {
    CurrentSensorsManager manager;
    FakeCurrentSensor s0("SCT013-1");
    s0.current = 2.5f;
    s0.valid = true;
    FakeCurrentSensor s1("SCT013-2");
    s1.current = -1.0f;
    s1.valid = false; // invalida, igual debe aparecer publicada
    manager.addCurrentSensor(&s0);
    manager.addCurrentSensor(&s1);

    JsonDocument doc;
    manager.publishMeasures(doc);

    TEST_ASSERT_TRUE(doc["SCT013-1"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(2.5f, doc["SCT013-1"].as<float>());
    TEST_ASSERT_TRUE(doc["SCT013-2"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, doc["SCT013-2"].as<float>());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_addCurrentSensor_assigns_sequential_ids_and_calls_begin);
    RUN_TEST(test_addCurrentSensor_rejects_null_without_adding);
    RUN_TEST(test_addCurrentSensor_rejects_when_full_returns_0xFF);

    RUN_TEST(test_clearCurrentSensors_resets_count_and_forgets_all);

    RUN_TEST(test_findByName_returns_match_or_nullptr);

    RUN_TEST(test_updateAll_calls_update_on_every_registered_sensor);
    RUN_TEST(test_updateAll_does_not_notify_listener_when_sample_invalid);
    RUN_TEST(test_updateAll_notifies_listener_once_per_valid_sample);

    RUN_TEST(test_publishMeasures_includes_every_registered_name_regardless_of_validity);

    return UNITY_END();
}
