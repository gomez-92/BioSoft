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
    // Config base solo con los rangos (normalMin/Max, criticalMin/Max).
    // Las Rule (critical/streak/frequency) quedan en su valor por defecto
    // (threshold=0), que por diseno las deja inertes -- sirve como punto
    // de partida neutro para los tests de abajo, que las configuran a
    // mano segun lo que quieren probar. NO representa la config real de
    // TEMP1: para eso ver makeRealTemp1Config(), que espeja lo que arma
    // Engine::_start() hoy (engine.hpp).
    SourceConfig config;
    config.bufferSize = 8;
    config.normalMin = 10.0f;
    config.normalMax = 20.0f;
    config.criticalMin = 5.0f;
    config.criticalMax = 25.0f;
    return config;
}

void test_source_default_rules_never_flag_regardless_of_input(void) {
    // Rule::evaluate() con threshold==0 siempre retorna false (es el
    // valor por defecto de una Rule sin configurar) -- ninguna de las 3
    // reglas deberia disparar nunca, sin importar cuan fuera de rango
    // este la muestra.
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

// =====================================================================
// Source — critical/streak evaluan RACHA CONSECUTIVA, no la ventana del
// buffer (Source::evaluate en detector.hpp le pasa
// getCriticalStreak()/getOutStreak(), no getCriticalCount()/getOutCount()).
// Estos tests prueban explicitamente esa semantica: una interrupcion
// (una sola muestra que no cumple la condicion) tiene que resetear la
// racha a 0, aunque la ventana todavia contenga muestras viejas que si
// cumplian.
// =====================================================================

void test_source_critical_streak_requires_consecutive_samples(void) {
    SourceConfig config = makeCemLikeConfig();
    config.critical.threshold = 3;
    config.critical.cooldown = 0;
    config.critical.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(100.0f); // critical, racha=1
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // critical, racha=2
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(15.0f);  // normal (dentro de [10,20]) -> corta la racha a 0
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // critical, la racha arranca de nuevo en 1
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // critical, racha=2
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);

    // Iban 4 muestras criticas en total (nunca 3 seguidas) -- bajo la
    // vieja semantica de ventana esto ya habria disparado. Con racha
    // consecutiva, todavia no.
    source.addSample(100.0f); // critical, racha=3 -> recien ahora dispara
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Critical), static_cast<int>(listener.events[0].type));
}

void test_source_critical_streak_resets_on_out_of_normal_not_critical(void) {
    // Una muestra "fuera de lo normal pero no critica" tampoco es
    // critica -- tiene que cortar la racha de critical igual que una
    // muestra normal.
    SourceConfig config = makeCemLikeConfig();
    config.critical.threshold = 2;
    config.critical.cooldown = 0;
    config.critical.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(100.0f); // critical, racha=1
    source.addSample(22.0f);  // fuera de normal (>20) pero no critico (<25) -> corta racha
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // critical, racha vuelve a arrancar en 1 (no en 2)
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
}

void test_source_streak_rule_fires_on_consecutive_out_of_normal_samples(void) {
    SourceConfig config = makeCemLikeConfig();
    config.streak.threshold = 3; // 3 muestras SEGUIDAS fuera de [normalMin,normalMax]
    config.streak.cooldown = 0;
    config.streak.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(22.0f); // fuera de lo normal (>20) pero no critico (<25), racha=1
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(23.0f); // racha=2
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(24.0f); // racha=3 -> dispara streak
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Streak), static_cast<int>(listener.events[0].type));
}

void test_source_streak_counts_critical_samples_too(void) {
    // "streak" mide muestras fuera de lo normal en general. "Critico" es
    // un subconjunto MAS SEVERO de "fuera de lo normal" (rangos
    // anidados: criticalMin<=normalMin<normalMax<=criticalMax), no una
    // categoria aparte -- una muestra critica tiene que sumar a la racha
    // de streak igual que una fuera-de-normal-pero-no-critica.
    SourceConfig config = makeCemLikeConfig();
    config.streak.threshold = 3;
    config.streak.cooldown = 0;
    config.streak.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(100.0f); // critica (>25) -> tambien fuera de normal, racha=1
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // critica, racha=2
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(22.0f);  // fuera de normal, no critica -- SUMA a la misma racha
    TEST_ASSERT_EQUAL_UINT8(1, listener.count); // racha=3 -> dispara streak
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Streak), static_cast<int>(listener.events[0].type));
}

