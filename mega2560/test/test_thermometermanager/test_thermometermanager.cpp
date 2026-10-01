#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/thermometermanager.hpp"

using namespace fakeit;

// =====================================================================
// FakeThermometer -- doble de prueba de IThermometer, sin dependencia de
// hardware real (a diferencia de ThermometerDS18B20, que depende de
// DallasTemperature/OneWire y no compila en el entorno native, ver
// CLAUDE.md/tarjeta MOD-005). Deja controlar begin()/update() call counts,
// nombre, temperatura y validez de forma directa desde cada test.
// =====================================================================

class FakeThermometer : public IThermometer {
  public:
    const char* name;
    float temperature = 0.0f;
    bool valid = false;
    uint8_t beginCalls = 0;
    uint8_t updateCalls = 0;

    explicit FakeThermometer(const char* name) : name(name) {}

    void begin() override { beginCalls++; }
    void update() override { updateCalls++; }
    float getTemperature() const override { return temperature; }
    bool isValid() const override { return valid; }
    const char* getName() const override { return name; }
};

class RecordingThermometerListener : public IThermometerListener {
  public:
    IThermometer* lastSample = nullptr;
    uint8_t sampleCount = 0;

    void onThermometerSample(IThermometer* thermometer) override {
        lastSample = thermometer;
        sampleCount++;
    }
};

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// addThermometer
// =====================================================================

