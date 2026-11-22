// Fija el formato de linea de log.hpp (copia identica en esp32/src): lo que
// lee el operador en el monitor serie y lo que se filtra con grep.
#include <ArduinoFake.h>
#include <unity.h>
#include <algorithm>
#include <string>

// Detalle prendido solo para CAMPO, como se haria con -D en platformio.ini.
#define LOG_DEBUG_CAMPO 1
#include "../../src/log.hpp"

using namespace fakeit;

static std::string captured;

static void captureSink(const char* data, size_t length) { captured.append(data, length); }

void setUp(void) {
  ArduinoFakeReset();
  captured.clear();
  Log::begin(captureSink);
  When(Method(ArduinoFake(), millis)).AlwaysReturn(12345);
}

void tearDown(void) { Log::begin(nullptr); }

void test_line_has_uptime_level_padded_module_and_message(void) {
  LOG_I(CORRIDA, "inicio dur=%u s", 300u);
  TEST_ASSERT_EQUAL_STRING("    12.345 I CORRIDA    inicio dur=300 s\r\n", captured.c_str());
}

void test_each_level_writes_its_letter(void) {
  LOG_E(ENLACE, "a");
  LOG_W(ENLACE, "b");
  TEST_ASSERT_EQUAL_STRING("    12.345 E ENLACE     a\r\n    12.345 W ENLACE     b\r\n", captured.c_str());
}

void test_longest_module_name_fits_the_column(void) {
  LOG_I(TELEMETRIA, "x");
  TEST_ASSERT_EQUAL_STRING("    12.345 I TELEMETRIA x\r\n", captured.c_str());
}

void test_debug_is_off_unless_the_module_enables_it(void) {
  LOG_D(TEMP, "no sale");
  TEST_ASSERT_EQUAL_STRING("", captured.c_str());
  LOG_D(CAMPO, "sale");
  TEST_ASSERT_EQUAL_STRING("    12.345 D CAMPO      sale\r\n", captured.c_str());
}

void test_floats_are_formatted(void) {
  LOG_I(TEMP, "valor=%.1f C", 41.25);
  TEST_ASSERT_EQUAL_STRING("    12.345 I TEMP       valor=41.2 C\r\n", captured.c_str());
}

void test_long_message_is_cut_but_line_still_ends(void) {
  std::string big(500, 'x');
  LOG_W(CONFIG, "%s", big.c_str());
  TEST_ASSERT_TRUE(captured.size() <= LOG_LINE_MAX);
  TEST_ASSERT_EQUAL_STRING("\r\n", captured.substr(captured.size() - 2).c_str());
  TEST_ASSERT_EQUAL(1, (int)std::count(captured.begin(), captured.end(), '\n'));
}

void test_without_sink_nothing_runs(void) {
  Log::begin(nullptr);
  ArduinoFakeReset();  // sin stub de millis: si se llamara, el test reventaria
  LOG_E(SISTEMA, "nada");
  TEST_ASSERT_EQUAL_STRING("", captured.c_str());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_line_has_uptime_level_padded_module_and_message);
  RUN_TEST(test_each_level_writes_its_letter);
  RUN_TEST(test_longest_module_name_fits_the_column);
  RUN_TEST(test_debug_is_off_unless_the_module_enables_it);
  RUN_TEST(test_floats_are_formatted);
  RUN_TEST(test_long_message_is_cut_but_line_still_ends);
  RUN_TEST(test_without_sink_nothing_runs);
  return UNITY_END();
}
