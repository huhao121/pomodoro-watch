# 番茄钟手表 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 Waveshare ESP32-S3-Touch-AMOLED-2.06 上做番茄钟：中央倒计时圆环 + MM:SS 剩余时间，屏下两个触摸按钮控制 开始/暂停 与 重置，经典 25/5 工作-休息自动循环。

**架构：** 三层解耦——纯 C 状态机 `pomodoro_fsm`（无 ESP/LVGL 依赖，主机端 Unity 测试）、LVGL v9 UI 层（arc 圆环 + 标签 + 按钮 + 闪烁动画）、`main.c` 胶水层（官方 BSP 初始化 + `lv_timer` 驱动）。

**技术栈：** ESP-IDF v5.5.2（本机）；官方 BSP `waveshare/esp32_s3_touch_amoled_2_06` >=1.0.7（封装 CO5300 显示 + FT3168 触摸 + LVGL v9.2 移植）；LVGL 9.2；Unity（ESP-IDF Linux 目标做主机测试）。

**规格来源：** `docs/superpowers/specs/2026-06-07-pomodoro-watch-design.md`

---

## 文件结构

```
pomodoro-watch/
├── CMakeLists.txt                              # 顶层固件工程
├── sdkconfig.defaults                          # PSRAM/flash/字体/分区默认
├── partitions.csv                              # 16MB 单 app 分区
├── main/
│   ├── CMakeLists.txt                          # 注册 main.c + ui.c
│   ├── idf_component.yml                        # 依赖 BSP + lvgl
│   ├── main.c                                  # app_main + 胶水 + tick 定时器
│   ├── ui.h                                    # UI 层接口
│   └── ui.c                                    # LVGL 控件构建/刷新/闪烁
├── components/
│   └── pomodoro_fsm/
│       ├── CMakeLists.txt                       # 纯逻辑组件
│       ├── include/pomodoro_fsm.h               # 类型 + 接口
│       └── pomodoro_fsm.c                       # 实现
├── test/                                        # 主机测试工程（Linux 目标）
│   ├── CMakeLists.txt
│   └── main/
│       ├── CMakeLists.txt                       # REQUIRES unity pomodoro_fsm
│       └── test_pomodoro_fsm.c                  # Unity 用例 + app_main runner
├── docs/superpowers/...
└── README.md
```

各文件职责：
- `components/pomodoro_fsm/*`：唯一的业务逻辑所在，纯函数，可主机测试。
- `main/ui.*`：只做「控件构建」与「按快照刷新」，不含计时逻辑。
- `main/main.c`：硬件初始化 + 把 fsm 与 ui 接起来。
- `test/*`：在 ESP-IDF Linux 目标上跑 Unity，验证 fsm。

**测试策略说明：** `pomodoro_fsm` 纯逻辑用 TDD（先红后绿）跑主机 Unity。UI/main 依赖真实显示与 LVGL，无法在主机有意义地单测——这些任务以「编译通过 + 设备端手动验收清单」验证（计划末尾的清单），不假装写无意义的单测。

---

## 任务 1：pomodoro_fsm 头文件与组件骨架

**文件：**
- 创建：`components/pomodoro_fsm/CMakeLists.txt`
- 创建：`components/pomodoro_fsm/include/pomodoro_fsm.h`
- 创建：`components/pomodoro_fsm/pomodoro_fsm.c`（先建空桩，仅保证可编译）

- [ ] **步骤 1：写组件 CMakeLists**

`components/pomodoro_fsm/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "pomodoro_fsm.c"
                       INCLUDE_DIRS "include")
```

- [ ] **步骤 2：写头文件（类型 + 接口）**

`components/pomodoro_fsm/include/pomodoro_fsm.h`：

