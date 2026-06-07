#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "pomodoro";

void app_main(void)
{
    ESP_LOGI(TAG, "booting pomodoro watch");
    bsp_display_start();          /* 初始化 CO5300 + FT3168 + LVGL，返回 lv_display_t* */
    bsp_display_brightness_set(100);
    ESP_LOGI(TAG, "display started");
}
