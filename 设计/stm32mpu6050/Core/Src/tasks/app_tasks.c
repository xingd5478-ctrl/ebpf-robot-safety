#include "tasks/app_tasks.h"
#include "cli_shell.h"
#include "data_protocol.h"
#include "task_watchdog.h"
#include "motor_control.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ========== FreeRTOS 钩子函数 ========== */

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    g_sys_status.heap_usage_pct = 100;
    const char *msg = "\r\n[FATAL] Malloc failed! Out of heap memory.\r\n";
    HAL_UART_Transmit(&CONSOLE_UART, (uint8_t*)msg, strlen(msg), 100);
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    taskDISABLE_INTERRUPTS();
    uint8_t buf[64];
    int len = snprintf((char*)buf, sizeof(buf),
        "\r\n[FATAL] Stack overflow in task: %s\r\n", pcTaskName);
    HAL_UART_Transmit(&CONSOLE_UART, buf, len, 100);
    for (;;) {}
}

/* ========== 独立看门狗刷新 ========== */
static void iwdg_refresh(void) { IWDG->KR = 0xAAAA; }

void vApplicationIdleHook(void) { iwdg_refresh(); }

/* ========== 队列句柄定义 ========== */
QueueHandle_t g_rawDataQ    = NULL;   /* Acquire → Comm: sensor data for telemetry */
QueueHandle_t g_sensorDataQ = NULL;   /* Acquire → Control: sensor data for fusion */

/* ========== 最后收到的命令 ID (用于遥测回显) ========== */
uint8_t g_last_cmd_id = 0;

/* ========== UART TX 互斥锁 ========== */
SemaphoreHandle_t g_uart_tx_mutex = NULL;

/* ========== 任务句柄定义 ========== */
TaskHandle_t g_h_acquire = NULL;
TaskHandle_t g_h_comm    = NULL;
TaskHandle_t g_h_control = NULL;
TaskHandle_t g_h_monitor = NULL;

/* ========== MPU6050 设备句柄 ========== */
MPU6050_Handle_t g_mpu_dev = {0};

/* ========== 系统状态 (Monitor任务填充) ========== */
SystemStatus_t g_sys_status = {0};

/* ========== 运行时可调参数 (默认值) ========== */
ConfigParam_t g_config = {
    .dlpf     = SYSCFG_DEFAULT_DLPF,
    .accel_fs = SYSCFG_DEFAULT_ACCEL_FS,
    .gyro_fs  = SYSCFG_DEFAULT_GYRO_FS,
    .rate_hz  = SYSCFG_DEFAULT_RATE_HZ,
};

/* ========== 系统配置 (持久化) ========== */
SystemConfig_t g_sys_cfg = {0};

/* ========== DWT 周期计数器 ========== */
static void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static inline uint32_t DWT_Get(void) { return DWT->CYCCNT; }
static inline float DWT_ToUS(uint32_t ticks) {
    return (float)ticks / 72.0f;
}
static uint32_t t_acq_meas = 0;
static uint32_t t_com_meas = 0;

/* ========== 应用级看门狗 ID ========== */
int g_wdt_acq  = -1;
int g_wdt_comm = -1;
int g_wdt_cli  = -1;
int g_wdt_ctrl = -1;

/* ========== 运行时统计 ========== */
static volatile unsigned long g_run_time_counter = 0;

void vConfigureTimerForRunTimeStats(void)
{
    __HAL_RCC_TIM4_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM4_IRQn, 15, 0);
    TIM4->PSC = 72000 - 1;
    TIM4->ARR = 0xFFFF;
    TIM4->CR1 = TIM_CR1_CEN;
}

unsigned long vGetRunTimeCounterValue(void) { return TIM4->CNT; }

