#include <unity.h>

#include "../../src/sensorchoice.hpp"

// La clave `sensor` de detector.sources decide que driver alimenta a cada
// fuente. Un valor mal interpretado no da error en ningun lado: CEM1 quedaria
// leyendo el simulador con el magnetometro real cableado, o TEMP1 bloqueando
// 750 ms por medicion sin sensor en el bus.

void setUp(void) {}
void tearDown(void) {}

// Helper: parte de `initial` y devuelve lo que queda despues de parsear.
static SensorChoice after(const char* source, const char* value, SensorChoice initial, bool* ok = nullptr) {
    SensorChoice choice = initial;
    bool accepted = SensorChoices::parse(source, value, choice);
    if (ok) *ok = accepted;
    return choice;
}

void test_cem1_accepts_mlx90393_and_sim(void) {
    TEST_ASSERT_EQUAL(SensorChoice::Real, after("CEM1", "mlx90393", SensorChoice::Sim));
    TEST_ASSERT_EQUAL(SensorChoice::Sim, after("CEM1", "sim", SensorChoice::Real));
}

// CEM1 es la realimentacion del lazo: sin magnetometro, el FieldController
// empujaria el duty a fondo buscando un campo que no puede ver.
void test_cem1_rejects_none_and_keeps_current(void) {
    bool ok = true;
    TEST_ASSERT_EQUAL(SensorChoice::Real, after("CEM1", "none", SensorChoice::Real, &ok));
    TEST_ASSERT_FALSE(ok);
}

void test_temp1_accepts_ds18b20_sim_and_none(void) {
    TEST_ASSERT_EQUAL(SensorChoice::Real, after("TEMP1", "ds18b20", SensorChoice::None));
    TEST_ASSERT_EQUAL(SensorChoice::Sim, after("TEMP1", "sim", SensorChoice::Real));
    TEST_ASSERT_EQUAL(SensorChoice::None, after("TEMP1", "none", SensorChoice::Real));
}

// Los nombres son por fuente: "mlx90393" no es un termometro.
void test_driver_names_are_not_interchangeable_between_sources(void) {
    bool ok = true;
    TEST_ASSERT_EQUAL(SensorChoice::None, after("TEMP1", "mlx90393", SensorChoice::None, &ok));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(SensorChoice::Sim, after("CEM1", "ds18b20", SensorChoice::Sim, &ok));
    TEST_ASSERT_FALSE(ok);
}

void test_unknown_value_or_source_keeps_current(void) {
    bool ok = true;
    TEST_ASSERT_EQUAL(SensorChoice::Real, after("TEMP1", "Sim", SensorChoice::Real, &ok));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL(SensorChoice::Real, after("TEMP1", "", SensorChoice::Real));
    TEST_ASSERT_EQUAL(SensorChoice::Sim, after("SCT013-1", "none", SensorChoice::Sim));
    TEST_ASSERT_EQUAL(SensorChoice::Sim, after("TEMP1", nullptr, SensorChoice::Sim));
    TEST_ASSERT_EQUAL(SensorChoice::Sim, after(nullptr, "none", SensorChoice::Sim));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_cem1_accepts_mlx90393_and_sim);
    RUN_TEST(test_cem1_rejects_none_and_keeps_current);
    RUN_TEST(test_temp1_accepts_ds18b20_sim_and_none);
    RUN_TEST(test_driver_names_are_not_interchangeable_between_sources);
    RUN_TEST(test_unknown_value_or_source_keeps_current);

    return UNITY_END();
}
