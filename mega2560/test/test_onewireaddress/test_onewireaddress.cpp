#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/onewireaddress.hpp"

// La direccion del DS18B20 viene de la SD (detector.sources[TEMP1].address).
// Un digito mal copiado no puede terminar apuntando a un sensor inexistente:
// se rechaza entera y queda la anterior.

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// El ROM code del sensor original (el que estaba fijo en el .ino): real, asi
// que su CRC tiene que dar.
static const uint8_t ORIGINAL[8] = { 0x28, 0x3F, 0xE5, 0x57, 0x04, 0xE1, 0x3D, 0xED };

void test_crc_of_a_real_rom_code_matches(void) {
    TEST_ASSERT_EQUAL_HEX8(ORIGINAL[7], OneWireAddress::crc8(ORIGINAL, 7));
}

void test_parses_with_colons(void) {
    uint8_t out[8] = {0};
    TEST_ASSERT_TRUE(OneWireAddress::parse("28:3F:E5:57:04:E1:3D:ED", out) == OneWireAddress::ParseResult::Ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ORIGINAL, out, 8);
}

void test_parses_plain_lowercase_and_dashes(void) {
    uint8_t out[8] = {0};
    TEST_ASSERT_TRUE(OneWireAddress::parse("283fe55704e13ded", out) == OneWireAddress::ParseResult::Ok);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ORIGINAL, out, 8);
    TEST_ASSERT_TRUE(OneWireAddress::parse("28-3F-E5-57-04-E1-3D-ED", out) == OneWireAddress::ParseResult::Ok);
}

void test_rejects_a_mistyped_digit_by_crc(void) {
    uint8_t out[8] = {0};
    TEST_ASSERT_TRUE(OneWireAddress::parse("28:3F:E5:57:04:E1:3E:ED", out) == OneWireAddress::ParseResult::BadCrc);
}

void test_rejects_wrong_length_and_garbage(void) {
    uint8_t out[8] = {0};
    TEST_ASSERT_TRUE(OneWireAddress::parse("28:3F:E5:57:04:E1:3D", out) == OneWireAddress::ParseResult::BadFormat);
    TEST_ASSERT_TRUE(OneWireAddress::parse("28:3F:E5:57:04:E1:3D:ED:00", out) == OneWireAddress::ParseResult::BadFormat);
    TEST_ASSERT_TRUE(OneWireAddress::parse("28:3F:E5:57:04:E1:3D:EG", out) == OneWireAddress::ParseResult::BadFormat);
    TEST_ASSERT_TRUE(OneWireAddress::parse("", out) == OneWireAddress::ParseResult::BadFormat);
    TEST_ASSERT_TRUE(OneWireAddress::parse(nullptr, out) == OneWireAddress::ParseResult::BadFormat);
}

void test_rejects_another_onewire_family(void) {
    // Mismo cuerpo con familia 0x10 (DS18S20) y su CRC recalculado: valida
    // como direccion, pero no es un DS18B20.
    uint8_t other[8] = { 0x10, 0x3F, 0xE5, 0x57, 0x04, 0xE1, 0x3D, 0x00 };
    other[7] = OneWireAddress::crc8(other, 7);
    char text[OneWireAddress::TextLength];
    OneWireAddress::format(other, text);
    uint8_t out[8] = {0};
    TEST_ASSERT_TRUE(OneWireAddress::parse(text, out) == OneWireAddress::ParseResult::NotDs18b20);
}

void test_rejected_parse_leaves_output_untouched(void) {
    uint8_t out[8];
    memcpy(out, ORIGINAL, 8);
    OneWireAddress::parse("28:3F:E5:57:04:E1:3E:ED", out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ORIGINAL, out, 8);
}

void test_format_round_trips(void) {
    char text[OneWireAddress::TextLength];
    OneWireAddress::format(ORIGINAL, text);
    TEST_ASSERT_EQUAL_STRING("28:3F:E5:57:04:E1:3D:ED", text);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_of_a_real_rom_code_matches);
    RUN_TEST(test_parses_with_colons);
    RUN_TEST(test_parses_plain_lowercase_and_dashes);
    RUN_TEST(test_rejects_a_mistyped_digit_by_crc);
    RUN_TEST(test_rejects_wrong_length_and_garbage);
    RUN_TEST(test_rejects_another_onewire_family);
    RUN_TEST(test_rejected_parse_leaves_output_untouched);
    RUN_TEST(test_format_round_trips);
    return UNITY_END();
}