void test_addThermometer_assigns_sequential_ids_and_calls_begin(void) {
    ThermometerManager manager;
    FakeThermometer t0("TEMP1");
    FakeThermometer t1("TEMP2");

    TEST_ASSERT_EQUAL_UINT8(0, manager.addThermometer(&t0));
    TEST_ASSERT_EQUAL_UINT8(1, manager.addThermometer(&t1));
    TEST_ASSERT_EQUAL_UINT8(1, t0.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(1, t1.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(2, manager.thermometerCount());
}

void test_addThermometer_rejects_null_without_adding(void) {
    ThermometerManager manager;
    TEST_ASSERT_EQUAL_UINT8(0xFF, manager.addThermometer(nullptr));
    TEST_ASSERT_EQUAL_UINT8(0, manager.thermometerCount());
}

void test_addThermometer_rejects_when_full_returns_0xFF(void) {
    ThermometerManager manager;
    FakeThermometer thermometers[MAX_THERMOMETERS] = {
        FakeThermometer("T0"), FakeThermometer("T1"), FakeThermometer("T2"),
        FakeThermometer("T3"), FakeThermometer("T4"), FakeThermometer("T5"),
        FakeThermometer("T6"), FakeThermometer("T7"), FakeThermometer("T8"),
        FakeThermometer("T9"),
    };
    for (uint8_t i = 0; i < MAX_THERMOMETERS; i++) {
        TEST_ASSERT_EQUAL_UINT8(i, manager.addThermometer(&thermometers[i]));
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_THERMOMETERS, manager.thermometerCount());

    FakeThermometer overflow("OVERFLOW");
    TEST_ASSERT_EQUAL_UINT8(0xFF, manager.addThermometer(&overflow));
    TEST_ASSERT_EQUAL_UINT8(0, overflow.beginCalls); // ni siquiera se inicializa
    TEST_ASSERT_EQUAL_UINT8(MAX_THERMOMETERS, manager.thermometerCount());
}

// =====================================================================
// clearThermometers
// =====================================================================

void test_clearThermometers_resets_count_and_forgets_all(void) {
    ThermometerManager manager;
    FakeThermometer t0("TEMP1");
    manager.addThermometer(&t0);
    TEST_ASSERT_EQUAL_UINT8(1, manager.thermometerCount());

    manager.clearThermometers();

    TEST_ASSERT_EQUAL_UINT8(0, manager.thermometerCount());
    TEST_ASSERT_NULL(manager.findByName("TEMP1"));
}

// =====================================================================
// findByName
// =====================================================================

void test_findByName_returns_match_or_nullptr(void) {
    ThermometerManager manager;
    FakeThermometer t0("TEMP1");
    FakeThermometer t1("TEMP2");
    manager.addThermometer(&t0);
    manager.addThermometer(&t1);

    TEST_ASSERT_EQUAL_PTR(&t1, manager.findByName("TEMP2"));
    TEST_ASSERT_NULL(manager.findByName("NOPE"));
}

// =====================================================================
// updateAll
// =====================================================================

void test_updateAll_calls_update_on_every_registered_thermometer(void) {
    ThermometerManager manager;
    FakeThermometer t0("TEMP1");
    FakeThermometer t1("TEMP2");
    manager.addThermometer(&t0);
    manager.addThermometer(&t1);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(1, t0.updateCalls);
    TEST_ASSERT_EQUAL_UINT8(1, t1.updateCalls);
}

void test_updateAll_does_not_notify_listener_when_sample_invalid(void) {
    ThermometerManager manager;
    RecordingThermometerListener listener;
    manager.setThermometerListener(&listener);

    FakeThermometer t0("TEMP1");
    t0.valid = false;
    manager.addThermometer(&t0);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(0, listener.sampleCount);
}

void test_updateAll_notifies_listener_once_per_valid_sample(void) {
    ThermometerManager manager;
    RecordingThermometerListener listener;
    manager.setThermometerListener(&listener);

    FakeThermometer t0("TEMP1");
    t0.valid = true;
    FakeThermometer t1("TEMP2");
    t1.valid = false;
    manager.addThermometer(&t0);
    manager.addThermometer(&t1);

    manager.updateAll();

    TEST_ASSERT_EQUAL_UINT8(1, listener.sampleCount); // solo TEMP1 (valida)
    TEST_ASSERT_EQUAL_PTR(&t0, listener.lastSample);
}

// =====================================================================
// publishMeasures
// =====================================================================

void test_publishMeasures_includes_every_registered_name_regardless_of_validity(void) {
    ThermometerManager manager;
    FakeThermometer t0("TEMP1");
    t0.temperature = 21.5f;
    t0.valid = true;
    FakeThermometer t1("TEMP2");
    t1.temperature = -1.0f;
    t1.valid = false; // invalida, igual debe aparecer publicada
    manager.addThermometer(&t0);
    manager.addThermometer(&t1);

    JsonDocument doc;
    manager.publishMeasures(doc);

    TEST_ASSERT_TRUE(doc["TEMP1"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(21.5f, doc["TEMP1"].as<float>());
    TEST_ASSERT_TRUE(doc["TEMP2"].is<float>());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, doc["TEMP2"].as<float>());
}

// Engine re-registra el termometro en cada start: uno que ya lee bien no se
// reinicializa (en el DS18B20, begin() re-escanea el bus OneWire).
void test_addThermometer_skips_begin_when_already_valid(void) {
    ThermometerManager manager;
    FakeThermometer t0("TEMP1");
    t0.valid = true;

    manager.addThermometer(&t0);

    TEST_ASSERT_EQUAL_UINT8(0, t0.beginCalls);
    TEST_ASSERT_EQUAL_UINT8(1, manager.thermometerCount());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_addThermometer_assigns_sequential_ids_and_calls_begin);
    RUN_TEST(test_addThermometer_rejects_null_without_adding);
    RUN_TEST(test_addThermometer_skips_begin_when_already_valid);
    RUN_TEST(test_addThermometer_rejects_when_full_returns_0xFF);

    RUN_TEST(test_clearThermometers_resets_count_and_forgets_all);

    RUN_TEST(test_findByName_returns_match_or_nullptr);

    RUN_TEST(test_updateAll_calls_update_on_every_registered_thermometer);
    RUN_TEST(test_updateAll_does_not_notify_listener_when_sample_invalid);
    RUN_TEST(test_updateAll_notifies_listener_once_per_valid_sample);

    RUN_TEST(test_publishMeasures_includes_every_registered_name_regardless_of_validity);

    return UNITY_END();
}
