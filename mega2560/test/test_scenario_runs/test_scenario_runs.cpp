#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "../support/arduino_fakes.hpp"
#include "../../src/scenariosensors.hpp"
#include "../../src/detector.hpp"
#include "../../src/detectorconfigbuilder.hpp"
#include "../../src/fieldcontroller.hpp"
#include "../../src/safetymargins.hpp"

using namespace fakeit;

// Corridas completas de los perfiles de escenario (tools/escenarios.json).
//
// Cada perfil promete un final ("corta por critico de TEMP1 a los ~75 s",
// "termina completed"). Hasta ahora esa promesa era texto en el generador y
// nadie la comprobaba: "Campo inestable" cortaba por critico a los 16 s
// porque el lazo arrancaba de duty 0, y su oscilacion de 4 s desaparecia
// muestreada cada 2 s. Aca cada perfil corre contra el Detector, el
// FieldController y los sensores de escenario REALES, con la misma
// secuencia que Engine (estabilizacion, cadencias, corte cuando
// count >= limit, silencio), y con varias combinaciones de rangos de los
// menus y de fase de muestreo: los niveles prometen el mismo final con
// cualquier rango, y eso es lo que se verifica.
//
// El simulador del generador (fase 2) reproduce esta misma corrida en JS y
// se compara contra el mismo archivo: si uno de los dos cambia, falla.
//
// Lo que NO reproduce: el Timer real (las tareas se modelan como periodos
// exactos con un desfase) y el envio por serie.

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// ---------------------------------------------------------------------
// Reloj y duty compartidos con los fakes
// ---------------------------------------------------------------------

static unsigned long nowMs = 0;
static float commonDuty = 0.0f;
static float currentDuty() { return commonDuty; }

// ---------------------------------------------------------------------
// Lectura de tools/escenarios.json
// ---------------------------------------------------------------------

static std::string profilesPath() {
    // __FILE__ = .../mega2560/test/test_scenario_runs/test_scenario_runs.cpp
    // (o relativo al proyecto, segun como lo invoque pio).
    std::string here = __FILE__;
    size_t cut = here.find_last_of("/\\");
    std::string dir = (cut == std::string::npos) ? std::string(".") : here.substr(0, cut);
    return dir + "/../../../tools/escenarios.json";
}

static bool loadProfiles(JsonDocument& doc) {
    std::string path = profilesPath();
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::string text;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    return !deserializeJson(doc, text);
}

static ScenarioSignal signalOf(JsonObjectConst sc) {
    ScenarioSignal s;
    float flat[2 * Scenario::MaxPoints];
    uint8_t length = 0;
    for (JsonArrayConst pt : sc["points"].as<JsonArrayConst>()) {
        TEST_ASSERT_TRUE_MESSAGE(length + 2 <= 2 * Scenario::MaxPoints, "perfil con mas de 8 puntos");
        flat[length++] = pt[0].as<float>();
        flat[length++] = pt[1].as<float>();
    }
    TEST_ASSERT_TRUE_MESSAGE(Scenario::PointsRefusal::None == Scenario::applyPoints(s, flat, length),
                             "perfil con puntos que el Mega rechazaria");
    s.noise = sc["noise"] | 0.0f;
    s.oscAmp = sc["oscAmp"] | 0.0f;
    s.oscPeriod = sc["oscPeriod"] | 0.0f;
    s.dropAt = sc["dropAt"] | 0.0f;
    s.setpointDuty = sc["setpointDuty"] | 0.0f;
    return s;
}

// ---------------------------------------------------------------------
// La corrida
// ---------------------------------------------------------------------

struct Ranges {
    float intensity;
    int tolerance;
    float tempNormalMin, tempNormalMax, tempCriticalMin, tempCriticalMax;
};

struct Outcome {
    bool cut = false;
    float at = 0.0f;
    std::string source;
    std::string type;
    bool sawStreak = false;
    bool sawFrequency = false;
    bool sawCritical = false;
};

static const char* typeName(EventType t) {
    switch (t) {
        case EventType::Critical: return "critical";
        case EventType::Streak: return "streak";
        case EventType::Frequency: return "frequency";
        case EventType::Silence: return "silence";
    }
    return "?";
}

