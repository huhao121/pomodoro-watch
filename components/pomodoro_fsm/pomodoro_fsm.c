#include "pomodoro_fsm.h"

void pomo_init(pomo_fsm_t *f)
{
    f->phase        = POMO_PHASE_WORK;
    f->status       = POMO_IDLE;
    f->total_ms     = POMO_WORK_MS;
    f->remaining_ms = POMO_WORK_MS;
    f->completed_work = 0u;
}

void pomo_toggle(pomo_fsm_t *f)
{
    if (f->status == POMO_RUNNING) {
        f->status = POMO_PAUSED;
    } else {
        f->status = POMO_RUNNING;
    }
}

void pomo_reset(pomo_fsm_t *f)
{
    f->remaining_ms = f->total_ms;
    if (f->status != POMO_IDLE) {
        f->status = POMO_PAUSED;
    }
}

pomo_event_t pomo_tick(pomo_fsm_t *f, uint32_t dt_ms)
{
    if (f->status != POMO_RUNNING) {
        return POMO_EVT_NONE;
    }

    /* 钳制 dt，防止时间跳变 */
    if (dt_ms > POMO_MAX_TICK_MS) {
        dt_ms = POMO_MAX_TICK_MS;
    }

    if (dt_ms < f->remaining_ms) {
        f->remaining_ms -= dt_ms;
        return POMO_EVT_NONE;
    }

    /* 阶段结束：切换阶段 */
    if (f->phase == POMO_PHASE_WORK) {
        f->completed_work++;
        f->phase    = POMO_PHASE_BREAK;
        f->total_ms = POMO_BREAK_MS;
    } else {
        f->phase    = POMO_PHASE_WORK;
        f->total_ms = POMO_WORK_MS;
    }
    f->remaining_ms = f->total_ms;
    /* status 保持 RUNNING */

    return POMO_EVT_PHASE_FINISHED;
}

pomo_snapshot_t pomo_snapshot(const pomo_fsm_t *f)
{
    pomo_snapshot_t s;
    s.phase          = f->phase;
    s.status         = f->status;
    s.remaining_ms   = f->remaining_ms;
    s.total_ms       = f->total_ms;
    s.completed_work = f->completed_work;
    return s;
}