```c
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

/* 快照与 fsm 字段一致，供 UI 只读渲染。 */
typedef struct {
    pomo_phase_t  phase;
    pomo_status_t status;
    uint32_t      remaining_ms;
    uint32_t      total_ms;
    uint32_t      completed_work;
} pomo_snapshot_t;

/* WORK / IDLE / remaining==total==WORK_MS / completed_work==0 */
void pomo_init(pomo_fsm_t *f);

/* IDLE 或 PAUSED -> RUNNING；RUNNING -> PAUSED */
void pomo_toggle(pomo_fsm_t *f);

/* remaining_ms 回到当前阶段 total_ms，status 置 PAUSED */
void pomo_reset(pomo_fsm_t *f);

/* 仅 RUNNING 时递减；dt 先按 POMO_MAX_TICK_MS 钳制；
   减到 0 时切换阶段（WORK->BREAK->WORK），新阶段满时长且保持 RUNNING，
   返回 POMO_EVT_PHASE_FINISHED，否则 POMO_EVT_NONE。 */
pomo_event_t pomo_tick(pomo_fsm_t *f, uint32_t dt_ms);

pomo_snapshot_t pomo_snapshot(const pomo_fsm_t *f);

#ifdef __cplusplus
}
#endif
```

- [ ] **步骤 3：写空实现桩（让链接通过，逻辑留到后续 TDD 任务填）**

`components/pomodoro_fsm/pomodoro_fsm.c`：

```c
#include "pomodoro_fsm.h"

void pomo_init(pomo_fsm_t *f) { (void)f; }
void pomo_toggle(pomo_fsm_t *f) { (void)f; }
void pomo_reset(pomo_fsm_t *f) { (void)f; }
pomo_event_t pomo_tick(pomo_fsm_t *f, uint32_t dt_ms) { (void)f; (void)dt_ms; return POMO_EVT_NONE; }

pomo_snapshot_t pomo_snapshot(const pomo_fsm_t *f) {
    pomo_snapshot_t s = { f->phase, f->status, f->remaining_ms, f->total_ms, f->completed_work };
    return s;
}
```

- [ ] **步骤 4：Commit**

```bash
git add components/pomodoro_fsm
git commit -m "feat: pomodoro_fsm 组件骨架与接口"
```

---

## 任务 2：主机测试工程（ESP-IDF Linux 目标 + Unity）

搭建可在 PC 上运行的 Unity 测试工程。此任务结束时应能 `set-target linux` 并构建出可执行文件（此时测试全部断言对空桩，预期会失败——这正是 TDD 的红）。

**文件：**
- 创建：`test/CMakeLists.txt`
- 创建：`test/main/CMakeLists.txt`
- 创建：`test/main/test_pomodoro_fsm.c`

- [ ] **步骤 1：写测试工程顶层 CMakeLists**

`test/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
# 把仓库根的 components/ 暴露给本测试工程
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pomodoro_fsm_test)
```

- [ ] **步骤 2：写测试 main 的 CMakeLists**

`test/main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "test_pomodoro_fsm.c"
                       INCLUDE_DIRS "."
                       REQUIRES unity pomodoro_fsm)
```

- [ ] **步骤 3：写测试入口（先放一个占位用例，确保框架跑通）**

`test/main/test_pomodoro_fsm.c`：

```c
#include "unity.h"
#include "pomodoro_fsm.h"

void setUp(void) {}
void tearDown(void) {}

/* 占位用例：后续任务会替换/追加真实用例。 */
static void test_placeholder_framework_runs(void)
{
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, 25u * 60u * 1000u);
}

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_framework_runs);
    UNITY_END();
}
```

- [ ] **步骤 4：构建并运行（验证 Linux 目标 + Unity 跑通）**

```bash
source ~/esp/esp-idf/export.sh
cd test
idf.py --preview set-target linux
idf.py build
./build/pomodoro_fsm_test.elf
```

预期：编译成功，输出 `1 Tests 0 Failures 0 Ignored` / `OK`。

- [ ] **步骤 5：Commit**

```bash
cd ..
git add test
git commit -m "test: 主机端 Unity 测试工程（Linux 目标）"
```

---

## 任务 3：TDD — pomo_init 与 pomo_snapshot

**文件：**
- 修改：`test/main/test_pomodoro_fsm.c`
- 修改：`components/pomodoro_fsm/pomodoro_fsm.c`

- [ ] **步骤 1：写失败的测试**

把 `test/main/test_pomodoro_fsm.c` 的占位用例替换为：

```c
#include "unity.h"
#include "pomodoro_fsm.h"

void setUp(void) {}
void tearDown(void) {}

static void test_init_defaults(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_snapshot_t s = pomo_snapshot(&f);
    TEST_ASSERT_EQUAL(POMO_PHASE_WORK, s.phase);
    TEST_ASSERT_EQUAL(POMO_IDLE, s.status);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, s.remaining_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, s.total_ms);
    TEST_ASSERT_EQUAL_UINT32(0u, s.completed_work);
}

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults);
    UNITY_END();
}
```

