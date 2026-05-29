/*
 * FreeRTOS 中断向量桥接 + SysTick 覆盖
 *
 * 1. 中断向量: startup_stm32f103xb.s 中 PendSV_Handler 和 SVC_Handler 是弱符号,
 *    FreeRTOS port.c 提供 xPortPendSVHandler 和 vPortSVCHandler。
 *    此处提供强符号裸函数跳板, 将向量表重定向到 FreeRTOS 实现。
 *
 * 2. SysTick 覆盖: port.c 默认的弱符号 vPortSetupTimerInterrupt 使用
 *    compile-time 宏 configSYSTICK_CLOCK_HZ 计算 SysTick 重装载值。
 *    若实际系统时钟与 72MHz 有偏差, 所有 FreeRTOS 定时将等比偏移。
 *    此处提供强符号版本, 在运行时读取 SystemCoreClock, 确保 SysTick 周期正确。
 */

#include "stm32f1xx.h"      /* SystemCoreClock, SysTick_Type 等 */
#include "FreeRTOS.h"       /* configTICK_RATE_HZ */

/* ========== PendSV: 上下文切换 ========== */
__attribute__((naked))
void PendSV_Handler(void)
{
    __asm volatile("b xPortPendSVHandler");
}

/* ========== SVC: 启动第一个任务 ========== */
__attribute__((naked))
void SVC_Handler(void)
{
    __asm volatile("b vPortSVCHandler");
}

/* ========== SysTick 初始化覆盖 ==========
 *
 * 默认实现 (port.c 弱符号) 使用 configSYSTICK_CLOCK_HZ:
 *     SysTick->LOAD = (configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ) - 1;
 *
 * 如果 configSYSTICK_CLOCK_HZ 硬编码为 72000000 而实际系统时钟不同,
 * SysTick 周期将偏离 1ms, 导致所有 FreeRTOS 定时等比偏移。
 *
 * 此覆盖使用运行时 SystemCoreClock (由 HAL_RCC_ClockConfig 在 main() 中更新),
 * 使 SysTick 周期始终为正确的 1ms。
 */
void vPortSetupTimerInterrupt(void)
{
    uint32_t reload = SystemCoreClock / configTICK_RATE_HZ;
    SysTick->LOAD = reload - 1UL;
    SysTick->VAL  = 0UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}