// ============================================================
//  Command Parser: ASCII text commands from Linux → ControlCmd_t
//  Protocol: "CMD [arg1] [arg2]\r\n"
//
//  Supported commands:
//    STOP        — stop all motors
//    ESTOP       — emergency stop (latch, requires HW reset)
//    FWD  <pwm>  — forward at PWM (0-999)
//    BACK <pwm>  — backward at PWM
//    LEFT <pwm>  — turn left
//    RIGHT<pwm>  — turn right
//    VEL  <lin> <ang>  — set linear + angular velocity
//    HEAD <deg>  — heading hold at target yaw (degrees)
//    KP   <val>  — set YAW_KP (float x1000)
//    KD   <val>  — set YAW_KD (float x1000)
//    KI   <val>  — set YAW_KI (float x1000)
// ============================================================

#define CMD_BUF_SIZE    64
static char  s_cmd_buf[CMD_BUF_SIZE];
static int   s_cmd_idx = 0;
static char  s_cmd_line[CMD_BUF_SIZE];

static int parse_command(const char *line, ControlCmd_t *cmd)
{
    if (!line || !cmd) return 0;

    char token[16];
    int v1 = 0, v2 = 0;
    int n = 0;

    // Extract first token
    n = 0;
    while (*line == ' ' || *line == '\t') line++;
    while (*line && *line != ' ' && *line != '\t' && *line != '\r' && *line != '\n'
           && n < (int)sizeof(token)-1)
        token[n++] = *(line++);
    token[n] = '\0';

    if (n == 0) return 0;

    // Parse optional args: arg1 [arg2]
    while (*line == ' ' || *line == '\t') line++;
    if (*line && *line != '\r' && *line != '\n') {
        v1 = atoi(line);
        while (*line && *line != ' ' && *line != '\t' && *line != '\r' && *line != '\n')
            line++;
        while (*line == ' ' || *line == '\t') line++;
        if (*line && *line != '\r' && *line != '\n')
            v2 = atoi(line);
    }

    // Match command token
    cmd->value1 = (int16_t)v1;
    cmd->value2 = (int16_t)v2;

    if (strcmp(token, "STOP") == 0) {
        cmd->type = CMD_STOP;
        g_last_cmd_id = 1;
    } else if (strcmp(token, "ESTOP") == 0) {
        cmd->type = CMD_EMERGENCY_STOP;
        g_last_cmd_id = 7;
    } else if (strcmp(token, "FWD") == 0) {
        cmd->type = CMD_MOVE_FWD;
        g_last_cmd_id = 2;
        if (v1 == 0) cmd->value1 = 400;
    } else if (strcmp(token, "BACK") == 0) {
        cmd->type = CMD_MOVE_BACK;
        g_last_cmd_id = 3;
        if (v1 == 0) cmd->value1 = 400;
    } else if (strcmp(token, "LEFT") == 0) {
        cmd->type = CMD_TURN_LEFT;
        g_last_cmd_id = 4;
        if (v1 == 0) cmd->value1 = 300;
    } else if (strcmp(token, "RIGHT") == 0) {
        cmd->type = CMD_TURN_RIGHT;
        g_last_cmd_id = 5;
        if (v1 == 0) cmd->value1 = 300;
    } else if (strcmp(token, "VEL") == 0) {
        cmd->type = CMD_SET_VEL;
        g_last_cmd_id = 6;
    } else if (strcmp(token, "HEAD") == 0) {
        cmd->type = CMD_HEADING;
        g_last_cmd_id = 8;
    } else {
        return 0;
    }

    return 1;
}

// Read one byte from UART RX (non-blocking), return 1 if byte read
static int uart_rx_poll(uint8_t *ch)
{
    if (__HAL_UART_GET_FLAG(&CONSOLE_UART, UART_FLAG_RXNE)) {
        *ch = (uint8_t)(CONSOLE_UART.Instance->DR & 0xFF);
        return 1;
    }
    return 0;
}

// Consume UART RX bytes and try to parse a complete command line.
// Returns 1 if a complete command was parsed into cmd.
static int uart_rx_consume(ControlCmd_t *cmd)
{
    uint8_t ch;
    int got_line = 0;

    while (uart_rx_poll(&ch)) {
        if (ch == '\r' || ch == '\n') {
            if (s_cmd_idx > 0) {
                s_cmd_buf[s_cmd_idx] = '\0';
                memcpy(s_cmd_line, s_cmd_buf, s_cmd_idx + 1);
                s_cmd_idx = 0;
                got_line = 1;
            }
        } else if (s_cmd_idx < CMD_BUF_SIZE - 1) {
            s_cmd_buf[s_cmd_idx++] = (char)ch;
        }
        if (got_line) break;
    }

    if (got_line) {
        return parse_command(s_cmd_line, cmd);
    }
    return 0;
}