- [ ] **步骤 2：运行验证失败**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：FAIL（空桩 `pomo_init` 未设字段，断言不通过）。

- [ ] **步骤 3：实现 pomo_init**

替换 `components/pomodoro_fsm/pomodoro_fsm.c` 中的 `pomo_init`：

```c
void pomo_init(pomo_fsm_t *f)
{
    f->phase = POMO_PHASE_WORK;
    f->status = POMO_IDLE;
    f->total_ms = POMO_WORK_MS;
    f->remaining_ms = POMO_WORK_MS;
    f->completed_work = 0u;
}
```

- [ ] **步骤 4：运行验证通过**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：`1 Tests 0 Failures` / OK。

- [ ] **步骤 5：Commit**

```bash
git add components/pomodoro_fsm/pomodoro_fsm.c test/main/test_pomodoro_fsm.c
git commit -m "feat: pomo_init 初始化默认值"
```

---

## 任务 4：TDD — pomo_toggle

**文件：**
- 修改：`test/main/test_pomodoro_fsm.c`
- 修改：`components/pomodoro_fsm/pomodoro_fsm.c`

- [ ] **步骤 1：写失败的测试**

在 `test_pomodoro_fsm.c` 中追加用例，并在 `app_main` 里加 `RUN_TEST(test_toggle_cycles_states);`：

```c
static void test_toggle_cycles_states(void)
{
    pomo_fsm_t f;
    pomo_init(&f);                         /* IDLE */
    pomo_toggle(&f);
    TEST_ASSERT_EQUAL(POMO_RUNNING, pomo_snapshot(&f).status);   /* IDLE -> RUNNING */
    pomo_toggle(&f);
    TEST_ASSERT_EQUAL(POMO_PAUSED, pomo_snapshot(&f).status);    /* RUNNING -> PAUSED */
    pomo_toggle(&f);
    TEST_ASSERT_EQUAL(POMO_RUNNING, pomo_snapshot(&f).status);   /* PAUSED -> RUNNING */
}
```

`app_main` 更新为：

```c
void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_toggle_cycles_states);
    UNITY_END();
}
```

- [ ] **步骤 2：运行验证失败**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：`test_toggle_cycles_states` FAIL（空桩不改 status）。

- [ ] **步骤 3：实现 pomo_toggle**

替换 `pomodoro_fsm.c` 中的 `pomo_toggle`：

```c
void pomo_toggle(pomo_fsm_t *f)
{
    if (f->status == POMO_RUNNING) {
        f->status = POMO_PAUSED;
    } else {
        f->status = POMO_RUNNING;
    }
}
```

- [ ] **步骤 4：运行验证通过**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：2 用例全过。

- [ ] **步骤 5：Commit**

```bash
git add components/pomodoro_fsm/pomodoro_fsm.c test/main/test_pomodoro_fsm.c
git commit -m "feat: pomo_toggle 状态切换"
```

---

## 任务 5：TDD — pomo_tick 递减、饱和、dt 钳制

**文件：**
- 修改：`test/main/test_pomodoro_fsm.c`
- 修改：`components/pomodoro_fsm/pomodoro_fsm.c`

- [ ] **步骤 1：写失败的测试**

追加用例并在 `app_main` 注册：

```c
static void test_tick_only_runs_when_running(void)
{
    pomo_fsm_t f;
    pomo_init(&f);                         /* IDLE */
    pomo_event_t ev = pomo_tick(&f, 1000u);
    TEST_ASSERT_EQUAL(POMO_EVT_NONE, ev);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, pomo_snapshot(&f).remaining_ms); /* 未动 */

    pomo_toggle(&f);                       /* RUNNING */
    ev = pomo_tick(&f, 1000u);
    TEST_ASSERT_EQUAL(POMO_EVT_NONE, ev);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS - 1000u, pomo_snapshot(&f).remaining_ms);
}

static void test_tick_clamps_dt(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f);                       /* RUNNING */
    pomo_tick(&f, 999999999u);             /* 远超钳制上限 */
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS - POMO_MAX_TICK_MS,
                             pomo_snapshot(&f).remaining_ms);
}

static void test_tick_saturates_to_zero(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f);                       /* RUNNING */
    f.remaining_ms = 500u;                 /* 白盒：直接置小，便于跨零 */
    pomo_event_t ev = pomo_tick(&f, 500u); /* 恰好到 0 -> 触发结束 */
    TEST_ASSERT_EQUAL(POMO_EVT_PHASE_FINISHED, ev);
}
```

