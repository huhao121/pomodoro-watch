#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "pomodoro_fsm.h"
#include "ui.h"

static const char *TAG = "pomodoro";

static pomo_fsm_t s_fsm;
static int64_t   s_last_us;

static lv_color_t phase_color(pomo_phase_t p)
{
    return (p == POMO_PHASE_WORK) ? lv_color_hex(0xFF5A36) : lv_color_hex(0x36C76F);
}

static void refresh(void)
{
    pomo_snapshot_t s = pomo_snapshot(&s_fsm);
    ui_render(&s);
}

static void on_toggle(lv_event_t *e)
{
    (void)e;
    pomo_toggle(&s_fsm);
    refresh();
}

static void on_reset(lv_event_t *e)
{
    (void)e;
    pomo_reset(&s_fsm);
    refresh();
}

static void tick_timer_cb(lv_timer_t *t)
{
    (void)t;
    int64_t now = esp_timer_get_time();
    uint32_t dt_ms = (uint32_t)((now - s_last_us) / 1000);
    s_last_us = now;

    pomo_event_t ev = pomo_tick(&s_fsm, dt_ms);
    if (ev == POMO_EVT_PHASE_FINISHED) {
        pomo_snapshot_t after = pomo_snapshot(&s_fsm);
        ui_flash(phase_color(after.phase));
    }
    refresh();
}

void app_main(void)
{
    ESP_LOGI(TAG, "booting pomodoro watch");
    bsp_display_start();
    bsp_display_brightness_set(100);

    pomo_init(&s_fsm);

    bsp_display_lock(0);
    ui_create(on_toggle, on_reset);
    refresh();
    s_last_us = esp_timer_get_time();
    lv_timer_create(tick_timer_cb, 100, NULL);
    bsp_display_unlock();

    ESP_LOGI(TAG, "ui ready");
}
