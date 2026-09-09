#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/mainpowerswitch.hpp"

using namespace fakeit;

// =====================================================================
// Relay/MainPowerSwitch tocan pinMode/digitalWrite/millis directamente (no
// hay interfaz de por medio, a diferencia de los managers de sensores).
// fakeMillis simula el reloj: cada test lo mueve a mano para ejercitar el
// blanking de 200ms sin depender de tiempo real.
// =====================================================================

namespace {
    unsigned long fakeMillis = 0;
}

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
    fakeMillis = 0;
    When(Method(ArduinoFake(), pinMode)).AlwaysReturn();
    When(Method(ArduinoFake(), digitalWrite)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() -> unsigned long { return fakeMillis; });
}

void tearDown(void) {}

// =====================================================================
// begin()
// =====================================================================

void test_begin_configures_output_pin(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    relay.begin();

    Verify(Method(ArduinoFake(), pinMode).Using(7, OUTPUT)).Once();
}

void test_begin_opens_relay_active_high_even_at_time_zero(void) {
    // Regresion: con _lastChange inicial en 0, begin() llamado dentro de
    // los primeros 200ms de millis() no debe quedar bloqueado por el
    // blanking -- debe garantizar OFF igual.
    fakeMillis = 0;
    Relay relay(7, Relay::ACTIVE_HIGH);
    relay.begin();

    Verify(Method(ArduinoFake(), digitalWrite).Using(7, LOW)).Once();
    TEST_ASSERT_TRUE(relay.isOpen());
    TEST_ASSERT_FALSE(relay.isClosed());
}

void test_begin_opens_relay_active_low_even_at_time_zero(void) {
    fakeMillis = 0;
    Relay relay(8, Relay::ACTIVE_LOW);
    relay.begin();

    Verify(Method(ArduinoFake(), digitalWrite).Using(8, HIGH)).Once();
    TEST_ASSERT_TRUE(relay.isOpen());
    TEST_ASSERT_FALSE(relay.isClosed());
}

// =====================================================================
// open()/close()/toggle() -- nivel fisico segun ActivationType
// =====================================================================

void test_close_applies_high_for_active_high(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    fakeMillis = 0;
    relay.begin();

    fakeMillis = 200;
    relay.close();

    Verify(Method(ArduinoFake(), digitalWrite).Using(7, HIGH)).Once();
    TEST_ASSERT_TRUE(relay.isClosed());
}

void test_close_applies_low_for_active_low(void) {
    Relay relay(8, Relay::ACTIVE_LOW);
    fakeMillis = 0;
    relay.begin();

    fakeMillis = 200;
    relay.close();

    Verify(Method(ArduinoFake(), digitalWrite).Using(8, LOW)).Once();
    TEST_ASSERT_TRUE(relay.isClosed());
}

void test_toggle_switches_state(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    fakeMillis = 0;
    relay.begin(); // OFF

    fakeMillis = 200;
    relay.toggle();
    TEST_ASSERT_TRUE(relay.isClosed());

    fakeMillis = 400;
    relay.toggle();
    TEST_ASSERT_TRUE(relay.isOpen());
}

// =====================================================================
// Blanking (BLANKING_TIME = 200ms)
// =====================================================================

void test_change_before_blanking_elapsed_is_silent_noop(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    fakeMillis = 0;
    relay.begin(); // OFF, _lastChange = 0

    fakeMillis = 199; // < 200ms desde el ultimo cambio
    relay.close();

    TEST_ASSERT_TRUE(relay.isOpen()); // no cambio
    TEST_ASSERT_FALSE(relay.isClosed());
    // Solo el digitalWrite del begin(); el close() no debe haber escrito nada.
    Verify(Method(ArduinoFake(), digitalWrite)).Once();
}

void test_change_exactly_at_blanking_boundary_is_applied(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    fakeMillis = 0;
    relay.begin();

    fakeMillis = 200; // exactamente BLANKING_TIME
    relay.close();

    TEST_ASSERT_TRUE(relay.isClosed());
}

// =====================================================================
// isClosed()/isOpen() -- complementarios
// =====================================================================

