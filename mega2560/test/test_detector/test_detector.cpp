#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/detector.hpp"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// Buffer — ring buffer de muestras
// =====================================================================

void test_buffer_empty_before_any_sample(void) {
    Buffer buf(3);
    TEST_ASSERT_TRUE(buf.empty());
    TEST_ASSERT_FALSE(buf.full());
    TEST_ASSERT_EQUAL_UINT32(0, buf.getCount());
}

void test_buffer_reports_overwrite_only_once_full(void) {
    Buffer buf(3);
    float overwritten = -1.0f;

    TEST_ASSERT_FALSE(buf.addSample(10.0f, overwritten)); // idx0
    TEST_ASSERT_FALSE(buf.addSample(20.0f, overwritten)); // idx1
    TEST_ASSERT_FALSE(buf.addSample(30.0f, overwritten)); // idx2 -> queda llena
    TEST_ASSERT_TRUE(buf.full());
    TEST_ASSERT_EQUAL_UINT32(3, buf.getCount());

    bool overwrote = buf.addSample(40.0f, overwritten); // pisa la muestra mas vieja (10.0f)
    TEST_ASSERT_TRUE(overwrote);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, overwritten);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, buf.getLastSample());
    TEST_ASSERT_EQUAL_UINT32(3, buf.getCount()); // no crece mas alla de la capacidad
}

void test_buffer_capacity_zero_rejects_samples(void) {
    Buffer buf(0);
    float overwritten = 0.0f;
    TEST_ASSERT_FALSE(buf.isInitialized());
    TEST_ASSERT_FALSE(buf.addSample(1.0f, overwritten));
}

// =====================================================================
// Rule — umbral + cooldown + tope de eventos
// =====================================================================

void test_rule_ignores_values_below_threshold(void) {
    Rule rule;
    rule.threshold = 5;
    TEST_ASSERT_FALSE(rule.evaluate(4, 1));
}

void test_rule_respects_cooldown_between_triggers(void) {
    Rule rule;
    rule.threshold = 5;
    rule.cooldown = 2;

    TEST_ASSERT_TRUE(rule.evaluate(5, 1));   // primer disparo
    TEST_ASSERT_FALSE(rule.evaluate(5, 2));  // todavia en cooldown (2)
    rule.updateCooldown(1);
    TEST_ASSERT_FALSE(rule.evaluate(5, 3));  // cooldown=1, todavia no
    rule.updateCooldown(1);
    TEST_ASSERT_TRUE(rule.evaluate(5, 4));   // cooldown agotado -> dispara de nuevo
    TEST_ASSERT_EQUAL_UINT32(2, rule.getTotalEvents());
}

void test_rule_stops_after_maxEvents(void) {
    Rule rule;
    rule.threshold = 5;
    rule.cooldown = 0;
    rule.maxEvents = 2;

    TEST_ASSERT_TRUE(rule.evaluate(5, 1));
    TEST_ASSERT_TRUE(rule.evaluate(5, 2));
    TEST_ASSERT_FALSE(rule.evaluate(5, 3)); // llego al limite configurado
    TEST_ASSERT_EQUAL_UINT32(2, rule.getTotalEvents());
}

void test_rule_maxEvents_zero_means_unlimited(void) {
    Rule rule;
    rule.threshold = 1;
    rule.cooldown = 0;
    rule.maxEvents = 0;

    for (uint32_t i = 0; i < 50; i++) {
        TEST_ASSERT_TRUE(rule.evaluate(1, i));
    }
    TEST_ASSERT_EQUAL_UINT32(50, rule.getTotalEvents());
}

// =====================================================================
// Source — helper de listener para capturar SourceEvents
// =====================================================================

struct CapturedEvent {
    EventType type;
    uint16_t count;
    uint16_t limit;
};

class RecordingSourceListener : public SourceListener {
  public:
    CapturedEvent events[16];
    uint8_t count = 0;

    void onSourceEvent(SourceEvent& event) override {
        if (count >= 16) return;
        events[count].type = event.type;
        events[count].count = event.count;
        events[count].limit = event.limit;
        count++;
    }
};

SourceConfig makeCemLikeConfig() {
    // Replica lo que Engine::_start() arma hoy para CEM1/TEMP1 (ver
    // engine.hpp ~582-687): solo normalMin/Max y criticalMin/Max. Las Rule
    // (critical/streak/frequency) quedan en su valor por defecto
    // (threshold=0) porque ese camino nunca las toca -- solo lo hace el
    // comando ConfigSource, que el ESP32 todavia no envia.
    SourceConfig config;
    config.bufferSize = 8;
    config.normalMin = 10.0f;
    config.normalMax = 20.0f;
    config.criticalMin = 5.0f;
    config.criticalMax = 25.0f;
    return config;
}

