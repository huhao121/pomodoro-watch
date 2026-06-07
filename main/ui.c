#include "ui.h"
#include <stdio.h>

#define COLOR_WORK   lv_color_hex(0xFF5A36)   /* 番茄红/橙 */
#define COLOR_BREAK  lv_color_hex(0x36C76F)   /* 绿 */
#define COLOR_BG     lv_color_hex(0x101014)
#define COLOR_ARC_BG lv_color_hex(0x2A2A30)

static lv_obj_t *s_arc;
static lv_obj_t *s_time_label;
static lv_obj_t *s_phase_label;
static lv_obj_t *s_btn_toggle;
static lv_obj_t *s_btn_toggle_label;
static lv_obj_t *s_overlay;     /* 闪烁用全屏覆盖层（顶层图层） */

static lv_color_t phase_color(pomo_phase_t p)
{
    return (p == POMO_PHASE_WORK) ? COLOR_WORK : COLOR_BREAK;
}

void ui_create(lv_event_cb_t on_toggle, lv_event_cb_t on_reset)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_phase_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_phase_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_phase_label, COLOR_WORK, 0);
    lv_label_set_text(s_phase_label, "Focus #1");
    lv_obj_align(s_phase_label, LV_ALIGN_TOP_MID, 0, 30);

    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 320, 320);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -10);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_value(s_arc, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB | LV_STATE_ANY);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COLOR_WORK, LV_PART_INDICATOR);

    s_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_label_set_text(s_time_label, "25:00");
    lv_obj_align_to(s_time_label, s_arc, LV_ALIGN_CENTER, 0, 0);

    s_btn_toggle = lv_button_create(scr);
    lv_obj_set_size(s_btn_toggle, 150, 70);
    lv_obj_align(s_btn_toggle, LV_ALIGN_BOTTOM_MID, -85, -40);
    lv_obj_set_style_bg_color(s_btn_toggle, COLOR_WORK, 0);
    lv_obj_add_event_cb(s_btn_toggle, on_toggle, LV_EVENT_CLICKED, NULL);
    s_btn_toggle_label = lv_label_create(s_btn_toggle);
    lv_obj_set_style_text_font(s_btn_toggle_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_btn_toggle_label, "Start");
    lv_obj_center(s_btn_toggle_label);

    lv_obj_t *btn_reset = lv_button_create(scr);
    lv_obj_set_size(btn_reset, 150, 70);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_MID, 85, -40);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x444450), 0);
    lv_obj_add_event_cb(btn_reset, on_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = lv_label_create(btn_reset);
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_center(reset_label);

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, COLOR_WORK, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_render(const pomo_snapshot_t *s)
{
    lv_color_t col = phase_color(s->phase);

    uint32_t v = 0;
    if (s->total_ms > 0) {
        v = (uint32_t)(((uint64_t)s->remaining_ms * 1000u) / s->total_ms);
    }
    lv_arc_set_value(s_arc, (int32_t)v);
    lv_obj_set_style_arc_color(s_arc, col, LV_PART_INDICATOR);

    uint32_t total_s = (s->remaining_ms + 999u) / 1000u;
    char buf[9];
    snprintf(buf, sizeof(buf), "%02u:%02u",
             (unsigned)(total_s / 60u), (unsigned)(total_s % 60u));
    lv_label_set_text(s_time_label, buf);

    char phase_buf[16];
    if (s->phase == POMO_PHASE_WORK) {
        snprintf(phase_buf, sizeof(phase_buf), "Focus #%u",
                 (unsigned)(s->completed_work + 1u));
    } else {
        snprintf(phase_buf, sizeof(phase_buf), "Break");
    }
    lv_label_set_text(s_phase_label, phase_buf);
    lv_obj_set_style_text_color(s_phase_label, col, 0);

    const char *txt = "Start";
    if (s->status == POMO_RUNNING)      txt = "Pause";
    else if (s->status == POMO_PAUSED)  txt = "Resume";
    lv_label_set_text(s_btn_toggle_label, txt);
    lv_obj_set_style_bg_color(s_btn_toggle, col, 0);
}

static void flash_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void flash_ready_cb(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

void ui_flash(pomo_phase_t phase)
{
    lv_obj_set_style_bg_color(s_overlay, phase_color(phase), 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, flash_opa_cb);
    lv_anim_set_values(&a, 0, 180);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_reverse_duration(&a, 150);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_completed_cb(&a, flash_ready_cb);
    lv_anim_start(&a);
}
