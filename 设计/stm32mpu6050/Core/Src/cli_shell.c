#include "cli_shell.h"
#include "tasks/app_tasks.h"
/* 融合算法已移至上位机实现 */
#include "task_watchdog.h"
#include "board_config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* ========== 配置 ========== */
#define CLI_LINE_MAX    80
#define CLI_PROMPT      "> "
#define CLI_RX_TO_MS    50

/* ==================== UART TX 互斥锁辅助 ====================
 * 所有 cli_xxx 发送函数在执行 HAL_UART_Transmit 前获取锁,
 * 防止与 Task_Comm (DMA) 和 Task_Monitor (阻塞) 并发冲突.
 *
 * 注意: CLI_Init() 在调度器启动前执行, 此时无并发, 直接跳过取锁.
 */
static void cli_tx_lock(void)
{
    if (g_uart_tx_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(g_uart_tx_mutex, portMAX_DELAY);
    }
}

static void cli_tx_unlock(void)
{
    if (g_uart_tx_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(g_uart_tx_mutex);
    }
}

/* ========== UART RX 环形缓冲区 (中断模式) ========== */
#define RX_RING_SIZE    64
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

/* 中断接收用的单字节缓冲 (必须在 RAM 中, 由 HAL 写入) */
static uint8_t rx_it_byte;

/* ========== 行缓冲 ========== */
static char line_buf[CLI_LINE_MAX];
static uint8_t line_pos = 0;

/* ========== 链路统计数据 ========== */
/* 类型定义和 g_link_stats 在 data_protocol.h 中 */
#include "data_protocol.h"

/* ========== 内部辅助 ========== */
static void cli_puts(const char *s)
{
    cli_tx_lock();
    HAL_UART_Transmit(&CONSOLE_UART, (uint8_t*)s, strlen(s), 100);
    cli_tx_unlock();
}

static void cli_putc(char c)
{
    cli_tx_lock();
    HAL_UART_Transmit(&CONSOLE_UART, (uint8_t*)&c, 1, 10);
    cli_tx_unlock();
}

static void cli_printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) {
        cli_tx_lock();
        HAL_UART_Transmit(&CONSOLE_UART, (uint8_t*)buf,
            (uint16_t)(n < (int)sizeof(buf) ? n : sizeof(buf)-1), 100);
        cli_tx_unlock();
    }
}

/* ========== 命令处理函数 ========== */
typedef struct {
    const char *name;
    const char *help;
    void (*handler)(int argc, char *argv[]);
} cmd_t;

static void cmd_help(int argc, char *argv[]);
static void cmd_status(int argc, char *argv[]);
static void cmd_dlpf(int argc, char *argv[]);
static void cmd_accel(int argc, char *argv[]);
static void cmd_gyro(int argc, char *argv[]);
static void cmd_rate(int argc, char *argv[]);
static void cmd_reset(int argc, char *argv[]);
static void cmd_echo(int argc, char *argv[]);
static void cmd_save(int argc, char *argv[]);
static void cmd_load(int argc, char *argv[]);
static void cmd_defaults(int argc, char *argv[]);
static void cmd_wdt(int argc, char *argv[]);
static void cmd_calib(int argc, char *argv[]);
static void cmd_tasks(int argc, char *argv[]);
static void cmd_link(int argc, char *argv[]);
static void cmd_monitor(int argc, char *argv[]);

static const cmd_t cmd_table[] = {
    {"help",     "显示本帮助",                              cmd_help},
    {"status",   "显示系统状态",                            cmd_status},
    {"tasks",    "显示 RTOS 任务/队列状态",                  cmd_tasks},
    {"link",     "显示串口链路质量统计",                     cmd_link},
    {"monitor",  "监控任务开关: monitor on/off",                cmd_monitor},
    {"dlpf",     "设置 DLPF: dlpf <0-6>",                   cmd_dlpf},
    {"accel",    "设置加速度量程: accel <0-3>",             cmd_accel},
    {"gyro",     "设置陀螺仪量程: gyro <0-3>",              cmd_gyro},
    {"rate",     "设置采集频率: rate <10-200> Hz",           cmd_rate},
    {"save",     "保存当前配置到 Flash (掉电保留)",          cmd_save},
    {"load",     "从 Flash 加载配置",                        cmd_load},
    {"defaults", "恢复出厂默认配置 (不保存到 Flash)",        cmd_defaults},
    {"wdt",      "显示应用看门狗状态",                       cmd_wdt},
    {"calib",    "显示标定数据",                              cmd_calib},
    {"reset",    "复位传感器",                               cmd_reset},
    {"echo",     "回显测试: echo <消息>",                    cmd_echo},
    {NULL, NULL, NULL}
};

