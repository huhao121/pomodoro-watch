#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 时长（毫秒）。改这里即可调整番茄钟节奏。 */
#define POMO_WORK_MS      (25u * 60u * 1000u)
#define POMO_BREAK_MS     (5u * 60u * 1000u)
/* 单次 tick 的 dt 上限，防止休眠/卡顿后时间跳变。 */
#define POMO_MAX_TICK_MS  (2000u)

typedef enum { POMO_PHASE_WORK, POMO_PHASE_BREAK } pomo_phase_t;
typedef enum { POMO_IDLE, POMO_RUNNING, POMO_PAUSED } pomo_status_t;
typedef enum { POMO_EVT_NONE, POMO_EVT_PHASE_FINISHED } pomo_event_t;

typedef struct {
    pomo_phase_t  phase;
    pomo_status_t status;
    uint32_t      remaining_ms;
    uint32_t      total_ms;        /* 当前阶段满时长 */
    uint32_t      completed_work;  /* 已完成的 WORK 段数 */
} pomo_fsm_t;

typedef struct {
    pomo_phase_t  phase;
    pomo_status_t status;
    uint32_t      remaining_ms;
    uint32_t      total_ms;
    uint32_t      completed_work;
} pomo_snapshot_t;

void pomo_init(pomo_fsm_t *f);
void pomo_toggle(pomo_fsm_t *f);
void pomo_reset(pomo_fsm_t *f);
pomo_event_t pomo_tick(pomo_fsm_t *f, uint32_t dt_ms);
pomo_snapshot_t pomo_snapshot(const pomo_fsm_t *f);

#ifdef __cplusplus
}
#endif
