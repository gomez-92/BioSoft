#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/magnetometermanager.hpp"

using namespace fakeit;

// =====================================================================
// FakeMagnetometer -- doble de prueba de IMagnetometer, sin dependencia de
// hardware real (a diferencia de MagnetometerMlx90393, que depende de
// Adafruit_MLX90393/Wire y no compila en el entorno native, ver
// CLAUDE.md/tarjeta MOD-007). Deja controlar begin()/update() call counts,
// nombre, campo magnetico y validez de forma directa desde cada test.
// =====================================================================

class FakeMagnetometer : public IMagnetometer {
  public:
    const char* name;
    float magneticField = 0.0f;
    bool valid = false;
    uint8_t beginCalls = 0;
    uint8_t updateCalls = 0;

    explicit FakeMagnetometer(const char* name) : name(name) {}

    void begin() override { beginCalls++; }
    void update() override { updateCalls++; }
    float getMagneticField() const override { return magneticField; }
    bool isValid() const override { return valid; }
    const char* getName() const override { return name; }
};

class RecordingMagnetometerListener : public IMagnetometerListener {
  public:
    IMagnetometer* lastSample = nullptr;
    uint8_t sampleCount = 0;

    void onMagnetometerSample(IMagnetometer* magnetometer) override {
        lastSample = magnetometer;
        sampleCount++;
    }
};

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// addMagnetometer
// =====================================================================

