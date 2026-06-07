#include "unity.h"
#include "pomodoro_fsm.h"

void setUp(void) {}
void tearDown(void) {}

/* 1. init 后字段默认值正确 */
void test_init_defaults(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    TEST_ASSERT_EQUAL(POMO_PHASE_WORK, f.phase);
    TEST_ASSERT_EQUAL(POMO_IDLE, f.status);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.remaining_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.total_ms);
    TEST_ASSERT_EQUAL_UINT32(0, f.completed_work);
}

/* 2. toggle 循环：IDLE→RUNNING→PAUSED→RUNNING */
void test_toggle_cycles_states(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    TEST_ASSERT_EQUAL(POMO_IDLE, f.status);

    pomo_toggle(&f);
    TEST_ASSERT_EQUAL(POMO_RUNNING, f.status);

    pomo_toggle(&f);
    TEST_ASSERT_EQUAL(POMO_PAUSED, f.status);

    pomo_toggle(&f);
    TEST_ASSERT_EQUAL(POMO_RUNNING, f.status);
}

/* 3. IDLE 时 tick 不动；RUNNING 后 tick 减少 remaining */
void test_tick_only_runs_when_running(void)
{
    pomo_fsm_t f;
    pomo_init(&f);

    pomo_event_t evt = pomo_tick(&f, 1000);
    TEST_ASSERT_EQUAL(POMO_EVT_NONE, evt);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.remaining_ms);

    pomo_toggle(&f); /* → RUNNING */
    evt = pomo_tick(&f, 1000);
    TEST_ASSERT_EQUAL(POMO_EVT_NONE, evt);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS - 1000u, f.remaining_ms);
}

/* 4. dt 超过 POMO_MAX_TICK_MS 时钳制 */
void test_tick_clamps_dt(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f); /* → RUNNING */

    pomo_tick(&f, 999999999u);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS - POMO_MAX_TICK_MS, f.remaining_ms);
}

/* 5. remaining==500 时 tick(500) 触发 PHASE_FINISHED */
void test_tick_saturates_to_zero(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    f.remaining_ms = 500u;
    pomo_toggle(&f); /* → RUNNING */

    pomo_event_t evt = pomo_tick(&f, 500u);
    TEST_ASSERT_EQUAL(POMO_EVT_PHASE_FINISHED, evt);
}

/* 6. WORK 跑完切到 BREAK，completed_work++ */
void test_work_finishes_to_break(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    f.remaining_ms = 100u;
    pomo_toggle(&f); /* → RUNNING */

    pomo_event_t evt = pomo_tick(&f, 100u);
    TEST_ASSERT_EQUAL(POMO_EVT_PHASE_FINISHED, evt);
    TEST_ASSERT_EQUAL(POMO_PHASE_BREAK, f.phase);
    TEST_ASSERT_EQUAL_UINT32(POMO_BREAK_MS, f.total_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_BREAK_MS, f.remaining_ms);
    TEST_ASSERT_EQUAL(POMO_RUNNING, f.status);
    TEST_ASSERT_EQUAL_UINT32(1u, f.completed_work);
}

/* 7. BREAK 跑完切回 WORK，completed_work 不再加 */
void test_break_finishes_back_to_work(void)
{
    pomo_fsm_t f;
    pomo_init(&f);

    /* 先跑完 WORK */
    f.remaining_ms = 100u;
    pomo_toggle(&f); /* → RUNNING */
    pomo_tick(&f, 100u); /* → BREAK */

    /* 再跑完 BREAK */
    f.remaining_ms = 100u;
    pomo_tick(&f, 100u); /* → WORK */

    TEST_ASSERT_EQUAL(POMO_PHASE_WORK, f.phase);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.total_ms);
    TEST_ASSERT_EQUAL_UINT32(1u, f.completed_work); /* BREAK 结束不加计数 */
}

/* 8. reset 恢复 remaining=total、status=PAUSED */
void test_reset_restores_phase_total_and_pauses(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f); /* → RUNNING */
    f.remaining_ms = 1234u;

    pomo_reset(&f);
    TEST_ASSERT_EQUAL_UINT32(f.total_ms, f.remaining_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.remaining_ms);
    TEST_ASSERT_EQUAL(POMO_PAUSED, f.status);
}

/* 9. snapshot 反映当前状态 */
void test_snapshot_reflects_state(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    f.remaining_ms = 100u;
    pomo_toggle(&f); /* → RUNNING */
    pomo_tick(&f, 100u); /* WORK 结束 → BREAK, RUNNING */

    pomo_snapshot_t s = pomo_snapshot(&f);
    TEST_ASSERT_EQUAL(POMO_PHASE_BREAK, s.phase);
    TEST_ASSERT_EQUAL_UINT32(1u, s.completed_work);
    TEST_ASSERT_EQUAL_UINT32(POMO_BREAK_MS, s.remaining_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_BREAK_MS, s.total_ms);
    TEST_ASSERT_EQUAL(POMO_RUNNING, s.status);
}

/* 10. PAUSED 时 tick 不消耗 remaining */
void test_tick_paused_does_nothing(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f); /* → RUNNING */
    pomo_toggle(&f); /* → PAUSED */

    uint32_t remaining_before = f.remaining_ms;
    pomo_event_t evt = pomo_tick(&f, 1000u);
    TEST_ASSERT_EQUAL(POMO_EVT_NONE, evt);
    TEST_ASSERT_EQUAL_UINT32(remaining_before, f.remaining_ms);
}

/* 11. IDLE 时 reset 仍保持 IDLE */
void test_reset_from_idle_stays_idle(void)
{
    pomo_fsm_t f;
    pomo_init(&f);                /* status = IDLE */
    f.remaining_ms = 12345u;      /* 白盒篡改 */

    pomo_reset(&f);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.remaining_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, f.total_ms);
    TEST_ASSERT_EQUAL(POMO_IDLE, f.status);
}

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_toggle_cycles_states);
    RUN_TEST(test_tick_only_runs_when_running);
    RUN_TEST(test_tick_clamps_dt);
    RUN_TEST(test_tick_saturates_to_zero);
    RUN_TEST(test_work_finishes_to_break);
    RUN_TEST(test_break_finishes_back_to_work);
    RUN_TEST(test_reset_restores_phase_total_and_pauses);
    RUN_TEST(test_snapshot_reflects_state);
    RUN_TEST(test_tick_paused_does_nothing);
    RUN_TEST(test_reset_from_idle_stays_idle);
    UNITY_END();
}