/* ========== 数字安全转换 (带范围校验) ========== */
static int safe_atoi(const char *s, int min, int max, const char *name)
{
    if (!s) {
        cli_printf("  内部错误: 空参数\r\n");
        return min - 1;  /* 触发默认错误 */
    }
    char *end = NULL;
    long val = strtol(s, &end, 0);
    if (end == s || *end != '\0') {
        cli_printf("  无效输入 '%s'! %s 需要整数\r\n", s, name);
        return min - 1;
    }
    if (val < (long)min || val > (long)max) {
        cli_printf("  越界! %s 范围: %d-%d, 收到 %ld\r\n", name, min, max, val);
        return min - 1;
    }
    return (int)val;
}

/* ========== 命令实现 ========== */

static void cmd_help(int argc, char *argv[])
{
    (void)argc; (void)argv;
    cli_puts("\r\n可用命令:\r\n");
    for (const cmd_t *c = cmd_table; c->name; c++) {
        cli_printf("  %-12s %s\r\n", c->name, c->help);
    }
}

static void cmd_status(int argc, char *argv[])
{
    (void)argc; (void)argv;
    unsigned int stk_acq = uxTaskGetStackHighWaterMark(g_h_acquire);
    unsigned int stk_com = uxTaskGetStackHighWaterMark(g_h_comm);
    unsigned int stk_mon = uxTaskGetStackHighWaterMark(g_h_monitor);

    int wdt_fault;
    int wdt_worst = TaskWDT_GetWorstCase(&wdt_fault);

    cli_printf("\r\n===== 系统状态 =====\r\n");
    cli_printf("  运行时间:  %lu s\r\n",  g_sys_status.uptime_ms / 1000);
    cli_printf("  总帧数:    %lu\r\n",    g_sys_status.total_frames);
    cli_printf("  丢帧:      %lu\r\n",    g_sys_status.dropped_frames);
    cli_printf("  通信错误:  %lu\r\n",    g_sys_status.comm_errors);
    cli_printf("  堆使用率:  %u%%\r\n",   (unsigned int)g_sys_status.heap_usage_pct);
    cli_printf("  传感器:    %s\r\n",     g_sys_status.sensor_ok ? "OK" : "FAIL");
    cli_printf("  应用看门狗: %s\r\n",
        (wdt_fault >= (int)TASKWDT_MAX_FAULTS) ? "ALARM" :
        (wdt_fault > 0) ? "WARN" : "OK");
    cli_printf("  栈剩余:    A=%u  C=%u  M=%u\r\n",
               stk_acq, stk_com, stk_mon);
    cli_printf("  当前配置:  %u Hz  DLPF=%u  AccelFS=%u  GyroFS=%u\r\n",
               g_config.rate_hz, g_config.dlpf, g_config.accel_fs, g_config.gyro_fs);
    cli_printf("  配置持久化: %s (v%lu)\r\n",
               SysCfg_Validate(&g_sys_cfg) ? "有效" : "未保存/无效",
               (unsigned long)g_sys_cfg.version);
    cli_printf("  链路 ACK:   %lu/%lu (%s)\r\n",
               g_link_stats.acks_received,
               g_link_stats.frames_sent,
               (g_link_stats.link_status == 1) ? "正常" :
               (g_link_stats.link_status == 2) ? "告警" :
               (g_link_stats.link_status == 3) ? "断链" : "未知");
    cli_puts("==================\r\n");
}