void test_source_with_engine_start_config_never_flags_even_when_critical(void) {
    // Documenta un bug real de integracion: con la config que arma
    // Engine::_start() hoy, las Rule quedan con threshold=0 y
    // Rule::evaluate() con threshold==0 siempre retorna false. Resultado:
    // el Detector nunca dispara onFlag durante un experimento real, aunque
    // el sensor este fuera de rango critico todo el tiempo.
    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(makeCemLikeConfig());

    for (int i = 0; i < 20; i++) {
        source.addSample(100.0f); // muy por encima de criticalMax (25.0f)
    }

    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    TEST_ASSERT_FALSE(source.hasCriticalFlags());
}

void test_source_flags_critical_when_rules_are_configured(void) {
    // Misma senal de entrada que el test anterior, pero con la Rule
    // "critical" configurada como haria el comando ConfigSource. Demuestra
    // que la logica de Source/Detector es correcta: el problema de arriba
    // es de cableado (Engine no llama a esto), no del algoritmo.
    SourceConfig config = makeCemLikeConfig();
    config.critical.threshold = 1; // cualquier muestra critica dispara
    config.critical.cooldown = 0;
    config.critical.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(100.0f); // fuera de [5,25] -> critical

    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Critical), static_cast<int>(listener.events[0].type));
}

void test_source_streak_rule_counts_out_of_normal_in_window(void) {
    SourceConfig config = makeCemLikeConfig();
    config.streak.threshold = 3; // 3 muestras fuera de [normalMin,normalMax] en el buffer
    config.streak.cooldown = 0;
    config.streak.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(22.0f); // fuera de lo normal (>20) pero no critico (<25)
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(23.0f);
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(24.0f); // 3ra muestra fuera de rango normal -> dispara streak
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Streak), static_cast<int>(listener.events[0].type));
}

// =====================================================================
// Detector — orquestacion de sources
// =====================================================================

class RecordingDetectorListener : public DetectorListener {
  public:
    uint8_t flagCount = 0;
    void onFlag(SourceEvent& event) override {
        (void)event;
        flagCount++;
    }
};

void test_detector_addSource_rejects_duplicates(void) {
    Detector detector;
    TEST_ASSERT_TRUE(detector.addSource("CEM1"));
    TEST_ASSERT_FALSE(detector.addSource("CEM1")); // duplicado
    TEST_ASSERT_EQUAL_UINT8(1, detector.getCount());
    TEST_ASSERT_NOT_NULL(detector.findSource("CEM1"));
    TEST_ASSERT_NULL(detector.findSource("TEMP1"));
}

void test_detector_forwards_onFlag_to_its_listener(void) {
    Detector detector;
    RecordingDetectorListener listener;
    detector.setListener(&listener);
    detector.addSource("CEM1");

    SourceConfig config = makeCemLikeConfig();
    config.critical.threshold = 1;
    detector.configureSource("CEM1", config);

    detector.addSample("CEM1", 100.0f); // critical -> onFlag

    TEST_ASSERT_EQUAL_UINT8(1, listener.flagCount);
}

void test_detector_addSample_on_unknown_source_returns_false(void) {
    Detector detector;
    TEST_ASSERT_FALSE(detector.addSample("NOPE", 1.0f));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_buffer_empty_before_any_sample);
    RUN_TEST(test_buffer_reports_overwrite_only_once_full);
    RUN_TEST(test_buffer_capacity_zero_rejects_samples);

    RUN_TEST(test_rule_ignores_values_below_threshold);
    RUN_TEST(test_rule_respects_cooldown_between_triggers);
    RUN_TEST(test_rule_stops_after_maxEvents);
    RUN_TEST(test_rule_maxEvents_zero_means_unlimited);

    RUN_TEST(test_source_with_engine_start_config_never_flags_even_when_critical);
    RUN_TEST(test_source_flags_critical_when_rules_are_configured);
    RUN_TEST(test_source_streak_rule_counts_out_of_normal_in_window);

    RUN_TEST(test_detector_addSource_rejects_duplicates);
    RUN_TEST(test_detector_forwards_onFlag_to_its_listener);
    RUN_TEST(test_detector_addSample_on_unknown_source_returns_false);

    return UNITY_END();
}
