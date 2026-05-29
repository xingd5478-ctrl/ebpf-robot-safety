#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bsp_mpu6050.h"
#include "system_config.h"
#include "board_config.h"
#include "control_task.h"

/* ========== 优先级 ========== */
#define PRIO_ACQUIRE     4    /* highest: sensor I2C read + queue push */
#define PRIO_CONTROL     4    /* same as Acquire: runs fusion + PID, won't starve Comm */
#define PRIO_COMM        3    /* bidirectional telemetry — must not be blocked */
#define PRIO_CLI         1

/* ========== 栈大小 (words) ========== */
#define STACK_ACQUIRE    256
#define STACK_CONTROL    384
#define STACK_COMM       384
#define STACK_CLI        256

/* ========== 队列消息结构 ========== */

/* 原始传感器数据 (从采集→通信) */
typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
    uint32_t timestamp_ms;
} RawSensorData_t;

/* ========== 队列句柄 (全局, 所有任务可访问) ========== */
extern QueueHandle_t g_rawDataQ;      /* Acquire → Comm: sensor data for telemetry TX */
extern QueueHandle_t g_sensorDataQ;   /* Acquire → Control: sensor data for fusion+PID */

/* ========== 外设句柄 (全局, 各任务共享) ========== */
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;
extern MPU6050_Handle_t g_mpu_dev;

/* ========== 任务句柄 (用于监控) ========== */
extern TaskHandle_t g_h_acquire;
extern TaskHandle_t g_h_comm;
extern TaskHandle_t g_h_control;
extern TaskHandle_t g_h_monitor;

/* ========== 任务函数 ========== */
void Task_Acquire(void *param);
void Task_Comm(void *param);
void Task_Monitor(void *param);
void Task_CLI(void *param);

/* ========== 运行时统计定时器 ========== */
void vConfigureTimerForRunTimeStats(void);
unsigned long vGetRunTimeCounterValue(void);

/* ========== 系统启动 ========== */
void AppTasks_Init(void);

/* ========== UART TX 互斥锁 ========== */
extern SemaphoreHandle_t g_uart_tx_mutex;

/* ========== 运行时可调参数 ========== */
typedef struct {
    uint8_t  dlpf;              /* DLPF 带宽索引 (0-6) */
    uint8_t  accel_fs;          /* 加速度量程 (0-3) */
    uint8_t  gyro_fs;           /* 陀螺仪量程 (0-3) */
    uint16_t rate_hz;           /* 目标采集频率 (Hz) */
} ConfigParam_t;

extern ConfigParam_t g_config;

/* ========== 应用级看门狗 ID ========== */
extern int g_wdt_acq;
extern int g_wdt_comm;
extern int g_wdt_cli;
extern int g_wdt_ctrl;

/* ========== 系统配置 (持久化) ========== */
extern SystemConfig_t g_sys_cfg;

/* ========== 系统状态 ========== */
typedef struct {
    uint32_t total_frames;
    uint32_t dropped_frames;
    uint32_t comm_errors;
    uint32_t uptime_ms;
    uint8_t  sensor_ok;
    uint8_t  heap_usage_pct;
} SystemStatus_t;

extern SystemStatus_t g_sys_status;

/* ========== 最后收到的命令 ID (用于遥测回显) ========== */
extern uint8_t g_last_cmd_id;

#endif /* APP_TASKS_H */
