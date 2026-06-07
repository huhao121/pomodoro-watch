#pragma once

#include "lvgl.h"
#include "pomodoro_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 构建所有控件；两个按钮的点击回调由调用方（main）提供。
   必须在持有 LVGL 锁的上下文中调用。 */
void ui_create(lv_event_cb_t on_toggle, lv_event_cb_t on_reset);

/* 按快照刷新：圆环值、MM:SS、阶段标签、按钮文字与配色。 */
void ui_render(const pomo_snapshot_t *s);

/* 阶段结束时的全屏闪烁（在顶层图层做 3 次明暗）。颜色由 phase 决定，内部使用 phase_color()。 */
void ui_flash(pomo_phase_t phase);

#ifdef __cplusplus
}
#endif
