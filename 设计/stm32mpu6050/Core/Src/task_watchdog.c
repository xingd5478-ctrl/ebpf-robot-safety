#include "task_watchdog.h"
#include "board_config.h"
#include <string.h>
#include <stdio.h>

/* ========== 内部数据结构 ========== */
typedef struct {
    const char  *name;
    TaskHandle_t handle;
    uint32_t     deadline_ms;
    uint32_t     last_checkin_tick;
    uint8_t      fault_count;
    uint8_t      enabled;
} WDTEntry_t;

static struct {
    WDTEntry_t entries[TASKWDT_MAX_TASKS];
    int        count;
} s_wdt = {0};

/* ========== 初始化 ========== */
void TaskWDT_Init(void)
{
    memset(&s_wdt, 0, sizeof(s_wdt));
}

/* ========== 注册 ========== */
int TaskWDT_Register(const char *name, TaskHandle_t handle, uint32_t deadline_ms)
{
    if (s_wdt.count >= TASKWDT_MAX_TASKS)
        return -1;

    int id = s_wdt.count++;
    s_wdt.entries[id].name              = name;
    s_wdt.entries[id].handle            = handle;
    s_wdt.entries[id].deadline_ms       = deadline_ms;
    s_wdt.entries[id].last_checkin_tick = xTaskGetTickCount();
    s_wdt.entries[id].fault_count       = 0;
    s_wdt.entries[id].enabled           = 1;
    return id;
}

/* ========== 签到 ========== */
void TaskWDT_CheckIn(int id)
{
    if (id < 0 || id >= s_wdt.count)
        return;
    s_wdt.entries[id].last_checkin_tick = xTaskGetTickCount();
}

/* ========== 检查所有任务 ========== */
void TaskWDT_Check(void)
{
    uint32_t now = xTaskGetTickCount();
    uint32_t report_mask = 1;  /* 只报错不反复刷屏 */

    for (int i = 0; i < s_wdt.count; i++) {
        if (!s_wdt.entries[i].enabled)
            continue;

        uint32_t elapsed = now - s_wdt.entries[i].last_checkin_tick;
        if (elapsed < s_wdt.entries[i].deadline_ms)
            continue;

        /* 超时! */
        s_wdt.entries[i].fault_count++;

        if (s_wdt.entries[i].fault_count >= TASKWDT_MAX_FAULTS) {
            /* 严重故障: 串口报警 */
            static uint32_t last_report = 0;
            if (now - last_report > pdMS_TO_TICKS(5000)) {
                last_report = now;
                uint8_t buf[64];
                int n = snprintf((char*)buf, sizeof(buf),
                    "\r\n[WDT] TASK '%s' FAULT (missed %u/%u)\r\n",
                    s_wdt.entries[i].name,
                    (unsigned)s_wdt.entries[i].fault_count,
                    (unsigned)TASKWDT_MAX_FAULTS);
                HAL_UART_Transmit(&CONSOLE_UART, buf, (uint16_t)n, 100);
            }
        }
    }
}

/* ========== 查询最差情况 ========== */
int TaskWDT_GetWorstCase(int *fault_count)
{
    int worst_id = -1;
    int worst_fc = -1;
    for (int i = 0; i < s_wdt.count; i++) {
        if (s_wdt.entries[i].enabled &&
            (int)s_wdt.entries[i].fault_count > worst_fc) {
            worst_fc = (int)s_wdt.entries[i].fault_count;
            worst_id = i;
        }
    }
    if (fault_count) *fault_count = worst_fc;
    return worst_id;
}