static void cmd_dlpf(int argc, char *argv[])
{
    if (argc < 2) {
        cli_printf("  当前 DLPF = %u (0=260Hz, 6=5Hz)\r\n", g_config.dlpf);
        return;
    }
    int val = safe_atoi(argv[1], 0, 6, "DLPF");
    if (val < 0) return;
    g_config.dlpf = (uint8_t)val;
    MPU6050_SetDLPF(&g_mpu_dev, (MPU6050_DLPF_Bandwidth_t)val);
    cli_printf("  DLPF 已设为 %u (提示: 输入 'save' 保存到 Flash)\r\n", val);
}

static void cmd_accel(int argc, char *argv[])
{
    if (argc < 2) {
        cli_printf("  当前 Accel FS = %u (0=2g, 1=4g, 2=8g, 3=16g)\r\n", g_config.accel_fs);
        return;
    }
    int val = safe_atoi(argv[1], 0, 3, "Accel FS");
    if (val < 0) return;
    g_config.accel_fs = (uint8_t)val;
    MPU6050_SetAccelFullScale(&g_mpu_dev, (MPU6050_AccelFullScale_t)val);
    cli_printf("  Accel 量程已设为 %u (提示: 输入 'save' 保存到 Flash)\r\n", val);
}

static void cmd_gyro(int argc, char *argv[])
{
    if (argc < 2) {
        cli_printf("  当前 Gyro FS = %u (0=250, 1=500, 2=1000, 3=2000 dps)\r\n", g_config.gyro_fs);
        return;
    }
    int val = safe_atoi(argv[1], 0, 3, "Gyro FS");
    if (val < 0) return;
    g_config.gyro_fs = (uint8_t)val;
    MPU6050_SetGyroFullScale(&g_mpu_dev, (MPU6050_GyroFullScale_t)val);
    cli_printf("  Gyro 量程已设为 %u (提示: 输入 'save' 保存到 Flash)\r\n", val);
}

static void cmd_rate(int argc, char *argv[])
{
    if (argc < 2) {
        cli_printf("  当前频率 = %u Hz\r\n", g_config.rate_hz);
        return;
    }
    int val = safe_atoi(argv[1], 10, 500, "Rate");
    if (val < 0) return;
    g_config.rate_hz = (uint16_t)val;
    cli_printf("  采集频率已设为 %u Hz (提示: 输入 'save' 保存到 Flash)\r\n", val);
}

/* ========== save: 配置持久化 ========== */
static void cmd_save(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* 同步当前运行时参数到持久化配置 */
    g_sys_cfg.dlpf     = g_config.dlpf;
    g_sys_cfg.accel_fs = g_config.accel_fs;
    g_sys_cfg.gyro_fs  = g_config.gyro_fs;
    g_sys_cfg.rate_hz  = g_config.rate_hz;

    if (SysCfg_Save(&g_sys_cfg) == HAL_OK) {
        cli_puts("  配置已保存到 Flash (掉电保留)\r\n");
    } else {
        cli_puts("  [ERROR] Flash 写入失败!\r\n");
    }
}

/* ========== load: 从 Flash 加载配置 ========== */
static void cmd_load(int argc, char *argv[])
{
    (void)argc; (void)argv;

    SysCfg_Load(&g_sys_cfg);
    if (!SysCfg_Validate(&g_sys_cfg)) {
        cli_puts("  Flash 中无有效配置, 使用默认值\r\n");
        return;
    }

    g_config.dlpf     = g_sys_cfg.dlpf;
    g_config.accel_fs = g_sys_cfg.accel_fs;
    g_config.gyro_fs  = g_sys_cfg.gyro_fs;
    g_config.rate_hz  = g_sys_cfg.rate_hz;

    MPU6050_SetDLPF(&g_mpu_dev, (MPU6050_DLPF_Bandwidth_t)g_config.dlpf);
    MPU6050_SetAccelFullScale(&g_mpu_dev, (MPU6050_AccelFullScale_t)g_config.accel_fs);
    MPU6050_SetGyroFullScale(&g_mpu_dev, (MPU6050_GyroFullScale_t)g_config.gyro_fs);

    cli_printf("  配置已加载: %u Hz, DLPF=%u, AccelFS=%u, GyroFS=%u\r\n",
               g_config.rate_hz, g_config.dlpf, g_config.accel_fs, g_config.gyro_fs);
}

