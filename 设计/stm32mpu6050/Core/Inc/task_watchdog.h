#ifndef TASK_WATCHDOG_H
#define TASK_WATCHDOG_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* ========== 配置 ========== */
#define TASKWDT_MAX_TASKS     8
#define TASKWDT_MAX_FAULTS    3       /* 连续超时 N 次视为故障 */
#define TASKWDT_DEFAULT_TICK  portMAX_DELAY  /* 未注册任务的默认值 */

/* ========== API ========== */

/* 初始化 (可注册一个全局故障回调, 传入 NULL 则使用默认: 串口报警) */
void TaskWDT_Init(void);

/* 注册任务: 返回 wdt_id (后续 CheckIn 使用) */
int  TaskWDT_Register(const char *name, TaskHandle_t handle, uint32_t deadline_ms);

/* 任务周期性调用: 携带注册时得到的 id */
void TaskWDT_CheckIn(int id);

/* 由 Monitor 任务调用 (通常 1Hz), 遍历检查所有已注册任务 */
void TaskWDT_Check(void);

/* 查询: 返回连续超时计数最大的任务的 id, -1 表示无故障 */
int  TaskWDT_GetWorstCase(int *fault_count);

#endif /* TASK_WATCHDOG_H */
