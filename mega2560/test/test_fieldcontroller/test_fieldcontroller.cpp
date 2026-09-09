#include <ArduinoFake.h>
#include <unity.h>

#include "../support/arduino_fakes.hpp"
#include "../../src/fieldcontroller.hpp"

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
}

void tearDown(void) {}

// =====================================================================
// Paso proporcional continuo
// =====================================================================

void test_error_within_deadband_leaves_output_unchanged(void) {
    FieldController::Config cfg;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);

    float out = controller.update(0.999f); // error=0.001, dentro de deadBand (0.01)

    TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
}

void test_large_error_clamps_step_to_max_step(void) {
    FieldController::Config cfg;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);

    // error=0.5 -> kp*error=0.05, ya en el limite de maxStep (0.05)
    float out = controller.update(0.5f);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, cfg.maxStep, out);
}

void test_step_scales_proportionally_with_error(void) {
    FieldController::Config cfg;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);

    // error=0.05 -> kp*error=0.1*0.05=0.005, por debajo de maxStep
    float out = controller.update(0.95f);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, cfg.kp * 0.05f, out);
}

void test_smaller_error_produces_smaller_step_than_larger_error(void) {
    // Confirma la razon del cambio (MOD-011): un error apenas fuera de la
    // deadBand ya no produce el mismo salto que un error mucho mayor.
    FieldController::Config cfgSmall;
    FieldController small(cfgSmall);
    small.setSetpoint(1.0f);
    float stepSmall = small.update(0.98f); // error=0.02, apenas fuera de deadBand

    FieldController::Config cfgLarge;
    FieldController large(cfgLarge);
    large.setSetpoint(1.0f);
    float stepLarge = large.update(0.5f); // error=0.5

    TEST_ASSERT_TRUE(stepSmall < stepLarge);
}

// =====================================================================
// Exclusion mutua paso proporcional / integral lenta (bug corregido)
// =====================================================================

void test_proportional_and_slow_integral_never_apply_in_the_same_call(void) {
    // Reproduce el escenario del bug: ventana con alta variacion (dispararia
    // la integral si se evaluara sola) Y, en la misma llamada que llena la
    // ventana, el error sigue fuera de deadBand (dispara el paso
    // proporcional). Antes de la correccion, el gate de la integral
    // corria en un `if` independiente y su condicion daba siempre "el
    // paso proporcional no toco el output" (comparacion vacia contra un
    // _lastOutput resincronizado cada llamada) -- asi que ambos mecanismos
    // aplicaban en la misma llamada. Con el fix (if/else sobre el mismo
    // error), solo el paso proporcional debe aplicar aqui.
    FieldController::Config cfg;
    cfg.driftWindow = 3;
    cfg.sampleTime = 1.0f;
    cfg.integralGain = 0.1f;
    cfg.integralLimit = 0.5f;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);

    controller.update(0.5f);  // error=0.5, kp*error=0.05=maxStep -> output=0.05
    controller.update(1.5f);  // error=-0.5, step=-0.05=maxStep -> output=0.00
    float out = controller.update(0.6f); // error=0.4 (>deadBand), llena la ventana [0.5,1.5,0.6] (variation=1.0 > driftThreshold)

    // Paso proporcional: kp*error=0.1*0.4=0.04 (no satura maxStep) -> output=0.04.
    // Si la integral tambien hubiera aplicado, output seria 0.00 + 0.04 (paso)
    // + 0.04 (integral: 0.4*1.0*0.1) = 0.08, no 0.04.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.04f, out);
}

void test_slow_integral_applies_only_when_settled_with_drift(void) {
    FieldController::Config cfg;
    cfg.driftWindow = 3;
    cfg.sampleTime = 1.0f;
    cfg.integralGain = 0.1f;
    cfg.integralLimit = 0.5f;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);

    controller.update(0.5f);   // error=0.5, coarse -> output=0.05
    controller.update(0.5f);   // error=0.5, coarse -> output=0.10
    // Llena la ventana [0.5, 0.5, 0.995]; variacion=0.495 > driftThreshold.
    // error=0.005, dentro de deadBand -> rama integral: slowIntegral =
    // 0.005*1.0*0.1 = 0.0005, sin clamp.
    float out = controller.update(0.995f);

    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 0.1005f, out);
}

// =====================================================================
// Clamps
// =====================================================================

void test_slow_integral_clamps_to_integral_limit(void) {
    FieldController::Config cfg;
    cfg.driftWindow = 2;
    cfg.sampleTime = 1.0f;
    cfg.integralGain = 10.0f;   // fuerza a que el incremento crudo exceda integralLimit
    cfg.integralLimit = 0.05f;
    cfg.deadBand = 1.0f;        // se mantiene "asentado" durante todo el test
    cfg.driftThreshold = 0.0f;
    cfg.outputMin = -1.0f;      // evita que el clamp de _output enmascare el de la integral
    cfg.outputMax = 1.0f;
    FieldController controller(cfg);
    controller.setSetpoint(0.0f);

    controller.update(-0.05f); // llena parcialmente la ventana, sin integral aun (windowFilled=false)
    // Llena la ventana [-0.05, 0.05]; variation=0.1 > 0. error=-0.05.
    // Incremento crudo: -0.05*1*10=-0.5, clampeado a -0.05 (integralLimit).
    float out = controller.update(0.05f);

    TEST_ASSERT_FLOAT_WITHIN(0.00001f, -0.05f, out);
}

