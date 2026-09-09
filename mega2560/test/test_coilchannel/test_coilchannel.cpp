#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/coilchannel.hpp"

using namespace fakeit;

// =====================================================================
// CoilChannel escribe al PwmDriver real (no hay interfaz de por medio) y
// toca pinMode/digitalWrite/analogWrite directamente, asi que lo que se
// verifica es el valor que termina llegando a analogWrite().
//
// Con resolucion de 8 bits, duty 1.0 -> 255 y duty 0.5 -> 127.
// =====================================================================

namespace {
    constexpr uint8_t PwmPin1 = 44;
    constexpr uint8_t PwmPin2 = 45;
    constexpr uint8_t EnablePin1 = 23;
    constexpr uint8_t EnablePin2 = 24;
}

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
    When(Method(ArduinoFake(), pinMode)).AlwaysReturn();
    When(Method(ArduinoFake(), digitalWrite)).AlwaysReturn();
    When(Method(ArduinoFake(), analogWrite)).AlwaysReturn();
}

void tearDown(void) {}

// =====================================================================
// begin() / enable() / disable()
// =====================================================================

void test_begin_configures_enable_pin_and_leaves_channel_off(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");

    channel.begin();

    Verify(Method(ArduinoFake(), pinMode).Using(EnablePin1, OUTPUT)).AtLeast(1);
    Verify(Method(ArduinoFake(), digitalWrite).Using(EnablePin1, LOW)).AtLeast(1);
    TEST_ASSERT_FALSE(channel.isEnabled());
}

void test_enable_drives_enable_pin_and_pwm(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.begin();

    channel.enable();

    Verify(Method(ArduinoFake(), digitalWrite).Using(EnablePin1, HIGH)).AtLeast(1);
    TEST_ASSERT_TRUE(channel.isEnabled());
}

void test_disable_turns_channel_off(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.begin();
    channel.enable();

    channel.disable();

    TEST_ASSERT_FALSE(channel.isEnabled());
}

// El nivel activo es configurable porque depende de como espere la señal la
// etapa de potencia; con LOW los niveles se invierten.
void test_enable_respects_active_low_wiring(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1", LOW);
    channel.begin();

    channel.enable();

    Verify(Method(ArduinoFake(), digitalWrite).Using(EnablePin1, LOW)).AtLeast(1);
}

// Un canal deshabilitado no saca señal: PwmDriver fuerza 0 mientras esta
// disabled, incluso si se le sigue escribiendo el duty comun.
void test_disabled_channel_writes_zero_duty(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.begin();

    channel.setIntensity(0.8f);

    Verify(Method(ArduinoFake(), analogWrite).Using(PwmPin1, 0)).AtLeast(1);
}

// =====================================================================
// Factor de calibracion
// =====================================================================

void test_default_factor_is_one_and_passes_duty_through(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.begin();
    channel.enable();

    TEST_ASSERT_EQUAL_FLOAT(1.0f, channel.calibrationFactor());

    channel.setIntensity(0.5f);
    Verify(Method(ArduinoFake(), analogWrite).Using(PwmPin1, 127)).AtLeast(1);
}

// El caso que motiva el factor: dos bobinas que necesitan distinto duty
// para el mismo campo reciben el MISMO duty comun del lazo y cada una lo
// escala.
void test_factor_scales_the_common_duty(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.begin();
    channel.enable();
    channel.setCalibrationFactor(1.5f);

    channel.setIntensity(0.4f);   // 0.4 * 1.5 = 0.6 -> 153

    Verify(Method(ArduinoFake(), analogWrite).Using(PwmPin1, 153)).AtLeast(1);
}

// Saturacion: el factor empuja el duty por encima de 1.0. Se limita, pero
// significa que esa bobina no llega a la consigna -- por eso el modulo lo
// registra en el log en vez de recortar en silencio.
void test_factor_pushing_above_one_clamps_to_full_duty(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.begin();
    channel.enable();
    channel.setCalibrationFactor(2.0f);

    channel.setIntensity(0.8f);   // 1.6 -> se limita a 1.0 -> 255

    Verify(Method(ArduinoFake(), analogWrite).Using(PwmPin1, 255)).AtLeast(1);
}