class CutListener : public DetectorListener {
  public:
    Outcome outcome;
    void onFlag(SourceEvent& event) override {
        if (outcome.cut) return;
        if (event.type == EventType::Streak) outcome.sawStreak = true;
        if (event.type == EventType::Frequency) outcome.sawFrequency = true;
        if (event.type == EventType::Critical) outcome.sawCritical = true;
        // Engine::onFlag: corta cuando el evento llega al tope.
        if (event.count >= event.limit) {
            outcome.cut = true;
            outcome.at = nowMs / 1000.0f;
            outcome.source = event.source->getName();
            outcome.type = typeName(event.type);
        }
    }
};

static constexpr unsigned long CemIntervalMs = 2000;    // Intervals::MeasureMagneticField
static constexpr unsigned long TempIntervalMs = 5000;   // Intervals::MeasureTemperature
static constexpr unsigned long SettlingMs = 10000;      // Intervals::SettlingTime
static constexpr unsigned long StepMs = 50;

static Outcome run(JsonObjectConst profile, const Ranges& r, unsigned long cemPhaseMs, unsigned long tempPhaseMs) {
    JsonObjectConst cemJ = profile["sources"]["CEM1"];
    JsonObjectConst tempJ = profile["sources"]["TEMP1"];
    ScenarioSignal cemSignal = signalOf(cemJ["scenario"]);
    ScenarioSignal tempSignal = signalOf(tempJ["scenario"]);

    nowMs = 0;
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() -> unsigned long { return nowMs; });

    // Rangos, igual que el handler de Start.
    SourceConfig cemConfig = DetectorConfigBuilder::defaultConfig();
    cemConfig.sampleIntervalMs = CemIntervalMs;
    DetectorConfigBuilder::applyCemRanges(cemConfig, r.intensity, r.tolerance, SafetyMargins::CemCriticalMultiplier);
    SourceConfig tempConfig = DetectorConfigBuilder::defaultConfig();
    tempConfig.sampleIntervalMs = TempIntervalMs;
    DetectorConfigBuilder::applyTempRanges(tempConfig, r.tempNormalMin, r.tempNormalMax, r.tempCriticalMin, r.tempCriticalMax);

    Detector detector;
    CutListener listener;
    detector.setListener(&listener);
    bool cemWatched = cemJ["enabled"] | false;
    bool tempWatched = tempJ["enabled"] | false;
    if (cemWatched) { detector.addSource("CEM1"); detector.configureSource("CEM1", cemConfig); }
    if (tempWatched) { detector.addSource("TEMP1"); detector.configureSource("TEMP1", tempConfig); }

    MagnetometerScenario cem("CEM1", currentDuty);
    cem.setSignal(cemSignal);
    cem.setRanges({cemConfig.normalMin, cemConfig.normalMax, cemConfig.criticalMin, cemConfig.criticalMax});
    cem.begin();
    ThermometerScenario temp("TEMP1");
    temp.setSignal(tempSignal);
    temp.setRanges({tempConfig.normalMin, tempConfig.normalMax, tempConfig.criticalMin, tempConfig.criticalMax});
    temp.begin();

    // Engine::_start(): campo X, mapa vacio -> duty de partida de la planta
    // (Engine::_scenarioStartDuty).
    FieldController controller(makeFieldControllerConfig(CemIntervalMs));
    controller.reset();
    controller.setSetpoint(r.intensity);
    controller.preload(cemSignal.setpointDuty);
    commonDuty = controller.getOutput();
    Scenario::restart(0);
    detector.resetSilence(0);

    // Corte por silencio del LAZO (Engine::_checkControlSilence): con el lazo
    // activo, CEM1 sin lecturas corta aunque no este vigilada ni haya
    // Detector. Mismo reloj que Engine: desde el start y en cada lectura
    // valida del magnetometro.
    unsigned long lastMagnetometerAliveMs = 0;
    SourceConfig cemTemplate = DetectorConfigBuilder::defaultConfig();
    cemTemplate.sampleIntervalMs = CemIntervalMs;

    bool settling = true;
    unsigned long nextCem = CemIntervalMs + cemPhaseMs;
    unsigned long nextTemp = TempIntervalMs + tempPhaseMs;
    unsigned long durationMs = (unsigned long)(profile["minDuration"].as<float>() * 1000.0f) + 120000UL;

    for (nowMs = 0; nowMs <= durationMs && !listener.outcome.cut; nowMs += StepMs) {
        if (settling && nowMs >= SettlingMs) {
            settling = false;
            detector.armSilence();
        }
        if (nowMs >= nextCem) {
            nextCem += CemIntervalMs;
            cem.update();
            if (cem.isValid()) {
                lastMagnetometerAliveMs = nowMs;
                detector.noteAlive("CEM1", nowMs);
                controller.update(cem.getMagneticField());
                commonDuty = controller.getOutput();
                if (!settling) detector.addSample("CEM1", cem.getMagneticField());
            }
        }
        if (nowMs >= nextTemp) {
            nextTemp += TempIntervalMs;
            temp.update();
            if (temp.isValid()) {
                detector.noteAlive("TEMP1", nowMs);
                if (!settling) detector.addSample("TEMP1", temp.getTemperature());
            }
        }
        detector.checkSilence(nowMs);
        if (!listener.outcome.cut &&
            controlSilenceExpired(true, settling, nowMs, lastMagnetometerAliveMs, cemTemplate)) {
            listener.outcome.cut = true;
            listener.outcome.at = nowMs / 1000.0f;
            listener.outcome.source = "CEM1";
            listener.outcome.type = "silence";
        }
    }
    return listener.outcome;
}