/* ========== defaults: 恢复出厂设置 ========== */
static void cmd_defaults(int argc, char *argv[])
{
    (void)argc; (void)argv;

    g_config.dlpf     = SYSCFG_DEFAULT_DLPF;
    g_config.accel_fs = SYSCFG_DEFAULT_ACCEL_FS;
    g_config.gyro_fs  = SYSCFG_DEFAULT_GYRO_FS;
    g_config.rate_hz  = SYSCFG_DEFAULT_RATE_HZ;

    MPU6050_SetDLPF(&g_mpu_dev, (MPU6050_DLPF_Bandwidth_t)g_config.dlpf);
    MPU6050_SetAccelFullScale(&g_mpu_dev, (MPU6050_AccelFullScale_t)g_config.accel_fs);
    MPU6050_SetGyroFullScale(&g_mpu_dev, (MPU6050_GyroFullScale_t)g_config.gyro_fs);

    cli_printf("  已恢复出厂默认: %u Hz, DLPF=%u, AccelFS=%u, GyroFS=%u\r\n",
               g_config.rate_hz, g_config.dlpf, g_config.accel_fs, g_config.gyro_fs);
    cli_puts("  输入 'save' 可保存到 Flash\r\n");
}

/* ========== wdt: 看门狗状态 ========== */
static void cmd_wdt(int argc, char *argv[])
{
    (void)argc; (void)argv;

    int worst_fault;
    int worst_id = TaskWDT_GetWorstCase(&worst_fault);

    cli_printf("\r\n应用看门狗状态 (超时阈值: %u 次):\r\n", TASKWDT_MAX_FAULTS);
    cli_printf("  最差任务ID: ");
    if (worst_id >= 0)
        cli_printf("%d, 连续超时: %d 次\r\n", worst_id, worst_fault);
    else
        cli_puts("无\r\n");
    cli_printf("  状态: %s\r\n",
        (worst_fault >= (int)TASKWDT_MAX_FAULTS) ? "ALARM" :
        (worst_fault > 0) ? "WARN" : "OK");
}

/* ========== tasks: RTOS 任务/队列调试 ========== */
static void cmd_tasks(int argc, char *argv[])
{
    (void)argc; (void)argv;

    cli_printf("\r\n===== RTOS 任务状态 =====\r\n");
    cli_printf("  %-10s %-8s %-8s\r\n", "名称", "栈剩余", "WDT");
    cli_printf("  -------------------------------\r\n");

    struct { TaskHandle_t h; const char *n; int wdt_id; } tasks[] = {
        {g_h_acquire, "Acquire", g_wdt_acq},
        {g_h_comm,    "Comm",    g_wdt_comm},
        {g_h_monitor, "Monitor", -1},
    };

    for (int i = 0; i < 3; i++) {
        unsigned int stk = uxTaskGetStackHighWaterMark(tasks[i].h);
        const char *wdt_status = "---";
        if (tasks[i].wdt_id >= 0) {
            int fc;
            TaskWDT_GetWorstCase(&fc);
            wdt_status = (fc >= (int)TASKWDT_MAX_FAULTS) ? "ALARM" :
                         (fc > 0) ? "WARN" : "OK";
        }
        cli_printf("  %-10s %-8u %-8s\r\n", tasks[i].n, stk, wdt_status);
    }

    cli_printf("\r\n  队列状态:\r\n");
    cli_printf("  %-10s %-8s %-8s\r\n", "队列", "已用", "剩余");
    cli_printf("  -------------------------------\r\n");

    if (g_rawDataQ) {
        UBaseType_t msgs = uxQueueMessagesWaiting(g_rawDataQ);
        UBaseType_t free = uxQueueSpacesAvailable(g_rawDataQ);
        cli_printf("  %-10s %-8u %-8u\r\n", "RawData", (unsigned)msgs, (unsigned)free);
    }
    cli_puts("==========================\r\n");
}