// Un factor <= 0 apagaria la bobina entera: es un error de configuracion,
// no un valor valido, asi que se rechaza y se conserva el anterior.
void test_invalid_factor_is_rejected(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.setCalibrationFactor(1.3f);

    TEST_ASSERT_FALSE(channel.setCalibrationFactor(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.3f, channel.calibrationFactor());

    TEST_ASSERT_FALSE(channel.setCalibrationFactor(-2.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.3f, channel.calibrationFactor());
}

// Rango del schema (docs/config-schema.md, seccion "coils"): 0.1-5.0. Fuera
// de eso se rechaza igual que <=0, aunque el valor sea positivo.
void test_factor_outside_schema_range_is_rejected(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel channel(pwm, EnablePin1, "BOB1");
    channel.setCalibrationFactor(1.3f);

    TEST_ASSERT_FALSE(channel.setCalibrationFactor(0.05f));
    TEST_ASSERT_EQUAL_FLOAT(1.3f, channel.calibrationFactor());

    TEST_ASSERT_FALSE(channel.setCalibrationFactor(5.1f));
    TEST_ASSERT_EQUAL_FLOAT(1.3f, channel.calibrationFactor());

    TEST_ASSERT_TRUE(channel.setCalibrationFactor(0.1f));
    TEST_ASSERT_EQUAL_FLOAT(0.1f, channel.calibrationFactor());

    TEST_ASSERT_TRUE(channel.setCalibrationFactor(5.0f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, channel.calibrationFactor());
}

// =====================================================================
// CoilChannels::findByName -- config_coil identifica el canal por nombre,
// no por indice (docs/config-schema.md seccion 10.1).
// =====================================================================

void test_findByName_returns_matching_channel(void) {
    PwmDriver pwm1(PwmPin1);
    PwmDriver pwm2(PwmPin2);
    CoilChannel c1(pwm1, EnablePin1, "BOB1");
    CoilChannel c2(pwm2, EnablePin2, "BOB2");
    CoilChannels channels;
    channels.addChannel(&c1);
    channels.addChannel(&c2);

    TEST_ASSERT_EQUAL_STRING("BOB2", channels.findByName("BOB2")->getName());
    TEST_ASSERT_EQUAL_PTR(&c1, channels.findByName("BOB1"));
}

void test_findByName_returns_null_when_not_found(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel c(pwm, EnablePin1, "BOB1");
    CoilChannels channels;
    channels.addChannel(&c);

    TEST_ASSERT_NULL(channels.findByName("BOB9"));
    TEST_ASSERT_NULL(channels.findByName(nullptr));
}

void test_findByName_returns_null_with_no_channels(void) {
    CoilChannels channels;

    TEST_ASSERT_NULL(channels.findByName("BOB1"));
}

// =====================================================================
// CoilChannels -- operaciones colectivas
// =====================================================================

void test_addChannel_counts_and_returns_channels(void) {
    PwmDriver pwm1(PwmPin1);
    PwmDriver pwm2(PwmPin2);
    CoilChannel c1(pwm1, EnablePin1, "BOB1");
    CoilChannel c2(pwm2, EnablePin2, "BOB2");
    CoilChannels channels;

    TEST_ASSERT_TRUE(channels.addChannel(&c1));
    TEST_ASSERT_TRUE(channels.addChannel(&c2));

    TEST_ASSERT_EQUAL_UINT8(2, channels.count());
    TEST_ASSERT_EQUAL_STRING("BOB2", channels.getChannel(1)->getName());
    TEST_ASSERT_NULL(channels.getChannel(2));
}

void test_addChannel_rejects_null_and_beyond_max(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel c(pwm, EnablePin1, "BOB1");
    CoilChannels channels;

    TEST_ASSERT_FALSE(channels.addChannel(nullptr));

    for (uint8_t i = 0; i < MAX_COIL_CHANNELS; i++) {
        TEST_ASSERT_TRUE(channels.addChannel(&c));
    }
    TEST_ASSERT_FALSE(channels.addChannel(&c));
    TEST_ASSERT_EQUAL_UINT8(MAX_COIL_CHANNELS, channels.count());
}

// El duty que calcula el lazo es UNO SOLO para todas las bobinas: cada una
// lo escala por su factor. Es la diferencia con cuatro lazos independientes,
// que con un solo magnetometro no serian posibles.
void test_writeAll_sends_the_same_common_duty_scaled_per_channel(void) {
    PwmDriver pwm1(PwmPin1);
    PwmDriver pwm2(PwmPin2);
    CoilChannel c1(pwm1, EnablePin1, "BOB1");
    CoilChannel c2(pwm2, EnablePin2, "BOB2");
    CoilChannels channels;
    channels.addChannel(&c1);
    channels.addChannel(&c2);
    channels.beginAll();
    channels.enableAll();

    c2.setCalibrationFactor(1.5f);
    channels.writeAll(0.4f);

    Verify(Method(ArduinoFake(), analogWrite).Using(PwmPin1, 102)).AtLeast(1);  // 0.4
    Verify(Method(ArduinoFake(), analogWrite).Using(PwmPin2, 153)).AtLeast(1);  // 0.6
}

void test_enableAll_and_disableAll_reach_every_channel(void) {
    PwmDriver pwm1(PwmPin1);
    PwmDriver pwm2(PwmPin2);
    CoilChannel c1(pwm1, EnablePin1, "BOB1");
    CoilChannel c2(pwm2, EnablePin2, "BOB2");
    CoilChannels channels;
    channels.addChannel(&c1);
    channels.addChannel(&c2);
    channels.beginAll();

    channels.enableAll();
    TEST_ASSERT_TRUE(c1.isEnabled());
    TEST_ASSERT_TRUE(c2.isEnabled());

    channels.disableAll();
    TEST_ASSERT_FALSE(c1.isEnabled());
    TEST_ASSERT_FALSE(c2.isEnabled());
}

void test_clearChannels_empties_the_set(void) {
    PwmDriver pwm(PwmPin1);
    CoilChannel c(pwm, EnablePin1, "BOB1");
    CoilChannels channels;
    channels.addChannel(&c);

    channels.clearChannels();

    TEST_ASSERT_EQUAL_UINT8(0, channels.count());
    TEST_ASSERT_NULL(channels.getChannel(0));
}

// Sin canales registrados las operaciones colectivas son no-ops, no crashes
// -- es el estado del sistema antes de que setup() registre las bobinas.
void test_collective_operations_are_safe_with_no_channels(void) {
    CoilChannels channels;

    channels.beginAll();
    channels.enableAll();
    channels.writeAll(0.5f);
    channels.disableAll();

    TEST_ASSERT_EQUAL_UINT8(0, channels.count());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_begin_configures_enable_pin_and_leaves_channel_off);
    RUN_TEST(test_enable_drives_enable_pin_and_pwm);
    RUN_TEST(test_disable_turns_channel_off);
    RUN_TEST(test_enable_respects_active_low_wiring);
    RUN_TEST(test_disabled_channel_writes_zero_duty);

    RUN_TEST(test_default_factor_is_one_and_passes_duty_through);
    RUN_TEST(test_factor_scales_the_common_duty);
    RUN_TEST(test_factor_pushing_above_one_clamps_to_full_duty);
    RUN_TEST(test_invalid_factor_is_rejected);
    RUN_TEST(test_factor_outside_schema_range_is_rejected);

    RUN_TEST(test_addChannel_counts_and_returns_channels);
    RUN_TEST(test_addChannel_rejects_null_and_beyond_max);
    RUN_TEST(test_writeAll_sends_the_same_common_duty_scaled_per_channel);
    RUN_TEST(test_enableAll_and_disableAll_reach_every_channel);
    RUN_TEST(test_clearChannels_empties_the_set);
    RUN_TEST(test_collective_operations_are_safe_with_no_channels);

    RUN_TEST(test_findByName_returns_matching_channel);
    RUN_TEST(test_findByName_returns_null_when_not_found);
    RUN_TEST(test_findByName_returns_null_with_no_channels);

    return UNITY_END();
}
