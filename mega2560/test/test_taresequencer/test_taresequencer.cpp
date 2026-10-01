#include <unity.h>

#include "../../src/taresequencer.hpp"

void setUp(void) {}
void tearDown(void) {}

static const unsigned long Cadence = 500;

// =====================================================================
// Arranque
// =====================================================================

void test_idle_sequencer_does_nothing(void) {
    TareSequencer seq;
    TEST_ASSERT_FALSE(seq.pending());
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(123456));
}

void test_first_sample_waits_for_the_settle_time(void) {
    TareSequencer seq;
    TEST_ASSERT_TRUE(seq.begin(1000, Cadence));
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(1000));
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(1000 + TareTiming::SettleMs - 1));
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(1000 + TareTiming::SettleMs));
}

void test_a_second_start_while_pending_is_refused_and_changes_nothing(void) {
    TareSequencer seq;
    seq.begin(1000, Cadence);
    TEST_ASSERT_FALSE(seq.begin(1300, Cadence));
    // Sigue contando desde el start original, no desde el repetido.
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(1000 + TareTiming::SettleMs));
}

// =====================================================================
// Cadencia
// =====================================================================

void test_samples_follow_the_cadence(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    unsigned long t = TareTiming::SettleMs;
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(t));
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(t + Cadence - 1));
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(t + Cadence));
}

void test_a_late_poll_takes_one_sample_not_a_burst(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    // loop() se demoro mucho (por ejemplo el DS18B20 bloqueando 750 ms).
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(1500));
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(1500));
}

// =====================================================================
// Resultado de la tara
// =====================================================================

void test_collecting_keeps_the_sequence_pending(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    seq.poll(TareTiming::SettleMs);
    TEST_ASSERT_EQUAL(TareOutcome::Pending, seq.report(TareStatus::Collecting));
    TEST_ASSERT_TRUE(seq.pending());
}

void test_ready_ends_the_sequence(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    seq.poll(TareTiming::SettleMs);
    TEST_ASSERT_EQUAL(TareOutcome::Ready, seq.report(TareStatus::Ready));
    TEST_ASSERT_FALSE(seq.pending());
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(TareTiming::SettleMs + 10 * Cadence));
}

void test_failed_ends_the_sequence_as_rejected(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    seq.poll(TareTiming::SettleMs);
    TEST_ASSERT_EQUAL(TareOutcome::Rejected, seq.report(TareStatus::Failed));
    TEST_ASSERT_FALSE(seq.pending());
}

void test_a_sensor_that_lost_its_tare_counts_as_rejected(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    seq.poll(TareTiming::SettleMs);
    TEST_ASSERT_EQUAL(TareOutcome::Rejected, seq.report(TareStatus::Idle));
}

void test_a_report_with_nothing_pending_is_ignored(void) {
    TareSequencer seq;
    TEST_ASSERT_EQUAL(TareOutcome::Pending, seq.report(TareStatus::Ready));
}

// =====================================================================
// Vencimiento
// =====================================================================

void test_times_out_after_the_limit_and_then_finishes(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    // Justo en el limite todavia no vence (el vencimiento es estrictamente mayor).
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(TareTiming::TimeoutMs - 1));
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(TareTiming::TimeoutMs));
    TEST_ASSERT_EQUAL(TareAction::Timeout, seq.poll(TareTiming::TimeoutMs + 1));
    TEST_ASSERT_FALSE(seq.pending());
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(TareTiming::TimeoutMs + 2));
}

void test_timeout_wins_over_a_sample_that_was_due(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    TEST_ASSERT_EQUAL(TareAction::Timeout, seq.poll(TareTiming::TimeoutMs + 600));
}

// =====================================================================
// Cancelacion
// =====================================================================

void test_cancel_stops_the_sequence(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    seq.cancel();
    TEST_ASSERT_FALSE(seq.pending());
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(TareTiming::SettleMs));
}

void test_a_new_start_is_accepted_after_cancel_or_finish(void) {
    TareSequencer seq;
    seq.begin(0, Cadence);
    seq.cancel();
    TEST_ASSERT_TRUE(seq.begin(5000, Cadence));
    seq.poll(5000 + TareTiming::SettleMs);
    seq.report(TareStatus::Ready);
    TEST_ASSERT_TRUE(seq.begin(9000, Cadence));
}

// =====================================================================
// Vuelta del contador de millis()
// =====================================================================

void test_works_across_the_millis_rollover(void) {
    TareSequencer seq;
    unsigned long start = 0xFFFFFFFFUL - 200UL;   // faltan 200 ms para la vuelta
    seq.begin(start, Cadence);
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(start + 100));
    // start + SettleMs ya paso por cero.
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(start + TareTiming::SettleMs));
    TEST_ASSERT_EQUAL(TareAction::None, seq.poll(start + TareTiming::SettleMs + Cadence - 1));
    TEST_ASSERT_EQUAL(TareAction::TakeSample, seq.poll(start + TareTiming::SettleMs + Cadence));
    TEST_ASSERT_EQUAL(TareAction::Timeout, seq.poll(start + TareTiming::TimeoutMs + 1));
}

// =====================================================================
// Recorrido completo, como lo hace Engine
// =====================================================================

void test_full_run_with_four_samples_fits_in_the_timeout(void) {
    TareSequencer seq;
    FieldTare tare;
    tare.begin();
    seq.begin(0, Cadence);

    TareOutcome outcome = TareOutcome::Pending;
    unsigned long now = 0;
    for (; now <= TareTiming::TimeoutMs && outcome == TareOutcome::Pending; now += 10) {
        TareAction action = seq.poll(now);
        if (action == TareAction::Timeout) break;
        if (action != TareAction::TakeSample) continue;
        tare.addSample(20, 30, -40);
        outcome = seq.report(tare.status());
    }
    TEST_ASSERT_EQUAL(TareOutcome::Ready, outcome);
    TEST_ASSERT_TRUE(tare.isReady());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_sequencer_does_nothing);
    RUN_TEST(test_first_sample_waits_for_the_settle_time);
    RUN_TEST(test_a_second_start_while_pending_is_refused_and_changes_nothing);
    RUN_TEST(test_samples_follow_the_cadence);
    RUN_TEST(test_a_late_poll_takes_one_sample_not_a_burst);
    RUN_TEST(test_collecting_keeps_the_sequence_pending);
    RUN_TEST(test_ready_ends_the_sequence);
    RUN_TEST(test_failed_ends_the_sequence_as_rejected);
    RUN_TEST(test_a_sensor_that_lost_its_tare_counts_as_rejected);
    RUN_TEST(test_a_report_with_nothing_pending_is_ignored);
    RUN_TEST(test_times_out_after_the_limit_and_then_finishes);
    RUN_TEST(test_timeout_wins_over_a_sample_that_was_due);
    RUN_TEST(test_cancel_stops_the_sequence);
    RUN_TEST(test_a_new_start_is_accepted_after_cancel_or_finish);
    RUN_TEST(test_works_across_the_millis_rollover);
    RUN_TEST(test_full_run_with_four_samples_fits_in_the_timeout);
    return UNITY_END();
}
