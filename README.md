# 番茄钟手表（ESP32-S3-Touch-AMOLED-2.06）

中央倒计时圆环 + MM:SS，触摸按钮控制 开始/暂停/重置，经典 25/5 工作-休息自动循环。

## 硬件
Waveshare ESP32-S3-Touch-AMOLED-2.06（CO5300 AMOLED 410×502 / FT3168 触摸），
显示与触摸由官方 BSP `waveshare/esp32_s3_touch_amoled_2_06` 驱动。

## 编译与烧录
```bash
source ~/esp/esp-idf/export.sh   # 激活 ESP-IDF v5.4+（本仓基于 v5.5.2）
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # 串口按实际调整
```

## 主机端逻辑测试（无需硬件）
```bash
source ~/esp/esp-idf/export.sh
cd test
idf.py --preview set-target linux
idf.py build
./build/pomodoro_fsm_test.elf
```

## 代码结构
- `components/pomodoro_fsm/` 纯计时状态机（含主机 Unity 测试）
- `main/ui.{h,c}` LVGL 界面
- `main/main.c` BSP 初始化与胶水

## 调整时长
改 `components/pomodoro_fsm/include/pomodoro_fsm.h` 的 `POMO_WORK_MS` / `POMO_BREAK_MS`。

## 备注
- LVGL 版本为 9.3.x（由 BSP 传递依赖 esp_lvgl_port 决定）。
- 界面文字采用英文/符号（Focus/Break、Start/Pause/Resume/Reset），不引入中文字库。
