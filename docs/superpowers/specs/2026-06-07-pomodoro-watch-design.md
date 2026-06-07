# 番茄钟手表设计规格（Waveshare ESP32-S3-Touch-AMOLED-2.06）

- 日期：2026-06-07
- 目标板：Waveshare ESP32-S3-Touch-AMOLED-2.06
- 框架：ESP-IDF（本机 v5.5.2，官方例程基线 v5.4.2，兼容）

## 1. 目标

在该手表开发板上实现一个番茄钟：屏幕中央一个倒计时圆环 + 剩余时间（MM:SS），
屏幕下方两个触摸按钮控制 开始/暂停 与 重置。经典 25/5 工作-休息循环，自动交替。

## 2. 硬件与依赖

- 显示：CO5300 AMOLED，410×502，QSPI 接口。
- 触摸：FT3168 电容触摸，I2C。
- 这两者由官方 BSP 组件 `waveshare/esp32_s3_touch_amoled_2_06`（>= 1.0.7）封装，
  自带 CO5300 + FT3168 驱动与 LVGL v9 移植；BSP 负责引脚/初始化，工程不硬编码 GPIO。
- 图形库：`lvgl/lvgl` 9.2.0（由 BSP 作为依赖引入；arc 控件画圆环）。
- 文字一律英文/符号（Focus/Break、Start/Pause/Reset），使用 LVGL 内置 Montserrat 字体，
  无需额外中文字库。
- IMU(QMI8658)/RTC(PCF85063)/PMIC 本项目不使用。

## 3. 架构（三层，逻辑与硬件解耦）

```
main.c          BSP 初始化 + 胶水层
  bsp 初始化 → lv_disp + 触摸 indev
  lv_timer(100ms) 驱动 fsm_tick + ui_render
  按钮回调 → 调用 fsm
       │                         │
  pomodoro_fsm (纯 C)        ui (LVGL)
  无 ESP/LVGL 依赖           arc 圆环 + MM:SS + 阶段标签 + 两按钮 + 闪烁动画
  → 可主机端单元测试
```

数据流：`lv_timer` → `bsp_display_lock()` → `fsm_tick(dt)` →
（若返回 PHASE_FINISHED：触发闪烁动画，阶段已在 fsm 内自动切换）→
`ui_render(snapshot)` → `bsp_display_unlock()`。

### 目录结构

```
pomodoro-watch/
├── CMakeLists.txt              # 顶层工程
├── sdkconfig.defaults         # PSRAM/flash/分区等默认
├── partitions.csv             # 单 app 分区（带足够空间）
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # 依赖 BSP + lvgl
│   ├── main.c                 # app_main + 胶水 + 闪烁动画
│   ├── ui.c / ui.h            # LVGL 控件构建与刷新
├── components/
│   └── pomodoro_fsm/
│       ├── CMakeLists.txt
│       ├── include/pomodoro_fsm.h
│       ├── pomodoro_fsm.c
│       └── test/              # 主机端 gcc 单测
│           ├── Makefile
│           └── test_pomodoro_fsm.c
├── docs/superpowers/specs/...
└── README.md
```

## 4. pomodoro_fsm（纯逻辑模块）

### 类型

```c
typedef enum { POMO_PHASE_WORK, POMO_PHASE_BREAK } pomo_phase_t;
typedef enum { POMO_IDLE, POMO_RUNNING, POMO_PAUSED } pomo_status_t;
typedef enum { POMO_EVT_NONE, POMO_EVT_PHASE_FINISHED } pomo_event_t;

typedef struct {
    pomo_phase_t  phase;
    pomo_status_t status;
    uint32_t      remaining_ms;
    uint32_t      total_ms;          // 当前阶段满时长
    uint32_t      completed_work;    // 已完成的 WORK 段数
} pomo_snapshot_t;
```

### 时长（宏，便于修改）

- `POMO_WORK_MS`  = 25 * 60 * 1000
- `POMO_BREAK_MS` =  5 * 60 * 1000
- `POMO_MAX_TICK_MS` = 2000（单次 tick 的 dt 上限钳制，防休眠后跳变）

### 接口

```c
void            pomo_init(pomo_fsm_t *f);                 // WORK, IDLE, remaining=total=WORK_MS
void            pomo_toggle(pomo_fsm_t *f);               // IDLE/PAUSED->RUNNING; RUNNING->PAUSED
void            pomo_reset(pomo_fsm_t *f);                // remaining=当前阶段 total, status=PAUSED
pomo_event_t    pomo_tick(pomo_fsm_t *f, uint32_t dt_ms); // 仅 RUNNING 递减
pomo_snapshot_t pomo_snapshot(const pomo_fsm_t *f);
```

### 行为规则

- `toggle`：在 IDLE 或 PAUSED 时进入 RUNNING；在 RUNNING 时进入 PAUSED。
- `reset`：把 `remaining_ms` 设回当前阶段 `total_ms`，状态置 PAUSED（停住）。
- `tick(dt)`：
  - 非 RUNNING：不变，返回 POMO_EVT_NONE。
  - `dt` 先按 `POMO_MAX_TICK_MS` 钳制。
  - `remaining_ms -= dt`（饱和到 0，不下溢）。
  - 若到达 0：
    - 若当前是 WORK：`completed_work++`，切到 BREAK；
    - 若当前是 BREAK：切到 WORK；
    - 新阶段 `remaining_ms = total_ms = 新阶段满时长`，`status` 保持 RUNNING（自动续跑）。
    - 返回 POMO_EVT_PHASE_FINISHED。
  - 否则返回 POMO_EVT_NONE。
