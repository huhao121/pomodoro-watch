#include "pomodoro_fsm.h"
#include <string.h>

void pomo_init(pomo_fsm_t *f)
{
    (void)f;
}

void pomo_toggle(pomo_fsm_t *f)
{
    (void)f;
}

void pomo_reset(pomo_fsm_t *f)
{
    (void)f;
}

pomo_event_t pomo_tick(pomo_fsm_t *f, uint32_t dt_ms)
{
    (void)f;
    (void)dt_ms;
    return POMO_EVT_NONE;
}

pomo_snapshot_t pomo_snapshot(const pomo_fsm_t *f)
{
    pomo_snapshot_t s;
    memset(&s, 0, sizeof(s));
    (void)f;
    return s;
}
