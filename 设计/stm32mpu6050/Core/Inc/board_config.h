#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/*
 * ========== 板级配置 ==========
 *
 * 集中管理外设映射, 更换硬件时只改此文件.
 * 例如从 USART1 → USART2, 只需改 CONSOLE_UART 定义.
 *
 * 使用方式: 在需要外设的文件中 #include "board_config.h",
 * 然后使用 CONSOLE_UART, SENSOR_I2C 等宏, 而非直接引用 huart1/hi2c1.
 */

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 串口控制台 ==========
 * 用于 CLI shell + Monitor 输出 + 数据通信
 */
#define CONSOLE_UART        huart1
#define CONSOLE_UART_IRQ    USART1_IRQn
#define CONSOLE_UART_DMA_TX DMA1_Channel4_IRQn  /* 取决于具体 DMA 配置 */

/* ========== I2C 传感器总线 ==========
 * MPU6050 挂载于此总线
 */
#define SENSOR_I2C          hi2c1
#define SENSOR_I2C_SPEED    100000    /* 100kHz 标准模式 (STM32F103 I2C 在 400kHz 突发读有数据损坏 bug, 降速解决) */

/* ========== 运行时统计定时器 ========== */
#define STAT_TIMER          TIM4
#define STAT_TIMER_IRQ      TIM4_IRQn
#define STAT_TIMER_PSC      72000     /* 72MHz / 72000 = 1kHz */
#define STAT_TIMER_ARR      0xFFFF

/* ========== 其他系统参数 ========== */
#define SYS_TICK_HZ         1000      /* FreeRTOS tick 频率 */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_CONFIG_H */