void test_addMagnetometer_assigns_sequential_ids_and_calls_begin(void) {
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    FakeMagnetometer m1("CEM2");

    TEST_ASSERT_EQUAL_UINT8(0, manager.addMagnetometer(&m0));
    TEST_ASSERT_EQUAL_UINT8(1, manager.addMagnetometer(&m1));
    TEST_ASSERT_EQUAL_UINT8(1, m0.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(1, m1.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(2, manager.magnetometerCount());
}

void test_addMagnetometer_skips_begin_when_already_valid(void) {
    // Regresion bring-up INT-001 (2026-09-01):
    // Engine::_applySourceSettings() hace clearMagnetometers() +
    // addMagnetometer() en cada "start". Con begin()
    // incondicional, eso reinicializaba MagnetometerMlx90393 en caliente en
    // cada experimento y fallaba en banco aunque el sensor ya funcionara
    // desde el boot. addMagnetometer() ahora saltea begin() si el
    // magnetometro ya reporta isValid()==true.
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    m0.valid = true;

    manager.addMagnetometer(&m0);

    TEST_ASSERT_EQUAL_UINT8(0, m0.beginCalls);
}

void test_addMagnetometer_calls_begin_when_invalid(void) {
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    m0.valid = false;

    manager.addMagnetometer(&m0);

    TEST_ASSERT_EQUAL_UINT8(1, m0.beginCalls);
}

void test_addMagnetometer_rejects_null_without_adding(void) {
    MagnetometerManager manager;
    TEST_ASSERT_EQUAL_UINT8(0xFF, manager.addMagnetometer(nullptr));
    TEST_ASSERT_EQUAL_UINT8(0, manager.magnetometerCount());
}

void test_addMagnetometer_rejects_when_full_returns_0xFF(void) {
    MagnetometerManager manager;
    FakeMagnetometer magnetometers[MAX_MAGNETOMETERS] = {
        FakeMagnetometer("M0"), FakeMagnetometer("M1"), FakeMagnetometer("M2"),
        FakeMagnetometer("M3"), FakeMagnetometer("M4"), FakeMagnetometer("M5"),
        FakeMagnetometer("M6"), FakeMagnetometer("M7"), FakeMagnetometer("M8"),
        FakeMagnetometer("M9"),
    };
    for (uint8_t i = 0; i < MAX_MAGNETOMETERS; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, manager.addMagnetometer(&magnetometers[i]));
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_MAGNETOMETERS, manager.magnetometerCount());

    FakeMagnetometer overflow("OVERFLOW");
    TEST_ASSERT_EQUAL_UINT8(0xFF, manager.addMagnetometer(&overflow));
    TEST_ASSERT_EQUAL_UINT8(0, overflow.beginCalls); // ni siquiera se inicializa
    TEST_ASSERT_EQUAL_UINT8(MAX_MAGNETOMETERS, manager.magnetometerCount());
}

// =====================================================================
// clearMagnetometers
// =====================================================================

void test_clearMagnetometers_resets_count_and_forgets_all(void) {
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    manager.addMagnetometer(&m0);
    TEST_ASSERT_EQUAL_UINT8(1, manager.magnetometerCount());

    manager.clearMagnetometers();

    TEST_ASSERT_EQUAL_UINT8(0, manager.magnetometerCount());
    TEST_ASSERT_NULL(manager.findByName("CEM1"));
}

// =====================================================================
// findByName
// =====================================================================

void test_findByName_returns_match_or_nullptr(void) {
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    FakeMagnetometer m1("CEM2");
    manager.addMagnetometer(&m0);
    manager.addMagnetometer(&m1);

    TEST_ASSERT_EQUAL_PTR(&m1, manager.findByName("CEM2"));
    TEST_ASSERT_NULL(manager.findByName("NOPE"));
}

// =====================================================================
// updateAll
// =====================================================================

void test_updateAll_calls_update_on_every_registered_magnetometer(void) {
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    FakeMagnetometer m1("CEM2");
    manager.addMagnetometer(&m0);
    manager.addMagnetometer(&m1);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(1, m0.updateCalls);
    TEST_ASSERT_EQUAL_UINT8(1, m1.updateCalls);
}

void test_updateAll_does_not_notify_listener_when_sample_invalid(void) {
    MagnetometerManager manager;
    RecordingMagnetometerListener listener;
    manager.setMagnetometerListener(&listener);

    FakeMagnetometer m0("CEM1");
    m0.valid = false;
    manager.addMagnetometer(&m0);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(0, listener.sampleCount);
}

void test_updateAll_notifies_listener_once_per_valid_sample(void) {
    MagnetometerManager manager;
    RecordingMagnetometerListener listener;
    manager.setMagnetometerListener(&listener);

    FakeMagnetometer m0("CEM1");
    m0.valid = true;
    FakeMagnetometer m1("CEM2");
    m1.valid = false;
    manager.addMagnetometer(&m0);
    manager.addMagnetometer(&m1);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(1, listener.sampleCount); // solo CEM1 (valida)
    TEST_ASSERT_EQUAL_PTR(&m0, listener.lastSample);
}

// =====================================================================
// publishMeasures
// =====================================================================

void test_publishMeasures_includes_every_registered_name_regardless_of_validity(void) {
    MagnetometerManager manager;
    FakeMagnetometer m0("CEM1");
    m0.magneticField = 1.25f;
    m0.valid = true;
    FakeMagnetometer m1("CEM2");
    m1.magneticField = -1.0f;
    m1.valid = false; // invalida, igual debe aparecer publicada
    manager.addMagnetometer(&m0);
    manager.addMagnetometer(&m1);

    JsonDocument doc;
    manager.publishMeasures(doc);

    TEST_ASSERT_TRUE(doc["CEM1"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(1.25f, doc["CEM1"].as<float>());
    TEST_ASSERT_TRUE(doc["CEM2"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, doc["CEM2"].as<float>());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_addMagnetometer_assigns_sequential_ids_and_calls_begin);
    RUN_TEST(test_addMagnetometer_skips_begin_when_already_valid);
    RUN_TEST(test_addMagnetometer_calls_begin_when_invalid);
    RUN_TEST(test_addMagnetometer_rejects_null_without_adding);
    RUN_TEST(test_addMagnetometer_rejects_when_full_returns_0xFF);

    RUN_TEST(test_clearMagnetometers_resets_count_and_forgets_all);

    RUN_TEST(test_findByName_returns_match_or_nullptr);

    RUN_TEST(test_updateAll_calls_update_on_every_registered_magnetometer);
    RUN_TEST(test_updateAll_does_not_notify_listener_when_sample_invalid);
    RUN_TEST(test_updateAll_notifies_listener_once_per_valid_sample);

    RUN_TEST(test_publishMeasures_includes_every_registered_name_regardless_of_validity);

    return UNITY_END();
}