`app_main` 追加三行 `RUN_TEST(...)`。

- [ ] **步骤 2：运行验证失败**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：新增三个用例 FAIL（空桩 `pomo_tick` 恒返回 NONE 且不改 remaining）。

- [ ] **步骤 3：实现 pomo_tick（含阶段切换，供本任务与任务 6 共用）**

在 `pomodoro_fsm.c` 顶部（`#include` 之后）加私有助手，并替换 `pomo_tick`：

```c
static uint32_t phase_total_ms(pomo_phase_t p)
{
    return (p == POMO_PHASE_WORK) ? POMO_WORK_MS : POMO_BREAK_MS;
}

pomo_event_t pomo_tick(pomo_fsm_t *f, uint32_t dt_ms)
{
    if (f->status != POMO_RUNNING) {
        return POMO_EVT_NONE;
    }
    if (dt_ms > POMO_MAX_TICK_MS) {
        dt_ms = POMO_MAX_TICK_MS;
    }
    if (dt_ms < f->remaining_ms) {
        f->remaining_ms -= dt_ms;
        return POMO_EVT_NONE;
    }
    /* 到点：切换阶段，新阶段满时长，保持 RUNNING 自动续跑 */
    if (f->phase == POMO_PHASE_WORK) {
        f->completed_work += 1u;
        f->phase = POMO_PHASE_BREAK;
    } else {
        f->phase = POMO_PHASE_WORK;
    }
    f->total_ms = phase_total_ms(f->phase);
    f->remaining_ms = f->total_ms;
    return POMO_EVT_PHASE_FINISHED;
}
```

- [ ] **步骤 4：运行验证通过**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：全部用例通过。

- [ ] **步骤 5：Commit**

```bash
git add components/pomodoro_fsm/pomodoro_fsm.c test/main/test_pomodoro_fsm.c
git commit -m "feat: pomo_tick 递减/dt钳制/跨零结束"
```

---

## 任务 6：TDD — 阶段切换（WORK↔BREAK）与 completed_work

逻辑已在任务 5 的 `pomo_tick` 实现，本任务专注补齐切换语义的断言（红→已绿，验证实现正确）。

**文件：**
- 修改：`test/main/test_pomodoro_fsm.c`

- [ ] **步骤 1：写测试**

追加用例并在 `app_main` 注册：

```c
static void test_work_finishes_to_break(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f);                       /* RUNNING, WORK */
    f.remaining_ms = 100u;
    pomo_event_t ev = pomo_tick(&f, 100u);
    pomo_snapshot_t s = pomo_snapshot(&f);
    TEST_ASSERT_EQUAL(POMO_EVT_PHASE_FINISHED, ev);
    TEST_ASSERT_EQUAL(POMO_PHASE_BREAK, s.phase);
    TEST_ASSERT_EQUAL_UINT32(POMO_BREAK_MS, s.total_ms);
    TEST_ASSERT_EQUAL_UINT32(POMO_BREAK_MS, s.remaining_ms);
    TEST_ASSERT_EQUAL(POMO_RUNNING, s.status);   /* 自动续跑 */
    TEST_ASSERT_EQUAL_UINT32(1u, s.completed_work);
}

static void test_break_finishes_back_to_work(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f);
    /* 跑完 WORK -> BREAK */
    f.remaining_ms = 100u;
    pomo_tick(&f, 100u);
    /* 跑完 BREAK -> WORK */
    f.remaining_ms = 100u;
    pomo_tick(&f, 100u);
    pomo_snapshot_t s = pomo_snapshot(&f);
    TEST_ASSERT_EQUAL(POMO_PHASE_WORK, s.phase);
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, s.total_ms);
    TEST_ASSERT_EQUAL_UINT32(1u, s.completed_work); /* BREAK 结束不加计数 */
}
```

`app_main` 追加两行 `RUN_TEST(...)`。

- [ ] **步骤 2：运行验证通过**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：全部通过（实现已就绪）。

- [ ] **步骤 3：Commit**

```bash
git add test/main/test_pomodoro_fsm.c
git commit -m "test: 阶段切换与 completed_work 用例"
```