// ============================================================
//  Task: 数据采集 (unchanged from original)
// ============================================================
void Task_Acquire(void *param)
{
    (void)param;
    TickType_t last_wake = xTaskGetTickCount();
    uint16_t last_rate = 0;
    uint8_t last_dlpf = 0xFF;

    vTaskDelay(pdMS_TO_TICKS(50));

    for (;;) {
        TickType_t period = pdMS_TO_TICKS(1000 / g_config.rate_hz);
        if (period == 0) period = 1;
        vTaskDelayUntil(&last_wake, period);

        if (g_config.rate_hz != last_rate) {
            MPU6050_SetSampleRate(&g_mpu_dev, g_config.rate_hz);
            last_rate = g_config.rate_hz;
        }
        if (g_config.dlpf != last_dlpf) {
            MPU6050_SetDLPF(&g_mpu_dev, (MPU6050_DLPF_Bandwidth_t)g_config.dlpf);
            last_dlpf = g_config.dlpf;
        }

        TaskWDT_CheckIn(g_wdt_acq);
        iwdg_refresh();

        RawSensorData_t raw;
        raw.timestamp_ms = xTaskGetTickCount();

        uint32_t t_start = DWT_Get();
    #if TEST_DUMMY_DATA
        {
            static uint32_t dummy_cnt = 0;
            raw.ax = (int16_t)(dummy_cnt * 100);
            raw.ay = 0;
            raw.az = 16384;
            raw.gx = 0;
            raw.gy = (int16_t)(dummy_cnt * 10);
            raw.gz = 0;
            dummy_cnt++;
        }
    #else
        if (MPU6050_ReadAll(&g_mpu_dev,
                            &raw.ax, &raw.ay, &raw.az,
                            &raw.gx, &raw.gy, &raw.gz,
                            NULL) != HAL_OK) {
            static uint8_t i2c_err = 0;
            if (++i2c_err >= 3) {
                i2c_err = 0;
                MPU6050_Init(&g_mpu_dev);
            }
            g_sys_status.dropped_frames++;
            vTaskDelay(period);
            continue;
        }
    #endif

        if (xQueueSend(g_rawDataQ, &raw, 0) != pdPASS) {
            g_sys_status.dropped_frames++;
        } else {
            g_sys_status.total_frames++;
            t_acq_meas = DWT_ToUS(DWT_Get() - t_start);
        }
        // Also push to Control task for sensor fusion (non-blocking, drop if full)
        xQueueSend(g_sensorDataQ, &raw, 0);
    }
}