void test_isClosed_and_isOpen_are_complementary(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    fakeMillis = 0;
    relay.begin();
    TEST_ASSERT_NOT_EQUAL(relay.isClosed(), relay.isOpen());

    fakeMillis = 200;
    relay.close();
    TEST_ASSERT_NOT_EQUAL(relay.isClosed(), relay.isOpen());
}

// =====================================================================
// MainPowerSwitch -- envuelve UN Relay y traduce el vocabulario del rele
// (open/close) al del dominio (disable/enable).
//
// Los tests de RelayManager que vivian acá (limite de addRelay, skip de
// slots nulos en xxxAll) se eliminaron con esa clase: probaban el manejo
// de una coleccion de N relés que por diseno nunca existio -- siempre
// hubo uno solo.
// =====================================================================

void test_begin_delegates_to_the_wrapped_relay(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    MainPowerSwitch mainSwitch(relay);

    mainSwitch.begin();

    Verify(Method(ArduinoFake(), pinMode).Using(7, OUTPUT)).Once();
    TEST_ASSERT_FALSE(mainSwitch.isEnabled());
}

// enable() cierra el rele: habilitar la etapa de potencia es dejar pasar
// corriente, sin importar como se llame del lado del hardware.
void test_enable_closes_the_relay(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    MainPowerSwitch mainSwitch(relay);
    mainSwitch.begin();

    fakeMillis = 200;
    mainSwitch.enable();

    TEST_ASSERT_TRUE(mainSwitch.isEnabled());
    Verify(Method(ArduinoFake(), digitalWrite).Using(7, HIGH)).AtLeast(1);
}

void test_disable_opens_the_relay(void) {
    Relay relay(7, Relay::ACTIVE_HIGH);
    MainPowerSwitch mainSwitch(relay);
    mainSwitch.begin();
    fakeMillis = 200;
    mainSwitch.enable();

    fakeMillis = 400;
    mainSwitch.disable();

    TEST_ASSERT_FALSE(mainSwitch.isEnabled());
}

// El wrapper no anula el blanking del rele: sigue siendo un no-op
// silencioso si se pide un cambio antes de los 200ms. Engine no se entera,
// por eso isEnabled() es la unica forma de confirmar el estado real.
void test_switch_still_respects_relay_blanking(void) {
    Relay relay(7, Relay::ACTIVE_LOW);
    MainPowerSwitch mainSwitch(relay);
    mainSwitch.begin();

    fakeMillis = 100;   // antes de que pasen los 200ms del begin()
    mainSwitch.enable();

    TEST_ASSERT_FALSE(mainSwitch.isEnabled());
}

void test_switch_honours_active_low_wiring(void) {
    Relay relay(7, Relay::ACTIVE_LOW);
    MainPowerSwitch mainSwitch(relay);
    mainSwitch.begin();

    fakeMillis = 200;
    mainSwitch.enable();

    TEST_ASSERT_TRUE(mainSwitch.isEnabled());
    Verify(Method(ArduinoFake(), digitalWrite).Using(7, LOW)).AtLeast(1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_begin_configures_output_pin);
    RUN_TEST(test_begin_opens_relay_active_high_even_at_time_zero);
    RUN_TEST(test_begin_opens_relay_active_low_even_at_time_zero);

    RUN_TEST(test_close_applies_high_for_active_high);
    RUN_TEST(test_close_applies_low_for_active_low);
    RUN_TEST(test_toggle_switches_state);

    RUN_TEST(test_change_before_blanking_elapsed_is_silent_noop);
    RUN_TEST(test_change_exactly_at_blanking_boundary_is_applied);

    RUN_TEST(test_isClosed_and_isOpen_are_complementary);

    RUN_TEST(test_begin_delegates_to_the_wrapped_relay);
    RUN_TEST(test_enable_closes_the_relay);
    RUN_TEST(test_disable_opens_the_relay);
    RUN_TEST(test_switch_still_respects_relay_blanking);
    RUN_TEST(test_switch_honours_active_low_wiring);

    return UNITY_END();
}