---

## 任务 7：TDD — pomo_reset

**文件：**
- 修改：`test/main/test_pomodoro_fsm.c`
- 修改：`components/pomodoro_fsm/pomodoro_fsm.c`

- [ ] **步骤 1：写失败的测试**

追加用例并注册：

```c
static void test_reset_restores_phase_total_and_pauses(void)
{
    pomo_fsm_t f;
    pomo_init(&f);
    pomo_toggle(&f);                       /* RUNNING */
    f.remaining_ms = 1234u;               /* 模拟已走过一段 */
    pomo_reset(&f);
    pomo_snapshot_t s = pomo_snapshot(&f);
    TEST_ASSERT_EQUAL_UINT32(s.total_ms, s.remaining_ms);  /* 回满 */
    TEST_ASSERT_EQUAL_UINT32(POMO_WORK_MS, s.remaining_ms);
    TEST_ASSERT_EQUAL(POMO_PAUSED, s.status);              /* 停住 */
}
```

`app_main` 追加 `RUN_TEST(test_reset_restores_phase_total_and_pauses);`。

- [ ] **步骤 2：运行验证失败**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：FAIL（空桩 `pomo_reset` 不改字段）。

- [ ] **步骤 3：实现 pomo_reset**

替换 `pomodoro_fsm.c` 中的 `pomo_reset`：

```c
void pomo_reset(pomo_fsm_t *f)
{
    f->remaining_ms = f->total_ms;
    f->status = POMO_PAUSED;
}
```

- [ ] **步骤 4：运行验证通过**

```bash
cd test && idf.py build && ./build/pomodoro_fsm_test.elf; cd ..
```

预期：全部用例通过。fsm 模块到此完成。

- [ ] **步骤 5：Commit**

```bash
git add components/pomodoro_fsm/pomodoro_fsm.c test/main/test_pomodoro_fsm.c
git commit -m "feat: pomo_reset 重置并暂停"
```

---

## 任务 8：固件工程骨架（BSP 初始化 + 空屏点亮）

搭建可烧录的最小固件：BSP 起显示、设亮度、黑屏。此任务验证 BSP 依赖能拉取、esp32s3 能编译。

**文件：**
- 创建：`CMakeLists.txt`（顶层）
- 创建：`sdkconfig.defaults`
- 创建：`partitions.csv`
- 创建：`main/CMakeLists.txt`
- 创建：`main/idf_component.yml`
- 创建：`main/main.c`（临时最小版，任务 11 会扩展）

- [ ] **步骤 1：顶层 CMakeLists**

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pomodoro_watch)
```

- [ ] **步骤 2：分区表（16MB flash）**

`partitions.csv`：

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     ,        24K
phy_init, data, phy,     ,        4K
factory,  app,  factory, ,        4M
```

- [ ] **步骤 3：sdkconfig.defaults（对齐官方 02_lvgl_demo_v9：QIO 16MB + 八线 PSRAM 80M + 240MHz + 字体）**

`sdkconfig.defaults`：

```ini
CONFIG_IDF_TARGET="esp32s3"

# Flash
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y

# PSRAM（板载八线 PSRAM）
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y

# 性能
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_FREERTOS_HZ=1000

# 分区
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# 实验特性（官方例程开启）
CONFIG_IDF_EXPERIMENTAL_FEATURES=y

# LVGL 字体（时间大字用 48，标签/按钮用 28）
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_48=y
```

- [ ] **步骤 4：main 组件清单（依赖 BSP + LVGL）**

`main/idf_component.yml`：

```yaml
dependencies:
  idf:
    version: ">=5.4"
  waveshare/esp32_s3_touch_amoled_2_06: "^1.0.7"
  lvgl/lvgl: "~9.2.0"
```

- [ ] **步骤 5：main 的 CMakeLists**

`main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")
```

- [ ] **步骤 6：最小 main.c（起 BSP + 亮度，黑屏）**

`main/main.c`：

```c
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
```

- [ ] **步骤 7：构建（验证 BSP 拉取 + esp32s3 编译）**

```bash
source ~/esp/esp-idf/export.sh
cd /data/projects/pomodoro-watch
idf.py set-target esp32s3
idf.py build
```

预期：依赖下载到 `managed_components/`，编译成功生成 `build/pomodoro_watch.bin`。