- 经典 25/5 简单交替，不实现「4 个后长休息」（YAGNI；如需后续扩展）。

## 5. UI（LVGL v9）

布局（410×502 竖屏，自上而下）：

- 顶部：阶段标签 `Focus` / `Break` + 计数 `#N`（N = completed_work + 1，运行/专注语境）。
- 中部：`lv_arc` 圆环。背景弧整圈淡灰；指示弧从满随 `remaining/total` 递减。
  范围 0–1000，每帧设值。圆心叠加大号 `MM:SS` 文本（Montserrat 48）。
- 底部：两个圆角按钮。
  - 「开始/暂停」按钮：文字随状态在 `Start` / `Pause` / `Resume` 间切换。
  - 「重置」按钮：固定 `Reset`。

配色随阶段：

- WORK：番茄红/橙 `0xFF5A36`
- BREAK：绿 `0x36C76F`
- 指示弧、主按钮主色、阶段标签颜色都跟随阶段色。

结束提示（屏幕闪烁+变色）：

- 收到 PHASE_FINISHED 时，对整屏背景/圆环做约 1.2 秒的 3 次明暗闪烁（`lv_anim`
  改背景不透明度或颜色），随后按新阶段色刷新（阶段已在 fsm 内切换）。

`ui` 接口（建议）：

```c
void ui_create(void);                      // 构建所有控件
void ui_render(const pomo_snapshot_t *s);  // 按快照刷新文字/弧值/配色
void ui_flash(void);                       // 触发结束闪烁动画
// 按钮回调通过函数指针/弱回调通知 main，再调用 fsm
```

## 6. 主循环（main.c）

- `app_main`：
  1. BSP 初始化显示+触摸（`bsp_display_start()` 返回 LVGL disp，触摸自动注册为 indev）。
  2. 设置背光亮度。
  3. `bsp_display_lock()` 期间 `ui_create()`、`pomo_init()`，`bsp_display_unlock()`。
  4. 创建 `lv_timer`，周期 100ms。
- `lv_timer` 回调：
  1. 用 `esp_timer_get_time()` 计算自上次的真实 `dt_ms`。
  2. `evt = pomo_tick(&fsm, dt)`。
  3. 若 `evt == PHASE_FINISHED`：`ui_flash()`。
  4. `ui_render(pomo_snapshot(&fsm))`。
  - （回调在 LVGL 任务上下文，已持锁，无需再 lock。）
- 按钮回调：`Start/Pause` → `pomo_toggle`；`Reset` → `pomo_reset`；随后 `ui_render`。

## 7. 错误处理

- BSP/显示初始化用 `ESP_ERROR_CHECK`，失败记录并复位。
- `dt` 上限钳制（`POMO_MAX_TICK_MS`）。
- 倒计时显示对 0 钳制；`remaining_ms` 饱和减法不下溢。

## 8. 测试

### 主机端单元测试（components/pomodoro_fsm/test，gcc）

先写测试再写实现（TDD）。覆盖：

1. `pomo_init` 初值：WORK / IDLE / remaining==total==WORK_MS / completed_work==0。
2. `toggle`：IDLE→RUNNING；RUNNING→PAUSED；PAUSED→RUNNING。
3. `tick` 仅在 RUNNING 递减；非 RUNNING 不变。
4. `tick` 饱和到 0 不下溢。
5. 跨零：WORK 跑完返回 PHASE_FINISHED、切 BREAK、total=BREAK_MS、completed_work==1、保持 RUNNING。
6. BREAK 跑完切回 WORK，completed_work 不再加。
7. `reset`：remaining 回当前阶段 total，status==PAUSED。
8. `dt` 钳制：传入超大 dt 只扣 `POMO_MAX_TICK_MS`。

测试用纯 gcc 编译运行（`make` in test/），断言失败非零退出。

### 设备端手动验收清单

- 上电显示 25:00、Focus、圆环满、停住。
- 点 Start：圆环随秒递减，时间走动，按钮变 Pause。
- 点 Pause：暂停，按钮变 Resume。
- 点 Reset：回到当前阶段满时长并暂停。
- 到点：屏幕闪烁，自动切 Break（绿色）并继续；Break 到点切回 Focus（红色），计数 +1。
- 触摸响应准确（按钮命中区域合理）。

## 9. 交付与构建

- 顶层 `idf.py set-target esp32s3 && idf.py build flash monitor`（需先 source IDF 环境）。
- 主机测试：`cd components/pomodoro_fsm/test && make && ./test_pomodoro_fsm`。
- README 说明环境激活、编译、烧录、跑测试步骤。
- 本会话不替用户烧录硬件；提供命令与验收清单。

## 10. 非目标（YAGNI）

- 4 段后长休息、声音/震动提示、设置界面调时长、IMU 抬腕亮屏、RTC 时钟、低功耗休眠、
  配置持久化（NVS）。均留作后续可扩展项。