void test_output_clamps_to_output_max(void) {
    FieldController::Config cfg;
    cfg.outputMax = 0.1f;
    FieldController controller(cfg);
    controller.setSetpoint(10.0f);

    controller.update(0.0f); // output=0.05
    controller.update(0.0f); // output=0.10
    float out = controller.update(0.0f); // output pediria 0.15, clampeado a 0.10

    TEST_ASSERT_EQUAL_FLOAT(0.1f, out);
}

void test_output_clamps_to_output_min(void) {
    FieldController::Config cfg;
    cfg.outputMin = -0.1f;
    FieldController controller(cfg);
    controller.setSetpoint(-10.0f);

    controller.update(0.0f); // output=-0.05
    controller.update(0.0f); // output=-0.10
    float out = controller.update(0.0f); // output pediria -0.15, clampeado a -0.10

    TEST_ASSERT_EQUAL_FLOAT(-0.1f, out);
}

// =====================================================================
// reset()
// =====================================================================

void test_reset_restores_initial_state(void) {
    FieldController::Config cfg;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);
    controller.update(0.5f);
    controller.update(0.5f);

    controller.reset();

    TEST_ASSERT_EQUAL_FLOAT(0.0f, controller.getOutput());
    // Ventana reiniciada: hacen falta driftWindow (5) muestras de nuevo
    // antes de que la integral lenta pueda dispararse -- si no se hubiera
    // reseteado, ya estaria "llena" desde antes del reset.
    controller.setSetpoint(1.0f);
    float out = controller.update(1.0f); // error=0, dentro de deadBand, ventana no llena -> nada aplica
    TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
}

// =====================================================================
// getOutput()
// =====================================================================

void test_get_output_matches_last_update_return_value(void) {
    FieldController::Config cfg;
    FieldController controller(cfg);
    controller.setSetpoint(1.0f);

    float out = controller.update(0.5f);

    TEST_ASSERT_EQUAL_FLOAT(out, controller.getOutput());
}

// =====================================================================
// makeFieldControllerConfig -- derivacion de sampleTime
//
// sampleTime tiene que coincidir con la cadencia real de update() o el
// termino integral queda escalado mal. Antes era un literal (0.2s) contra
// un intervalo real de 500ms, y nada delataba la diferencia porque las dos
// constantes vivian en archivos distintos. Estos tests fijan la derivacion.
// =====================================================================

void test_makeConfig_derives_sample_time_from_interval(void) {
    // El caso real del Mega: Intervals::MeasureMagneticField = 500ms.
    FieldController::Config config = makeFieldControllerConfig(500);

    TEST_ASSERT_EQUAL_FLOAT(0.5f, config.sampleTime);
}

void test_makeConfig_tracks_a_different_interval(void) {
    // Si se cambia la cadencia de medicion, el regulador la sigue solo --
    // que es el punto de derivarlo en vez de escribirlo a mano.
    TEST_ASSERT_EQUAL_FLOAT(0.2f, makeFieldControllerConfig(200).sampleTime);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, makeFieldControllerConfig(1000).sampleTime);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, makeFieldControllerConfig(50).sampleTime);
}

// Solo sampleTime es derivado: los parametros de calibracion se fijan en el
// .ino (y, cuando exista, desde la configuracion en SD), no acá.
void test_makeConfig_leaves_tuning_parameters_at_defaults(void) {
    FieldController::Config defaults;
    FieldController::Config config = makeFieldControllerConfig(500);

    TEST_ASSERT_EQUAL_FLOAT(defaults.kp, config.kp);
    TEST_ASSERT_EQUAL_FLOAT(defaults.maxStep, config.maxStep);
    TEST_ASSERT_EQUAL_FLOAT(defaults.deadBand, config.deadBand);
    TEST_ASSERT_EQUAL_FLOAT(defaults.integralGain, config.integralGain);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_error_within_deadband_leaves_output_unchanged);
    RUN_TEST(test_large_error_clamps_step_to_max_step);
    RUN_TEST(test_step_scales_proportionally_with_error);
    RUN_TEST(test_smaller_error_produces_smaller_step_than_larger_error);

    RUN_TEST(test_proportional_and_slow_integral_never_apply_in_the_same_call);
    RUN_TEST(test_slow_integral_applies_only_when_settled_with_drift);

    RUN_TEST(test_slow_integral_clamps_to_integral_limit);
    RUN_TEST(test_output_clamps_to_output_max);
    RUN_TEST(test_output_clamps_to_output_min);

    RUN_TEST(test_reset_restores_initial_state);

    RUN_TEST(test_get_output_matches_last_update_return_value);

    RUN_TEST(test_makeConfig_derives_sample_time_from_interval);
    RUN_TEST(test_makeConfig_tracks_a_different_interval);
    RUN_TEST(test_makeConfig_leaves_tuning_parameters_at_defaults);

    return UNITY_END();
}
