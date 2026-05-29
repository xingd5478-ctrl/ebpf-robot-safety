#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f1xx_hal.h"

/* ========== 处理器与时钟 ========== */
#define configCPU_CLOCK_HZ               ((unsigned long)72000000)
#define configSYSTICK_CLOCK_HZ           ((unsigned long)72000000)
#define configTICK_RATE_HZ               ((TickType_t)1000)

/* ========== 内核参数 ========== */
#define configMAX_PRIORITIES             (5)
#define configMINIMAL_STACK_SIZE         ((unsigned short)128)
#define configTOTAL_HEAP_SIZE            ((size_t)(12 * 1024))   /* 12KB heap (USB CDC adds ~3KB extra) */
#define configMAX_TASK_NAME_LEN          (16)
#define configUSE_16_BIT_TICKS           0
#define configIDLE_SHOULD_YIELD          1
#define configUSE_TICKLESS_IDLE          0

/* ========== 任务通知替代二值信号量 ========== */
#define configUSE_TASK_NOTIFICATIONS      1
#define configUSE_PREEMPTION              1
#define configUSE_TIME_SLICING            1

/* ========== 队列/信号量 ========== */
#define configUSE_COUNTING_SEMAPHORES     1
#define configUSE_MUTEXES                 1
#define configUSE_RECURSIVE_MUTEXES       0
#define configQUEUE_REGISTRY_SIZE         8

/* ========== 软件定时器 ========== */
#define configUSE_TIMERS                  1
#define configTIMER_TASK_PRIORITY         (2)
#define configTIMER_QUEUE_LENGTH          5
#define configTIMER_TASK_STACK_DEPTH      (configMINIMAL_STACK_SIZE)

/* ========== Cortex-M3 NVIC 配置 ========== */
#define configPRIO_BITS                   4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY    15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY   (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ========== 可选功能 ========== */
#define INCLUDE_vTaskPrioritySet          1
#define INCLUDE_uxTaskPriorityGet         1
#define INCLUDE_vTaskDelete               1
#define INCLUDE_vTaskDelayUntil           1
#define INCLUDE_xTaskGetSchedulerState    1
#define INCLUDE_vTaskDelay                1
#define INCLUDE_xTaskGetCurrentTaskHandle  1
#define INCLUDE_xTaskGetIdleTaskHandle     0
#define INCLUDE_xTimerGetTimerDaemonTaskHandle 0
#define INCLUDE_pcTaskGetTaskName          0
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_eTaskGetState               1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xEventGroupSetBitFromISR   1

/* ========== 处理器安装检查 ==========
 * 本项目使用 freertos_vector.c 中的 SVC_Handler/PendSV_Handler
 * 分支跳转到 vPortSVCHandler/xPortPendSVHandler, 因此向量表地址不直接匹配,
 * 必须禁用 configCHECK_HANDLER_INSTALLATION 以避免 configASSERT 失败. */
#define configCHECK_HANDLER_INSTALLATION 0

/* ========== 断言 ========== */
#define configASSERT(x) if((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }

/* ========== 钩子函数 ========== */
#define configUSE_IDLE_HOOK               1
#define configUSE_TICK_HOOK               0
#define configUSE_MALLOC_FAILED_HOOK      1
#define configCHECK_FOR_STACK_OVERFLOW    2

/* ========== 运行时统计 ========== */
#define configGENERATE_RUN_TIME_STATS     1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
extern void vConfigureTimerForRunTimeStats(void);
extern unsigned long vGetRunTimeCounterValue(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() vConfigureTimerForRunTimeStats()
#define portGET_RUN_TIME_COUNTER_VALUE()     vGetRunTimeCounterValue()

/* ========== Trace ========== */
#define configUSE_TRACE_FACILITY          1

#endif /* FREERTOS_CONFIG_H */
