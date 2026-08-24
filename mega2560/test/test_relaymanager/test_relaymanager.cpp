#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/relaymanager.hpp"

using namespace fakeit;

// =====================================================================
// Relay/RelayManager tocan pinMode/digitalWrite/millis directamente (no
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
// RelayManager::addRelay
// =====================================================================

void test_addRelay_accepts_up_to_max_relays(void) {
    RelayManager manager;
    Relay relays[10] = {
        Relay(0, Relay::ACTIVE_HIGH), Relay(1, Relay::ACTIVE_HIGH),
        Relay(2, Relay::ACTIVE_HIGH), Relay(3, Relay::ACTIVE_HIGH),
        Relay(4, Relay::ACTIVE_HIGH), Relay(5, Relay::ACTIVE_HIGH),
        Relay(6, Relay::ACTIVE_HIGH), Relay(7, Relay::ACTIVE_HIGH),
        Relay(8, Relay::ACTIVE_HIGH), Relay(9, Relay::ACTIVE_HIGH),
    };
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(manager.addRelay(&relays[i]));
    }
    TEST_ASSERT_EQUAL_INT(10, manager.count());
}

void test_addRelay_rejects_beyond_max_relays(void) {
    RelayManager manager;
    Relay relays[10] = {
        Relay(0, Relay::ACTIVE_HIGH), Relay(1, Relay::ACTIVE_HIGH),
        Relay(2, Relay::ACTIVE_HIGH), Relay(3, Relay::ACTIVE_HIGH),
        Relay(4, Relay::ACTIVE_HIGH), Relay(5, Relay::ACTIVE_HIGH),
        Relay(6, Relay::ACTIVE_HIGH), Relay(7, Relay::ACTIVE_HIGH),
        Relay(8, Relay::ACTIVE_HIGH), Relay(9, Relay::ACTIVE_HIGH),
    };
    for (int i = 0; i < 10; i++) manager.addRelay(&relays[i]);

    Relay overflow(10, Relay::ACTIVE_HIGH);
    TEST_ASSERT_FALSE(manager.addRelay(&overflow));
    TEST_ASSERT_EQUAL_INT(10, manager.count());
}

// =====================================================================
// RelayManager::xxxAll -- aplica a cada rele, salta nulos, blanking individual
// =====================================================================

void test_beginAll_calls_begin_on_every_registered_relay(void) {
    RelayManager manager;
    Relay r0(2, Relay::ACTIVE_HIGH);
    Relay r1(3, Relay::ACTIVE_LOW);
    manager.addRelay(&r0);
    manager.addRelay(&r1);

    manager.beginAll();

    Verify(Method(ArduinoFake(), pinMode).Using(2, OUTPUT)).Once();
    Verify(Method(ArduinoFake(), pinMode).Using(3, OUTPUT)).Once();
    TEST_ASSERT_TRUE(r0.isOpen());
    TEST_ASSERT_TRUE(r1.isOpen());
}

void test_closeAll_respects_individual_blanking(void) {
    RelayManager manager;
    Relay r0(2, Relay::ACTIVE_HIGH);
    Relay r1(3, Relay::ACTIVE_HIGH);
    manager.addRelay(&r0);
    manager.addRelay(&r1);

    fakeMillis = 0;
    manager.beginAll(); // ambos OFF, _lastChange = 0

    // r1 tiene un cambio reciente propio, dentro del blanking; r0 no.
    fakeMillis = 50;
    r1.close();
    TEST_ASSERT_TRUE(r1.isOpen()); // bloqueado por blanking, sigue OFF

    fakeMillis = 250; // >=200ms desde el begin() de ambos, y desde el intento de r1
    manager.closeAll();

    TEST_ASSERT_TRUE(r0.isClosed());
    TEST_ASSERT_TRUE(r1.isClosed());
}

void test_openAll_skips_null_slots(void) {
    RelayManager manager;
    Relay r0(2, Relay::ACTIVE_HIGH);
    manager.addRelay(&r0);
    manager.addRelay(nullptr);

    fakeMillis = 0;
    manager.beginAll();
    fakeMillis = 200;

    // No debe crashear con el slot nulo intercalado.
    manager.openAll();
    TEST_ASSERT_TRUE(r0.isOpen());
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

    RUN_TEST(test_addRelay_accepts_up_to_max_relays);
    RUN_TEST(test_addRelay_rejects_beyond_max_relays);

    RUN_TEST(test_beginAll_calls_begin_on_every_registered_relay);
    RUN_TEST(test_closeAll_respects_individual_blanking);
    RUN_TEST(test_openAll_skips_null_slots);

    return UNITY_END();
}