// ============================================================
//  Task: 串口通信 (bidirectional — TX sensor data + RX commands)
// ============================================================
void Task_Comm(void *param)
{
    (void)param;
    RawSensorData_t raw;
    uint8_t tx_buf[64];
    Protocol_Init();
    s_cmd_idx = 0;

    for (;;) {
        // Wait for sensor data with timeout to allow RX polling
        if (xQueueReceive(g_rawDataQ, &raw, pdMS_TO_TICKS(5)) != pdPASS) {
            // No data — still check for incoming commands
            ControlCmd_t cmd;
            if (uart_rx_consume(&cmd)) {
                xQueueSend(g_cmdQ, &cmd, 0);
            }
            continue;
        }

        TaskWDT_CheckIn(g_wdt_comm);

        // --- Check incoming commands (non-blocking) ---
        ControlCmd_t cmd;
        while (uart_rx_consume(&cmd)) {
            xQueueSend(g_cmdQ, &cmd, 0);
        }

        // --- Build and send telemetry frame ---
        // Frame format (32 bytes, CRC over bytes 0-29):
        //   [0-1]:   0xBADD (telemetry frame ID)
        //   [2]:     seq number
        //   [3-4]:   current_yaw (int16, deg*10)
        //   [5-6]:   target_yaw  (int16, deg*10)
        //   [7-8]:   motor_left_pwm  (int16)
        //   [9-10]:  motor_right_pwm (int16)
        //   [11-12]: ax_raw (int16)
        //   [13-14]: ay_raw (int16)
        //   [15-16]: az_raw (int16)
        //   [17-18]: gx_raw (int16)
        //   [19-20]: gy_raw (int16)
        //   [21-22]: gz_raw (int16)
        //   [23]:    emergency_stop flag
        //   [24]:    heading_mode flag
        //   [25]:    last_cmd_id (command echo for confirmation)
        //   [26-27]: jitter_us*10 (uint16)
        //   [28]:    missed_cycles (uint8, saturates at 255)
        //   [29]:    reserved
        //   [30-31]: CRC16

        uint32_t t_start_c = DWT_Get();

        memset(tx_buf, 0, 32);
        tx_buf[0]  = 0xBA;
        tx_buf[1]  = 0xDD;
        static uint8_t tlm_seq = 0;
        tx_buf[2]  = tlm_seq++;

        int16_t yaw_d10    = (int16_t)(g_ctrl_state.current_yaw * 10.0f);
        int16_t tyaw_d10   = (int16_t)(g_ctrl_state.target_yaw * 10.0f);
        int16_t motor_l    = g_ctrl_state.motor_left_pwm;
        int16_t motor_r    = g_ctrl_state.motor_right_pwm;

        memcpy(&tx_buf[3],  &yaw_d10,  2);
        memcpy(&tx_buf[5],  &tyaw_d10, 2);
        memcpy(&tx_buf[7],  &motor_l,  2);
        memcpy(&tx_buf[9],  &motor_r,  2);
        memcpy(&tx_buf[11], &raw.ax,   2);
        memcpy(&tx_buf[13], &raw.ay,   2);
        memcpy(&tx_buf[15], &raw.az,   2);
        memcpy(&tx_buf[17], &raw.gx,   2);
        memcpy(&tx_buf[19], &raw.gy,   2);
        memcpy(&tx_buf[21], &raw.gz,   2);
        tx_buf[23] = g_ctrl_state.emergency_stop;
        tx_buf[24] = g_ctrl_state.heading_mode;

        tx_buf[25] = g_last_cmd_id;  // command echo for confirmation

        uint16_t jitter_proxy = (uint16_t)(t_acq_meas * 10.0f);
        memcpy(&tx_buf[26], &jitter_proxy, 2);

        tx_buf[28] = (uint8_t)(g_ctrl_state.missed_cycles > 255 ? 255 : g_ctrl_state.missed_cycles);

        uint16_t crc = crc16(tx_buf, 30);
        memcpy(&tx_buf[30], &crc, 2);

        if (xSemaphoreTake(g_uart_tx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            HAL_UART_Transmit(&CONSOLE_UART, tx_buf, 32, 100);
            xSemaphoreGive(g_uart_tx_mutex);
            g_link_stats.frames_sent++;
            g_link_stats.link_status = 1;
        } else {
            g_sys_status.comm_errors++;
        }
        t_com_meas = DWT_ToUS(DWT_Get() - t_start_c);
    }
}

// ============================================================
//  Task: 系统监控 (1Hz)
// ============================================================
void Task_Monitor(void *param)
{
    (void)param;
    uint8_t buf[256];
    int len;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        g_sys_status.uptime_ms = xTaskGetTickCount();
        g_sys_status.sensor_ok = (g_mpu_dev.i2c != NULL) ? 1 : 0;
        g_sys_status.heap_usage_pct = (100 * (configTOTAL_HEAP_SIZE - xPortGetFreeHeapSize()))
                                      / configTOTAL_HEAP_SIZE;

        TaskWDT_Check();

        unsigned int stk_acq = uxTaskGetStackHighWaterMark(g_h_acquire);
        unsigned int stk_com = uxTaskGetStackHighWaterMark(g_h_comm);
        unsigned int stk_ctrl= uxTaskGetStackHighWaterMark(g_h_control);
        unsigned int stk_mon = uxTaskGetStackHighWaterMark(g_h_monitor);

        len = snprintf((char*)buf, sizeof(buf),
            "[MON] up=%lus  f=%lu/%lu  ce=%lu  h=%u%%  sen=%s  "
            "ctrl={yaw=%.1f, miss=%lu, cmd=%lu, estop=%d}  "
            "stk={A:%u C:%u L:%u M:%u}\r\n",
            g_sys_status.uptime_ms / 1000,
            g_sys_status.total_frames,
            g_sys_status.dropped_frames,
            g_sys_status.comm_errors,
            (unsigned int)g_sys_status.heap_usage_pct,
            g_sys_status.sensor_ok ? "OK" : "FAIL",
            (double)g_ctrl_state.current_yaw,
            (unsigned long)g_ctrl_state.missed_cycles,
            (unsigned long)g_ctrl_state.cmd_count,
            g_ctrl_state.emergency_stop,
            stk_acq, stk_com, stk_ctrl, stk_mon);

        if (xSemaphoreTake(g_uart_tx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            HAL_UART_Transmit(&CONSOLE_UART, buf, len, 100);
            xSemaphoreGive(g_uart_tx_mutex);
        }
    }
}

// ============================================================
//  创建所有任务和队列
// ============================================================
void AppTasks_Init(void)
{
    DWT_Init();

    // --- 加载持久化配置 ---
    SysCfg_Load(&g_sys_cfg);
    g_config.dlpf     = g_sys_cfg.dlpf;
    g_config.accel_fs = g_sys_cfg.accel_fs;
    g_config.gyro_fs  = g_sys_cfg.gyro_fs;
    g_config.rate_hz  = g_sys_cfg.rate_hz;
    g_config.rate_hz  = 100;  // force 100Hz

    // --- 初始化电机驱动 ---
    Motor_Init();
    Control_Init();

    // --- 初始化应用看门狗 ---
    TaskWDT_Init();

    // --- 创建队列 ---
    g_rawDataQ    = xQueueCreate(32, sizeof(RawSensorData_t));
    g_sensorDataQ = xQueueCreate(8,  sizeof(RawSensorData_t));
    g_cmdQ        = xQueueCreate(8,  sizeof(ControlCmd_t));

    if (!g_rawDataQ || !g_sensorDataQ || !g_cmdQ) {
        Error_Handler();
    }

    // --- UART TX 互斥锁 ---
    g_uart_tx_mutex = xSemaphoreCreateMutex();
    if (!g_uart_tx_mutex) {
        Error_Handler();
    }

    // --- 创建任务 ---
    xTaskCreate(Task_Control, "Control", STACK_CONTROL, NULL, PRIO_CONTROL, &g_h_control);
    xTaskCreate(Task_Acquire, "Acquire", STACK_ACQUIRE, NULL, PRIO_ACQUIRE, &g_h_acquire);
    xTaskCreate(Task_Comm,    "Comm",    STACK_COMM,    NULL, PRIO_COMM,    &g_h_comm);
    xTaskCreate(Task_Monitor, "Monitor", STACK_COMM,    NULL, 0,            &g_h_monitor);
    xTaskCreate(Task_CLI,     "CLI",     STACK_CLI,     NULL, PRIO_CLI,     NULL);

    // --- 注册看门狗 ---
    g_wdt_acq  = TaskWDT_Register("Acquire", g_h_acquire, pdMS_TO_TICKS(200));
    g_wdt_comm = TaskWDT_Register("Comm",    g_h_comm,    pdMS_TO_TICKS(200));
    g_wdt_ctrl = TaskWDT_Register("Control", g_h_control, pdMS_TO_TICKS(200));
    g_wdt_cli  = TaskWDT_Register("CLI",     NULL,        pdMS_TO_TICKS(2000));

    // --- CLI 初始化 ---
    CLI_Init();

    // --- 运行时统计定时器 ---
    vConfigureTimerForRunTimeStats();
}