> 注：本会话无法烧录硬件；编译成功即视为本任务通过。烧录由用户执行 `idf.py flash monitor`。

- [ ] **步骤 8：Commit**

```bash
git add CMakeLists.txt sdkconfig.defaults partitions.csv main
git commit -m "feat: 固件工程骨架与 BSP 初始化"
```

---

## 任务 9：UI 层——控件构建与刷新（ui.h / ui.c）

**文件：**
- 创建：`main/ui.h`
- 创建：`main/ui.c`
- 修改：`main/CMakeLists.txt`（加入 ui.c）

- [ ] **步骤 1：UI 接口头**

`main/ui.h`：

```c
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

/* 阶段结束时的全屏闪烁（在顶层图层做 3 次明暗）。color 为新阶段色。 */
void ui_flash(lv_color_t color);

#ifdef __cplusplus
}
#endif
```

- [ ] **步骤 2：UI 实现**

`main/ui.c`：

```c
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

    /* 阶段标签（顶部） */
    s_phase_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_phase_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_phase_label, COLOR_WORK, 0);
    lv_label_set_text(s_phase_label, "Focus #1");
    lv_obj_align(s_phase_label, LV_ALIGN_TOP_MID, 0, 30);

    /* 圆环（中部） */
    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, 320, 320);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -10);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_value(s_arc, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);            /* 去掉拖动旋钮 */
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);          /* 圆环不可交互 */
    lv_obj_set_style_arc_width(s_arc, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 22, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, COLOR_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, COLOR_WORK, LV_PART_INDICATOR);

    /* 圆心时间大字 */
    s_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_label_set_text(s_time_label, "25:00");
    lv_obj_align_to(s_time_label, s_arc, LV_ALIGN_CENTER, 0, 0);

    /* 开始/暂停 按钮（底部左） */
    s_btn_toggle = lv_button_create(scr);
    lv_obj_set_size(s_btn_toggle, 150, 70);
    lv_obj_align(s_btn_toggle, LV_ALIGN_BOTTOM_MID, -85, -40);
    lv_obj_set_style_bg_color(s_btn_toggle, COLOR_WORK, 0);
    lv_obj_add_event_cb(s_btn_toggle, on_toggle, LV_EVENT_CLICKED, NULL);
    s_btn_toggle_label = lv_label_create(s_btn_toggle);
    lv_obj_set_style_text_font(s_btn_toggle_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_btn_toggle_label, "Start");
    lv_obj_center(s_btn_toggle_label);

    /* 重置 按钮（底部右） */
    lv_obj_t *btn_reset = lv_button_create(scr);
    lv_obj_set_size(btn_reset, 150, 70);
    lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_MID, 85, -40);
    lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0x444450), 0);
    lv_obj_add_event_cb(btn_reset, on_reset, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset_label = lv_label_create(btn_reset);
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_center(reset_label);

    /* 闪烁覆盖层（顶层图层，默认隐藏、不透明度 0、不接管点击） */
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

    /* 圆环值（remaining/total，防 0 除） */
    uint32_t v = 0;
    if (s->total_ms > 0) {
        v = (uint32_t)(((uint64_t)s->remaining_ms * 1000u) / s->total_ms);
    }
    lv_arc_set_value(s_arc, (int32_t)v);
    lv_obj_set_style_arc_color(s_arc, col, LV_PART_INDICATOR);

    /* MM:SS（向上取整，使收尾恰好显示 00:00） */
    uint32_t total_s = (s->remaining_ms + 999u) / 1000u;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u",
             (unsigned)(total_s / 60u), (unsigned)(total_s % 60u));
    lv_label_set_text(s_time_label, buf);

    /* 阶段标签 */
    char phase_buf[16];
    if (s->phase == POMO_PHASE_WORK) {
        snprintf(phase_buf, sizeof(phase_buf), "Focus #%u",
                 (unsigned)(s->completed_work + 1u));
    } else {
        snprintf(phase_buf, sizeof(phase_buf), "Break");
    }
    lv_label_set_text(s_phase_label, phase_buf);
    lv_obj_set_style_text_color(s_phase_label, col, 0);

    /* 开始/暂停 按钮文字与主色 */
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

void ui_flash(lv_color_t color)
{
    lv_obj_set_style_bg_color(s_overlay, color, 0);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, flash_opa_cb);
    lv_anim_set_values(&a, 0, 180);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_playback_duration(&a, 150);
    lv_anim_set_repeat_count(&a, 3);
    lv_anim_set_ready_cb(&a, flash_ready_cb);
    lv_anim_start(&a);
}
```

