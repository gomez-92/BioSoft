#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/coilexcitation.hpp"

using namespace fakeit;

// =====================================================================
// CoilExcitation habla con dos cosas: un generador de senal (via
// ISignalGenerator, por eso se puede doblar acá) y un unico pin digital
// que selecciona el modo de experimento. Los tests verifican ambas: que
// el generador reciba las ordenes correctas, y que el pin quede en el
// nivel que corresponde al modo.
//
// El modo NO invierte fase por software -- eso es analogico (op-amp
// inversor + mux, ver docs/adr/001-inversion-de-fase.md). Acá solo se
// verifica que CoilExcitation mueva el selector.
// =====================================================================

namespace {

class FakeSignalGenerator : public ISignalGenerator {
  public:
    uint8_t beginCalls = 0;
    uint8_t offCalls = 0;
    uint8_t sineCalls = 0;
    float lastFrequency = -1.0f;

    void begin() override { beginCalls++; }
    void setSineWave(float freq = 0) override { sineCalls++; lastFrequency = freq; }
    void off() override { offCalls++; }
};

constexpr uint8_t ModePin = 22;

}

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
    When(Method(ArduinoFake(), pinMode)).AlwaysReturn();
    When(Method(ArduinoFake(), digitalWrite)).AlwaysReturn();
}

void tearDown(void) {}

// =====================================================================
// begin()
// =====================================================================

void test_begin_configures_mode_pin_and_starts_generator(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);

    excitation.begin();

    Verify(Method(ArduinoFake(), pinMode).Using(ModePin, OUTPUT)).Once();
    TEST_ASSERT_EQUAL_UINT8(1, generator.beginCalls);
}

// El pin no queda librado a como despierte tras un reset: begin() escribe
// el nivel del modo por defecto.
void test_begin_leaves_mode_pin_in_a_known_state(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);

    excitation.begin();

    Verify(Method(ArduinoFake(), digitalWrite).Using(ModePin, LOW)).AtLeast(1);
}

void test_default_mode_is_field_x(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);

    TEST_ASSERT_TRUE(FieldMode::X == excitation.mode());
}

// =====================================================================
// setMode() -- nivel del selector
// =====================================================================

void test_setMode_null_drives_pin_to_configured_null_level(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();

    TEST_ASSERT_TRUE(excitation.setMode(FieldMode::Null));

    TEST_ASSERT_TRUE(FieldMode::Null == excitation.mode());
    Verify(Method(ArduinoFake(), digitalWrite).Using(ModePin, HIGH)).AtLeast(1);
}

// nullLevel es configurable porque depende de como quede cableado el mux:
// con LOW, los niveles se invierten respecto del test anterior.
void test_setMode_respects_inverted_null_level(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, LOW);
    excitation.begin();

    excitation.setMode(FieldMode::Null);
    Verify(Method(ArduinoFake(), digitalWrite).Using(ModePin, LOW)).AtLeast(1);

    excitation.setMode(FieldMode::X);
    Verify(Method(ArduinoFake(), digitalWrite).Using(ModePin, HIGH)).AtLeast(1);
}

// =====================================================================
// setMode() -- proteccion durante el experimento
//
// Cambiar de campo X a nulo (o al reves) con el experimento en curso
// cambia la condicion experimental de animales que ya estan expuestos.
// CoilExcitation lo rechaza en vez de aplicarlo.
// =====================================================================

void test_setMode_is_rejected_while_running(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();
    excitation.start(50.0f);

    TEST_ASSERT_FALSE(excitation.setMode(FieldMode::Null));
    TEST_ASSERT_TRUE(FieldMode::X == excitation.mode());
}

void test_setMode_works_again_after_stop(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();
    excitation.start(50.0f);
    excitation.stop();

    TEST_ASSERT_TRUE(excitation.setMode(FieldMode::Null));
    TEST_ASSERT_TRUE(FieldMode::Null == excitation.mode());
}

// =====================================================================
// start() / stop()
// =====================================================================

void test_start_sets_sine_wave_at_requested_frequency(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();

    excitation.start(10.0f);

    TEST_ASSERT_EQUAL_UINT8(1, generator.sineCalls);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, generator.lastFrequency);
    TEST_ASSERT_TRUE(excitation.isRunning());
}

void test_stop_turns_generator_off(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();
    excitation.start(50.0f);

    excitation.stop();

    TEST_ASSERT_EQUAL_UINT8(1, generator.offCalls);
    TEST_ASSERT_FALSE(excitation.isRunning());
}

// El modo elegido antes de arrancar sigue vigente durante la corrida: no
// se pisa ni se resetea al llamar start().
void test_start_preserves_the_selected_mode(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();
    excitation.setMode(FieldMode::Null);

    excitation.start(50.0f);

    TEST_ASSERT_TRUE(FieldMode::Null == excitation.mode());
}

void test_isRunning_is_false_before_start(void) {
    FakeSignalGenerator generator;
    CoilExcitation excitation(generator, ModePin, HIGH);
    excitation.begin();

    TEST_ASSERT_FALSE(excitation.isRunning());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_begin_configures_mode_pin_and_starts_generator);
    RUN_TEST(test_begin_leaves_mode_pin_in_a_known_state);
    RUN_TEST(test_default_mode_is_field_x);

    RUN_TEST(test_setMode_null_drives_pin_to_configured_null_level);
    RUN_TEST(test_setMode_respects_inverted_null_level);
    RUN_TEST(test_setMode_is_rejected_while_running);
    RUN_TEST(test_setMode_works_again_after_stop);

    RUN_TEST(test_start_sets_sine_wave_at_requested_frequency);
    RUN_TEST(test_stop_turns_generator_off);
    RUN_TEST(test_start_preserves_the_selected_mode);
    RUN_TEST(test_isRunning_is_false_before_start);

    return UNITY_END();
}