void test_source_streak_resets_only_on_fully_normal_sample(void) {
    // La racha de streak NO se corta con una muestra critica (ver test de
    // arriba) -- solo se corta con una muestra realmente dentro de
    // [normalMin, normalMax].
    SourceConfig config = makeCemLikeConfig();
    config.streak.threshold = 2;
    config.streak.cooldown = 0;
    config.streak.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(100.0f); // critica, racha=1
    source.addSample(15.0f);  // normal (dentro de [10,20]) -> SI corta la racha
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(22.0f);  // fuera de normal, la racha arranca de nuevo en 1
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // critica, racha=2 -> dispara
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Streak), static_cast<int>(listener.events[0].type));
}

void test_source_frequency_counts_window_regardless_of_order(void) {
    // A diferencia de critical/streak, "frequency" SI sigue basada en la
    // ventana del buffer -- cuenta muestras "no normales" (criticas o
    // fuera de rango) sin importar si estan seguidas o salteadas.
    SourceConfig config = makeCemLikeConfig();
    config.bufferSize = 6;
    config.frequency.threshold = 3;
    config.frequency.cooldown = 0;
    config.frequency.maxEvents = 0;

    Source source("CEM1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(config);

    source.addSample(15.0f);  // normal
    source.addSample(100.0f); // critical (1 no-normal en ventana)
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(15.0f);  // normal
    source.addSample(22.0f);  // fuera de normal, no critico (2 no-normales en ventana)
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(15.0f);  // normal
    source.addSample(100.0f); // critical (3 no-normales en ventana de 6) -> dispara,
                               // aunque ninguna de las 3 este seguida de otra
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL(static_cast<int>(EventType::Frequency), static_cast<int>(listener.events[0].type));
}

void test_source_zone_classification_matches_normal_critical_boundaries(void) {
    // Tabla de verdad de las 3 zonas (normal=[10,20], critical=[5,25]):
    //   NORMAL       dentro de [normalMin,normalMax]      -> ningun flag
    //   ADVERTENCIA  fuera de normal, dentro de critico    -> streak +
    //                frequency, NO critical
    //   CRITICA      fuera de [criticalMin,criticalMax]    -> los 3 tipos
    //                (critical incluido; la regla critical es mas
    //                estricta -- requiere racha -- pero la muestra en si
    //                cuenta para todos)
    SourceConfig config = makeCemLikeConfig();
    config.critical.threshold = 1;
    config.critical.cooldown = 0;
    config.critical.maxEvents = 0;
    config.streak.threshold = 1;
    config.streak.cooldown = 0;
    config.streak.maxEvents = 0;
    config.frequency.threshold = 1;
    config.frequency.cooldown = 0;
    config.frequency.maxEvents = 0;

    // NORMAL: 15.0 esta dentro de [10,20]
    {
        Source source("CEM1");
        RecordingSourceListener listener;
        source.setListener(&listener);
        source.setConfig(config);
        source.addSample(15.0f);
        TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    }

    // ADVERTENCIA: 22.0 fuera de normal (>20) pero dentro de critico (<25)
    {
        Source source("CEM1");
        RecordingSourceListener listener;
        source.setListener(&listener);
        source.setConfig(config);
        source.addSample(22.0f);

        bool sawCritical = false, sawStreak = false, sawFrequency = false;
        for (uint8_t i = 0; i < listener.count; i++) {
            if (listener.events[i].type == EventType::Critical)  sawCritical = true;
            if (listener.events[i].type == EventType::Streak)    sawStreak = true;
            if (listener.events[i].type == EventType::Frequency) sawFrequency = true;
        }
        TEST_ASSERT_EQUAL_UINT8(2, listener.count); // streak + frequency, nada mas
        TEST_ASSERT_FALSE(sawCritical);
        TEST_ASSERT_TRUE(sawStreak);
        TEST_ASSERT_TRUE(sawFrequency);
    }

    // CRITICA: 100.0 fuera de [5,25]
    {
        Source source("CEM1");
        RecordingSourceListener listener;
        source.setListener(&listener);
        source.setConfig(config);
        source.addSample(100.0f);

        bool sawCritical = false, sawStreak = false, sawFrequency = false;
        for (uint8_t i = 0; i < listener.count; i++) {
            if (listener.events[i].type == EventType::Critical)  sawCritical = true;
            if (listener.events[i].type == EventType::Streak)    sawStreak = true;
            if (listener.events[i].type == EventType::Frequency) sawFrequency = true;
        }
        TEST_ASSERT_EQUAL_UINT8(3, listener.count); // los 3 tipos
        TEST_ASSERT_TRUE(sawCritical);
        TEST_ASSERT_TRUE(sawStreak);
        TEST_ASSERT_TRUE(sawFrequency);
    }
}

// =====================================================================
// Source — configuracion REAL de TEMP1, espeja Engine::_start() (ver
// engine.hpp, seccion "CONFIGURACION SOURCE TEMP"). Si esos numeros
// cambian ahi, este helper y sus tests tienen que actualizarse -- son la
// prueba de que la configuracion que de verdad se manda a la placa hace
// lo que se espera, no solo la logica en abstracto.
// =====================================================================

SourceConfig makeRealTemp1Config() {
    SourceConfig config;
    config.bufferSize = 32; // default de SourceConfig, Engine no lo toca
    config.normalMin = 10.0f;
    config.normalMax = 20.0f;
    config.criticalMin = 5.0f;
    config.criticalMax = 25.0f;

    config.critical.threshold = 3;
    config.critical.cooldown = 1;
    config.critical.maxEvents = 2;

    config.streak.threshold = 5;
    config.streak.cooldown = 4;
    config.streak.maxEvents = 5;

    config.frequency.threshold = 16;
    config.frequency.cooldown = 16;
    config.frequency.maxEvents = 3;

    return config;
}

void test_source_real_temp1_config_cuts_after_second_critical_flag(void) {
    // Con los numeros reales hacen falta 3 muestras criticas SEGUIDAS
    // para el primer flag, y una 4ta seguida para el segundo (el limite
    // configurado, maxEvents=2). Engine::onFlag() corta el experimento
    // cuando event.count >= event.limit -- esta prueba confirma que el
    // Detector llega ahi exactamente en la 4ta muestra critica
    // consecutiva, ni antes ni despues.
    Source source("TEMP1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(makeRealTemp1Config());

    source.addSample(100.0f); // critica (>>criticalMax=25), racha=1
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // racha=2
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    source.addSample(100.0f); // racha=3 -> 1er flag (count=1, limit=2)
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_EQUAL_UINT16(1, listener.events[0].count);
    TEST_ASSERT_EQUAL_UINT16(2, listener.events[0].limit);

    source.addSample(100.0f); // racha=4, cooldown(1) ya paso -> 2do flag
    TEST_ASSERT_EQUAL_UINT8(2, listener.count);
    TEST_ASSERT_EQUAL_UINT16(2, listener.events[1].count);
    TEST_ASSERT_EQUAL_UINT16(2, listener.events[1].limit);

    // count >= limit es exactamente la condicion que usa Engine::onFlag()
    // para decidir cortar el experimento.
    TEST_ASSERT_TRUE(listener.events[1].count >= listener.events[1].limit);
}

void test_source_real_temp1_config_does_not_flag_on_isolated_critical_spikes(void) {
    // Un pico critico aislado (rodeado de muestras normales) nunca junta
    // 3 seguidas -- no tiene que disparar nunca, sin importar cuantas
    // veces se repita el pico.
    Source source("TEMP1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(makeRealTemp1Config());

    for (int i = 0; i < 10; i++) {
        source.addSample(100.0f); // critica, racha=1
        source.addSample(15.0f);  // normal -> corta la racha
    }

    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
}

void test_source_real_temp1_config_critical_samples_also_advance_streak(void) {
    // Con la config real, 3-4 muestras criticas seguidas disparan
    // "critical" (llega a su maxEvents=2 en la 4ta). La MISMA racha de
    // muestras criticas tambien cuenta para "streak" (union, no
    // exclusion mutua) -- en la 5ta muestra critica seguida,
    // streak.threshold=5 se cumple y dispara por su cuenta, aunque
    // critical ya haya llegado a su propio limite.
    Source source("TEMP1");
    RecordingSourceListener listener;
    source.setListener(&listener);
    source.setConfig(makeRealTemp1Config());

    for (int i = 0; i < 5; i++) {
        source.addSample(100.0f); // critica seguida
    }

    uint8_t criticalEvents = 0;
    uint8_t streakEvents = 0;
    for (uint8_t i = 0; i < listener.count; i++) {
        if (listener.events[i].type == EventType::Critical) criticalEvents++;
        else if (listener.events[i].type == EventType::Streak) streakEvents++;
    }

    TEST_ASSERT_EQUAL_UINT8(2, criticalEvents); // llego a su maxEvents en la 4ta muestra
    TEST_ASSERT_EQUAL_UINT8(1, streakEvents);   // recien dispara en la 5ta (su propio threshold)
    TEST_ASSERT_EQUAL_UINT8(3, listener.count);
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

void test_detector_real_temp1_config_reaches_limit_on_fourth_consecutive_critical(void) {
    // Mismo escenario que test_source_real_temp1_config_cuts_after_second_
    // critical_flag, pero pasando por el Detector completo (addSource +
    // configureSource + addSample), igual que lo usa Engine en la placa
    // real -- confirma que el reenvio Detector->listener no pierde nada.
    Detector detector;
    RecordingDetectorListener listener;
    detector.setListener(&listener);
    detector.addSource("TEMP1");
    detector.configureSource("TEMP1", makeRealTemp1Config());

    detector.addSample("TEMP1", 100.0f); // racha=1
    detector.addSample("TEMP1", 100.0f); // racha=2
    detector.addSample("TEMP1", 100.0f); // racha=3 -> 1er flag
    TEST_ASSERT_EQUAL_UINT8(1, listener.flagCount);

    detector.addSample("TEMP1", 100.0f); // racha=4 -> 2do flag (count==limit)
    TEST_ASSERT_EQUAL_UINT8(2, listener.flagCount);
}

void test_detector_addSample_on_unknown_source_returns_false(void) {
    Detector detector;
    TEST_ASSERT_FALSE(detector.addSample("NOPE", 1.0f));
}

// `detector.enabled: false` es el interruptor general de la tarjeta SD: con
// el apagado ninguna fuente puede cortar un experimento, aunque las muestras
// esten muy por encima del critico. Si esto se rompe, una corrida de banco
// con el detector "apagado" se cortaria igual, o -- peor -- uno "encendido"
// dejaria de vigilar.
void test_detector_disabled_never_flags(void) {
    Detector detector;
    RecordingDetectorListener listener;
    detector.setListener(&listener);
    detector.addSource("TEMP1");
    detector.configureSource("TEMP1", makeRealTemp1Config());

    detector.setEnabled(false);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_FALSE(detector.addSample("TEMP1", 100.0f));
    }

    TEST_ASSERT_EQUAL_UINT8(0, listener.flagCount);
    TEST_ASSERT_FALSE(detector.isEnabled());
}

void test_detector_reenabled_keeps_sources_and_config(void) {
    Detector detector;
    RecordingDetectorListener listener;
    detector.setListener(&listener);
    detector.addSource("TEMP1");
    detector.configureSource("TEMP1", makeRealTemp1Config());

    detector.setEnabled(false);
    detector.addSample("TEMP1", 100.0f);
    detector.setEnabled(true);

    // Las muestras de cuando estaba apagado NO cuentan para la racha: hace
    // falta la racha completa de 3 desde que se volvio a encender.
    detector.addSample("TEMP1", 100.0f);
    detector.addSample("TEMP1", 100.0f);
    TEST_ASSERT_EQUAL_UINT8(0, listener.flagCount);
    detector.addSample("TEMP1", 100.0f);
    TEST_ASSERT_EQUAL_UINT8(1, listener.flagCount);
    TEST_ASSERT_EQUAL_UINT8(1, detector.getCount());
}

void test_detector_is_enabled_by_default(void) {
    Detector detector;
    TEST_ASSERT_TRUE(detector.isEnabled());
}

// =====================================================================
// Silencio (tarjeta 25): una fuente vigilada que deja de dar lecturas
// corta. Antes el Detector solo evaluaba muestras que llegaban, y un
// DS18B20 desconectado dejaba correr el experimento hasta "completed".
// =====================================================================

class SilenceListener : public DetectorListener {
  public:
    uint8_t count = 0;
    EventType lastType = EventType::Critical;
    uint16_t lastCount = 0;
    uint16_t lastLimit = 0;
    void onFlag(SourceEvent& event) override {
        count++;
        lastType = event.type;
        lastCount = event.count;
        lastLimit = event.limit;
    }
};

// TEMP1 de fabrica: una lectura cada 5 s, 3 perdidas => 15 s.
static void setUpSilentTemp1(Detector& detector, SilenceListener& listener) {
    detector.setListener(&listener);
    detector.addSource("TEMP1");
    SourceConfig config = makeRealTemp1Config();
    config.sampleIntervalMs = 5000;
    config.maxMissedSamples = 3;
    detector.configureSource("TEMP1", config);
    detector.resetSilence(0);
}

void test_silence_cuts_after_maxMissedSamples_and_not_before(void) {
    Detector detector;
    SilenceListener listener;
    setUpSilentTemp1(detector, listener);
    detector.armSilence();

    detector.checkSilence(14999);
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);

    detector.checkSilence(15000);
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
    TEST_ASSERT_TRUE(listener.lastType == EventType::Silence);
    // Llega ya en su limite: Engine corta con count >= limit.
    TEST_ASSERT_TRUE(listener.lastCount >= listener.lastLimit);
    TEST_ASSERT_EQUAL_UINT16(3, listener.lastLimit);
}

void test_silence_resets_with_every_valid_reading(void) {
    Detector detector;
    SilenceListener listener;
    setUpSilentTemp1(detector, listener);
    detector.armSilence();

    detector.noteAlive("TEMP1", 10000);
    detector.checkSilence(24999);
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
    detector.checkSilence(25000);
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
}

// Durante la estabilizacion no corta, igual que el resto de las reglas;
// pero el reloj corre desde el start, asi que un sensor que nunca dio una
// lectura corta apenas se arma, sin esperar otro limite entero.
void test_silence_does_not_cut_before_being_armed(void) {
    Detector detector;
    SilenceListener listener;
    setUpSilentTemp1(detector, listener);

    detector.checkSilence(60000);
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);

    detector.armSilence();
    detector.checkSilence(60000);
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
}

void test_silence_reports_only_once(void) {
    Detector detector;
    SilenceListener listener;
    setUpSilentTemp1(detector, listener);
    detector.armSilence();

    detector.checkSilence(15000);
    detector.checkSilence(30000);
    detector.checkSilence(90000);
    TEST_ASSERT_EQUAL_UINT8(1, listener.count);
}

void test_silence_respects_the_master_switch(void) {
    Detector detector;
    SilenceListener listener;
    setUpSilentTemp1(detector, listener);
    detector.armSilence();
    detector.setEnabled(false);

    detector.checkSilence(60000);
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
}

// CEM1: 3 x 500 ms = 1.5 s, pero el piso es 5 s -- el DS18B20 bloquea
// ~750 ms por medicion y un tick lento no puede contar como sensor caido.
void test_silence_limit_has_a_floor(void) {
    SourceConfig config;
    config.maxMissedSamples = 3;
    config.sampleIntervalMs = 500;
    TEST_ASSERT_EQUAL_UINT32(5000, Source::silenceLimitMs(config));
    config.sampleIntervalMs = 5000;
    TEST_ASSERT_EQUAL_UINT32(15000, Source::silenceLimitMs(config));
}

// Sin cadencia (una Source configurada sin pasar por Engine) no hay corte.
void test_silence_is_off_without_interval(void) {
    SourceConfig config;
    config.maxMissedSamples = 3;
    config.sampleIntervalMs = 0;
    TEST_ASSERT_EQUAL_UINT32(0, Source::silenceLimitMs(config));
}

// Reconfigurar la fuente (cada start) desarma el silencio: la corrida
// nueva no puede heredar el armado de la anterior.
void test_reconfiguring_disarms_silence(void) {
    Detector detector;
    SilenceListener listener;
    setUpSilentTemp1(detector, listener);
    detector.armSilence();

    SourceConfig config = makeRealTemp1Config();
    config.sampleIntervalMs = 5000;
    detector.configureSource("TEMP1", config);
    detector.checkSilence(60000);
    TEST_ASSERT_EQUAL_UINT8(0, listener.count);
}

// CEM1 caido con el lazo activo: corta aunque CEM1 no este vigilada y
// aunque el Detector este apagado (no recibe un Detector: no depende de el).
void test_control_silence_cuts_cem1_even_unwatched(void) {
    SourceConfig cem;                  // maxMissedSamples = 3 por defecto
    cem.sampleIntervalMs = 500;        // -> piso de 5 s

    TEST_ASSERT_FALSE(controlSilenceExpired(true, false, 4999, 0, cem));
    TEST_ASSERT_TRUE(controlSilenceExpired(true, false, 5000, 0, cem));
}

void test_control_silence_needs_the_loop_active_and_settling_over(void) {
    SourceConfig cem;
    cem.sampleIntervalMs = 500;

    // "Solo sensado" (control.enabled = false): no hay bobinas a ciegas.
    TEST_ASSERT_FALSE(controlSilenceExpired(false, false, 60000, 0, cem));
    // Durante la estabilizacion tampoco, igual que el resto.
    TEST_ASSERT_FALSE(controlSilenceExpired(true, true, 60000, 0, cem));
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

    RUN_TEST(test_source_default_rules_never_flag_regardless_of_input);
    RUN_TEST(test_source_flags_critical_when_rules_are_configured);

    RUN_TEST(test_source_critical_streak_requires_consecutive_samples);
    RUN_TEST(test_source_critical_streak_resets_on_out_of_normal_not_critical);
    RUN_TEST(test_source_streak_rule_fires_on_consecutive_out_of_normal_samples);
    RUN_TEST(test_source_streak_counts_critical_samples_too);
    RUN_TEST(test_source_streak_resets_only_on_fully_normal_sample);
    RUN_TEST(test_source_frequency_counts_window_regardless_of_order);
    RUN_TEST(test_source_zone_classification_matches_normal_critical_boundaries);

    RUN_TEST(test_source_real_temp1_config_cuts_after_second_critical_flag);
    RUN_TEST(test_source_real_temp1_config_does_not_flag_on_isolated_critical_spikes);
    RUN_TEST(test_source_real_temp1_config_critical_samples_also_advance_streak);

    RUN_TEST(test_detector_addSource_rejects_duplicates);
    RUN_TEST(test_detector_forwards_onFlag_to_its_listener);
    RUN_TEST(test_detector_real_temp1_config_reaches_limit_on_fourth_consecutive_critical);
    RUN_TEST(test_detector_addSample_on_unknown_source_returns_false);
    RUN_TEST(test_detector_disabled_never_flags);
    RUN_TEST(test_detector_reenabled_keeps_sources_and_config);
    RUN_TEST(test_detector_is_enabled_by_default);
    RUN_TEST(test_silence_cuts_after_maxMissedSamples_and_not_before);
    RUN_TEST(test_silence_resets_with_every_valid_reading);
    RUN_TEST(test_silence_does_not_cut_before_being_armed);
    RUN_TEST(test_silence_reports_only_once);
    RUN_TEST(test_silence_respects_the_master_switch);
    RUN_TEST(test_silence_limit_has_a_floor);
    RUN_TEST(test_silence_is_off_without_interval);
    RUN_TEST(test_reconfiguring_disarms_silence);
    RUN_TEST(test_control_silence_cuts_cem1_even_unwatched);
    RUN_TEST(test_control_silence_needs_the_loop_active_and_settling_over);

    return UNITY_END();
}