/* ========== link: 串口链路质量 ========== */
static void cmd_link(int argc, char *argv[])
{
    (void)argc; (void)argv;
    cli_printf("\r\n===== 串口链路质量 =====\r\n");
    cli_printf("  发送帧数:   %lu\r\n", g_link_stats.frames_sent);
    cli_printf("  ACK收到:    %lu\r\n", g_link_stats.acks_received);
    cli_printf("  ACK丢失:    %lu\r\n", g_link_stats.acks_missed);
    cli_printf("  连续丢失:   %lu\r\n", g_link_stats.consecutive_miss);

    const char *status_str = "未知";
    if (g_link_stats.link_status == 1) status_str = "正常";
    else if (g_link_stats.link_status == 2) status_str = "告警";
    else if (g_link_stats.link_status == 3) status_str = "断链";
    cli_printf("  链路状态:   %s\r\n", status_str);

    if (g_link_stats.frames_sent > 0) {
        unsigned int ack_rate = (unsigned int)
            (g_link_stats.acks_received * 100 / g_link_stats.frames_sent);
        cli_printf("  ACK率:      %u%%\r\n", ack_rate);
    }
    cli_puts("==========================\r\n");
}

static void cmd_monitor(int argc, char *argv[])
{
    if (argc < 2) {
        cli_printf("  Monitor 任务当前为 %s\r\n",
                   eTaskGetState(g_h_monitor) == eSuspended ? "暂停 (off)" : "运行 (on)");
        return;
    }
    if (strcmp(argv[1], "off") == 0) {
        vTaskSuspend(g_h_monitor);
        cli_puts("  Monitor 任务已暂停, 不再占用 UART\r\n");
    } else if (strcmp(argv[1], "on") == 0) {
        vTaskResume(g_h_monitor);
        cli_puts("  Monitor 任务已恢复\r\n");
    } else {
        cli_printf("  用法: monitor on|off  (当前 = %s)\r\n",
                   eTaskGetState(g_h_monitor) == eSuspended ? "暂停" : "运行");
    }
}

static void cmd_calib(int argc, char *argv[])
{
    cli_printf("\r\n标定数据:\r\n");
    cli_printf("  Accel偏移: %d %d %d\r\n",
               g_sys_cfg.accel_offset[0],
               g_sys_cfg.accel_offset[1],
               g_sys_cfg.accel_offset[2]);
    cli_printf("  Gyro偏移:  %.3f %.3f %.3f\r\n",
               (double)g_sys_cfg.gyro_offset[0],
               (double)g_sys_cfg.gyro_offset[1],
               (double)g_sys_cfg.gyro_offset[2]);
}

static void cmd_reset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    if (MPU6050_Init(&g_mpu_dev) == HAL_OK) {
        cli_puts("  传感器复位成功\r\n");
    } else {
        cli_puts("  传感器复位失败!\r\n");
    }
}

static void cmd_echo(int argc, char *argv[])
{
    cli_puts("  ");
    for (int i = 1; i < argc; i++) {
        cli_puts(argv[i]);
        if (i < argc - 1) cli_putc(' ');
    }
    cli_puts("\r\n");
}

/* ========== UART RX 中断回调 ========== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        rx_ring[rx_head] = rx_it_byte;
        rx_head = (uint8_t)((rx_head + 1) % RX_RING_SIZE);
        /* 使用标准 HAL API 重新使能单字节接收, 避免绕过状态机 */
        HAL_UART_Receive_IT(huart, &rx_it_byte, 1);
    }
}

/* 从环形缓冲区读取一个字节 (非阻塞) */
static int uart_rx_getc(uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline) {
        taskENTER_CRITICAL();
        if (rx_head != rx_tail) {
            uint8_t c = rx_ring[rx_tail];
            rx_tail = (uint8_t)((rx_tail + 1) % RX_RING_SIZE);
            taskEXIT_CRITICAL();
            return (int)c;
        }
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(1));  /* 让出 CPU 给低优先级任务 */
    }
    return -1; /* 超时 */
}