// ---------------------------------------------------------------------
// Las combinaciones: los menus de fabrica (docs/config.example.json) y tres
// fases de muestreo, escalonadas para que CEM1 y TEMP1 no coincidan.
// ---------------------------------------------------------------------

static const Ranges Combos[] = {
    {1.0f, 5,  30, 40, 25, 45},
    {1.0f, 10, 28, 42, 23, 47},
    {2.0f, 5,  26, 44, 20, 50},
    {2.0f, 10, 30, 40, 20, 50},
    {1.0f, 5,  26, 44, 25, 45},
};
static const unsigned long Phases[][2] = { {250, 1100}, {900, 2600}, {1650, 4300} };

static constexpr float AtToleranceSeconds = 15.0f;

static void checkProfile(JsonObjectConst profile) {
    const char* key = profile["key"];
    JsonObjectConst expect = profile["expect"];
    const char* reason = expect["reason"];
    char msg[200];

    for (const Ranges& r : Combos) {
        for (const auto& ph : Phases) {
            Outcome o = run(profile, r, ph[0], ph[1]);
            snprintf(msg, sizeof(msg), "%s (%.0f mT %d%%, temp %.0f~%.0f/%.0f~%.0f, fase %lu/%lu): %s %s %s a los %.1f s",
                     key, r.intensity, r.tolerance, r.tempNormalMin, r.tempNormalMax, r.tempCriticalMin,
                     r.tempCriticalMax, ph[0], ph[1], o.cut ? "corto" : "completo",
                     o.source.c_str(), o.type.c_str(), o.at);

            if (strcmp(reason, "completed") == 0) {
                TEST_ASSERT_FALSE_MESSAGE(o.cut, msg);
                for (JsonVariantConst a : expect["alerts"].as<JsonArrayConst>()) {
                    if (strcmp(a.as<const char*>(), "streak") == 0) TEST_ASSERT_TRUE_MESSAGE(o.sawStreak, msg);
                    if (strcmp(a.as<const char*>(), "frequency") == 0) TEST_ASSERT_TRUE_MESSAGE(o.sawFrequency, msg);
                }
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(o.cut, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expect["source"].as<const char*>(), o.source.c_str(), msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expect["type"].as<const char*>(), o.type.c_str(), msg);
            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(AtToleranceSeconds, expect["at"].as<float>(), o.at, msg);
            // Y el corte tiene que llegar dentro de la duracion minima que
            // el perfil le pide al operador.
            TEST_ASSERT_TRUE_MESSAGE(o.at <= profile["minDuration"].as<float>(), msg);
        }
    }
}

static JsonDocument profilesDoc;

static JsonObjectConst profileByKey(const char* key) {
    for (JsonObjectConst p : profilesDoc["profiles"].as<JsonArrayConst>()) {
        if (strcmp(p["key"] | "", key) == 0) return p;
    }
    TEST_FAIL_MESSAGE(key);
    return JsonObjectConst();
}

void test_profiles_file_is_readable(void) {
    TEST_ASSERT_TRUE_MESSAGE(profilesDoc["profiles"].size() > 0, profilesPath().c_str());
}

// Cada perfil del archivo tiene que estar en la lista de abajo: un perfil
// nuevo sin test seria una promesa sin comprobar.
void test_every_profile_is_checked(void);

#define PROFILE_TEST(KEY) void test_profile_##KEY(void) { checkProfile(profileByKey(#KEY)); }
PROFILE_TEST(normal)
PROFILE_TEST(tempAlta)
PROFILE_TEST(tempSube)
PROFILE_TEST(tempRecupera)
PROFILE_TEST(tempAdvertencia)
PROFILE_TEST(campoCompensado)
PROFILE_TEST(campoInestable)
PROFILE_TEST(campoFuera)
PROFILE_TEST(sensorCaido)
PROFILE_TEST(tempBaja)
PROFILE_TEST(campoCaido)

static const char* const CheckedProfiles[] = {
    "normal", "tempAlta", "tempSube", "tempRecupera", "tempAdvertencia",
    "campoCompensado", "campoInestable", "campoFuera", "sensorCaido",
    "tempBaja", "campoCaido",
};

void test_every_profile_is_checked(void) {
    for (JsonObjectConst p : profilesDoc["profiles"].as<JsonArrayConst>()) {
        bool found = false;
        for (const char* k : CheckedProfiles) found = found || strcmp(k, p["key"] | "") == 0;
        TEST_ASSERT_TRUE_MESSAGE(found, p["key"] | "(sin key)");
    }
}

// El caso que motivo todo esto, fijado aparte: sin el duty de partida de la
// planta, un escenario que vigila CEM1 corta a los pocos segundos de la
// estabilizacion por el arranque del lazo, no por lo que el escenario dice.
void test_plant_starting_at_zero_duty_cuts_early(void) {
    JsonObjectConst normal = profileByKey("normal");
    JsonDocument copy;
    copy.set(normal);
    copy["sources"]["CEM1"]["scenario"]["setpointDuty"] = 0.5f;
    // Simula el firmware viejo: preload(0) en vez del duty de la planta.
    ScenarioSignal s = signalOf(copy["sources"]["CEM1"]["scenario"]);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, s.setpointDuty);
    FieldController controller(makeFieldControllerConfig(CemIntervalMs));
    controller.reset();
    controller.setSetpoint(1.0f);
    controller.preload(0.0f);
    // A los 10 s (5 actualizaciones) con duty 0 de partida y maxStep 0.05,
    // el duty no puede pasar de 0.25: la mitad del campo objetivo.
    commonDuty = 0.0f;
    MagnetometerScenario cem("CEM1", currentDuty);
    ScenarioRanges r = {0.95f, 1.05f, 0.9375f, 1.0625f};
    cem.setSignal(s);
    cem.setRanges(r);
    cem.begin();
    nowMs = 0;
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() -> unsigned long { return nowMs; });
    Scenario::restart(0);
    for (int i = 1; i <= 5; i++) {
        nowMs = i * CemIntervalMs;
        cem.update();
        controller.update(cem.getMagneticField());
        commonDuty = controller.getOutput();
    }
    TEST_ASSERT_TRUE(commonDuty <= 0.26f);
    TEST_ASSERT_TRUE(cem.getMagneticField() < r.criticalMin);
}

int main(int argc, char** argv) {
    bool loaded = loadProfiles(profilesDoc);
    UNITY_BEGIN();
    RUN_TEST(test_profiles_file_is_readable);
    if (loaded) {
        RUN_TEST(test_every_profile_is_checked);
        RUN_TEST(test_profile_normal);
        RUN_TEST(test_profile_tempAlta);
        RUN_TEST(test_profile_tempSube);
        RUN_TEST(test_profile_tempRecupera);
        RUN_TEST(test_profile_tempAdvertencia);
        RUN_TEST(test_profile_campoCompensado);
        RUN_TEST(test_profile_campoInestable);
        RUN_TEST(test_profile_campoFuera);
        RUN_TEST(test_profile_sensorCaido);
        RUN_TEST(test_profile_tempBaja);
        RUN_TEST(test_profile_campoCaido);
        RUN_TEST(test_plant_starting_at_zero_duty_cuts_early);
    }
    return UNITY_END();
}