- [ ] **步骤 3：把 ui.c 加入构建**

修改 `main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "main.c" "ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES pomodoro_fsm)
```

- [ ] **步骤 4：构建验证（UI 代码编译通过）**

此步 main.c 还没调用 ui，需临时验证编译。运行：

```bash
idf.py build
```

预期：`ui.c` 编译通过（可能有 “s_arc 等已定义但 ui_create 未被调用” 之类无警告；未调用函数不报错）。若报 `lv_font_montserrat_48` 未定义，确认任务 8 的 sdkconfig.defaults 已生效（必要时 `idf.py reconfigure`）。

- [ ] **步骤 5：Commit**

```bash
git add main/ui.h main/ui.c main/CMakeLists.txt
git commit -m "feat: LVGL UI 层（圆环/时间/按钮/闪烁）"
```

---

## 任务 10：胶水层——把 fsm 与 ui 接起来（main.c）

**文件：**
- 修改：`main/main.c`

- [ ] **步骤 1：完整实现 main.c**

替换 `main/main.c` 全文：

```c
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
    lv_timer_create(tick_timer_cb, 100, NULL);   /* 每 100ms 驱动一次 */
    bsp_display_unlock();

    ESP_LOGI(TAG, "ui ready");
}
```

- [ ] **步骤 2：构建**

```bash
idf.py build
```

预期：整体编译链接成功，生成 `build/pomodoro_watch.bin`。

- [ ] **步骤 3：Commit**

```bash
git add main/main.c
git commit -m "feat: main 胶水层（lv_timer 驱动 fsm + ui）"
```

---

## 任务 11：README 与设备端验收

**文件：**
- 创建：`README.md`

- [ ] **步骤 1：写 README**

`README.md`：

````markdown
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
````

- [ ] **步骤 2：Commit**

```bash
git add README.md
git commit -m "docs: README 编译/烧录/测试说明"
```

- [ ] **步骤 3：设备端手动验收（用户在真机执行）**

烧录后逐项确认：

1. 上电显示 `Focus #1`、`25:00`、圆环满、按钮 `Start`，不走动。
2. 点 `Start`：时间每秒递减、圆环随之回缩，按钮变 `Pause`。
3. 点 `Pause`：暂停，按钮变 `Resume`；再点继续。
4. 点 `Reset`：回到当前阶段满时长并停住（按钮 `Resume`/`Start`）。
5. 计时到 0：屏幕闪烁 3 下，自动切 `Break`（绿色）并继续；Break 到 0 切回 `Focus #2`（红色）。
6. 触摸按钮命中准确、无明显延迟。

如某项不符，进入 systematic-debugging 流程。

---

## 自检结果

**规格覆盖度：**
- 倒计时圆环 → 任务 9（lv_arc）✅
- 剩余时间 MM:SS → 任务 9 `ui_render` ✅
- 触摸 开始/暂停/重置（屏上按钮）→ 任务 9 按钮 + 任务 10 回调 ✅
- 经典 25/5 自动循环 → 任务 5/6 `pomo_tick` ✅
- 结束闪烁+变色 → 任务 9 `ui_flash` + 任务 10 触发 ✅
- 官方 BSP + LVGL v9 → 任务 8 依赖 + 初始化 ✅
- 英文/符号文字（无中文字库）→ 任务 9 全用 Montserrat ✅
- 主机端 Linux 目标 + Unity 测试 → 任务 2–7 ✅
- 错误处理（dt 钳制/0 除/饱和）→ 任务 5 + 任务 9 ✅

**占位符扫描：** 无 TODO/待定；每个代码步骤均有完整代码。

**类型一致性：** `pomo_fsm_t`/`pomo_snapshot_t`/`pomo_event_t` 全程一致；函数名 `pomo_init/toggle/reset/tick/snapshot` 在 fsm、test、main 三处签名一致；UI `ui_create/ui_render/ui_flash` 与 main 调用一致；LVGL v9 API（`lv_button_create`/`lv_screen_active`/`lv_obj_remove_flag`/`lv_anim_set_duration`）统一用 v9 命名。