/* ========== 行处理 ========== */
static void process_line(const char *line, uint8_t len)
{
    if (len == 0) return;

    char buf[CLI_LINE_MAX];
    uint8_t copy_len = (len < CLI_LINE_MAX - 1) ? len : CLI_LINE_MAX - 1;
    memcpy(buf, line, copy_len);
    buf[copy_len] = '\0';

    char *argv[8];
    int argc = 0;
    char *token = strtok(buf, " \t");
    while (token && argc < 8) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }

    if (argc == 0) return;

    for (const cmd_t *c = cmd_table; c->name; c++) {
        if (strcmp(argv[0], c->name) == 0) {
            c->handler(argc, argv);
            return;
        }
    }

    /* 尝试作为机器人运动命令转发到 g_cmdQ */
    {
        /* 复用 app_tasks.c 的命令解析逻辑: 手动匹配 */
        int v1 = (argc >= 2) ? atoi(argv[1]) : 0;
        int v2 = (argc >= 3) ? atoi(argv[2]) : 0;
        ControlCmd_t cmd;
        cmd.value1 = (int16_t)v1;
        cmd.value2 = (int16_t)v2;
        int matched = 0;

        if (strcmp(argv[0], "STOP") == 0) {
            cmd.type = CMD_STOP; g_last_cmd_id = 1; matched = 1;
        } else if (strcmp(argv[0], "ESTOP") == 0) {
            cmd.type = CMD_EMERGENCY_STOP; g_last_cmd_id = 7; matched = 1;
        } else if (strcmp(argv[0], "FWD") == 0) {
            cmd.type = CMD_MOVE_FWD; g_last_cmd_id = 2;
            if (v1 == 0) cmd.value1 = 400;
            matched = 1;
        } else if (strcmp(argv[0], "BACK") == 0) {
            cmd.type = CMD_MOVE_BACK; g_last_cmd_id = 3;
            if (v1 == 0) cmd.value1 = 400;
            matched = 1;
        } else if (strcmp(argv[0], "LEFT") == 0) {
            cmd.type = CMD_TURN_LEFT; g_last_cmd_id = 4;
            if (v1 == 0) cmd.value1 = 300;
            matched = 1;
        } else if (strcmp(argv[0], "RIGHT") == 0) {
            cmd.type = CMD_TURN_RIGHT; g_last_cmd_id = 5;
            if (v1 == 0) cmd.value1 = 300;
            matched = 1;
        } else if (strcmp(argv[0], "VEL") == 0) {
            cmd.type = CMD_SET_VEL; g_last_cmd_id = 6; matched = 1;
        } else if (strcmp(argv[0], "HEAD") == 0) {
            cmd.type = CMD_HEADING; g_last_cmd_id = 8; matched = 1;
        }

        if (matched) {
            if (xQueueSend(g_cmdQ, &cmd, 0) == pdPASS)
                return;  /* 命令已转发, 静默处理 */
        }
    }

    cli_printf("  未知命令: %s  (输入 help 查看可用命令)\r\n", argv[0]);
}

/* ========== 初始化 ========== */
void CLI_Init(void)
{
    cli_puts("\r\n=== MPU6050 CLI Shell v2 (生产级) ===\r\n");
    cli_puts("输入 help 查看可用命令\r\n");

    /* 启动 UART 中断接收 (在 CLI 任务运行前先使能) */
    HAL_UART_Receive_IT(&CONSOLE_UART, &rx_it_byte, 1);

    cli_puts(CLI_PROMPT);
}

/* ========== CLI 任务 ========== */
void Task_CLI(void *param)
{
    (void)param;

    for (;;) {
        int c = uart_rx_getc(CLI_RX_TO_MS);
        if (c < 0) {
            TaskWDT_CheckIn(g_wdt_cli);
            continue;
        }

        TaskWDT_CheckIn(g_wdt_cli);

        if (c == '\r' || c == '\n') {
            cli_puts("\r\n");
            process_line((const char*)line_buf, line_pos);
            line_pos = 0;
            cli_puts(CLI_PROMPT);
        } else if (c == '\b' || c == 127) {
            if (line_pos > 0) {
                line_pos--;
                cli_puts("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            if (line_pos < CLI_LINE_MAX - 1) {
                line_buf[line_pos++] = (uint8_t)c;
                cli_putc((char)c);
            }
        }
    }
}
