#include "main.h"
#include "bsp_mpu6050.h"
#include "data_protocol.h"
#include "tasks/app_tasks.h"
#include <stdio.h>
#include <math.h>

/* ========== 外设句柄 ========== */
I2C_HandleTypeDef hi2c1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;

/* ========== 函数原型 ========== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);

#ifdef SELF_TEST
static void run_self_test(void);
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* ---- 设置向量表偏移 (Bootloader 场景) ---- */
#if defined(APP_START_OFFSET) && (APP_START_OFFSET > 0)
    SCB->VTOR = FLASH_BASE | APP_START_OFFSET;
#endif

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_TIM2_Init();
    MX_USART1_UART_Init();

    /* ---- 输出系统时钟频率 ---- */
    {
        uint8_t msg[80];
        UART_HandleTypeDef *huart = &huart1;
        int n = sprintf((char*)msg, "[BOOT] SystemCoreClock=%lu HSE=%d\r\n",
                        SystemCoreClock, __HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY));
        HAL_UART_Transmit(huart, msg, n, 100);
    }

    /* ---- 启动确认: 能看到此消息则 MCU 正常运行 ---- */
    {
        uint8_t msg[] = "[BOOT] MCU started, initializing...\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg)-1, 100);
    }

    /* ---- 初始化MPU6050（注入I2C句柄） ---- */
    g_mpu_dev.i2c = &hi2c1;
    g_mpu_dev.accel_fs = MPU6050_ACCEL_FS_2G;
    g_mpu_dev.gyro_fs  = MPU6050_GYRO_FS_250DPS;

    if (MPU6050_Init(&g_mpu_dev) != HAL_OK) {
        /* 诊断模式：即使传感器初始化失败也继续执行 */
        uint8_t msg[] = "[WARN] MPU6050_Init failed, continuing in diagnostic mode\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg)-1, 100);
        // Error_Handler();
    } else {
        uint8_t msg[] = "[INFO] MPU6050_Init OK\r\n";
        HAL_UART_Transmit(&huart1, msg, sizeof(msg)-1, 100);
    }

    /* ---- 独立看门狗: 超时约1秒 (LSI 40kHz / 64 * 625 ≈ 1s) ---- */
    {
        __HAL_RCC_LSI_ENABLE();
        while (!__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY));
        IWDG->KR = 0x5555;          /* 取消写保护 */
        IWDG->PR = 6;                /* 预分频 256 (PR[2:0]=6 => /256) */
        IWDG->RLR = 15000;           /* 重载值 → 超时约 2500*256/40k ≈ 16s (诊断用) */
        IWDG->KR = 0xCCCC;          /* 启动 IWDG */
    }

#ifdef SELF_TEST
    run_self_test();
#endif

    /* ---- 创建 FreeRTOS 任务和队列 ---- */
    AppTasks_Init();

    /* ---- 启动调度器 (不再返回) ---- */
    vTaskStartScheduler();

    /* 调度器启动失败才会到达这里 */
    Error_Handler();
}

/* ========== 自检模式 ========== */
#ifdef SELF_TEST
static void run_self_test(void)
{
    uint8_t msg[64];
    int len;

    len = sprintf((char*)msg, "[SELF_TEST] MPU6050 SelfTest... ");
    HAL_UART_Transmit(&huart1, msg, len, 100);
    if (MPU6050_SelfTest(&g_mpu_dev) == HAL_OK) {
        len = sprintf((char*)msg, "PASS\r\n");
    } else {
        len = sprintf((char*)msg, "WARN (self-test regs non-zero)\r\n");
    }
    HAL_UART_Transmit(&huart1, msg, len, 100);

    int16_t ax, ay, az, gx, gy, gz;
    float temp;
    if (MPU6050_ReadAll(&g_mpu_dev, &ax, &ay, &az, &gx, &gy, &gz, &temp) == HAL_OK) {
        len = sprintf((char*)msg, "[SELF_TEST] Accel: %6d %6d %6d\r\n", ax, ay, az);
        HAL_UART_Transmit(&huart1, msg, len, 100);
        len = sprintf((char*)msg, "[SELF_TEST] Gyro:  %6d %6d %6d\r\n", gx, gy, gz);
        HAL_UART_Transmit(&huart1, msg, len, 100);
        len = sprintf((char*)msg, "[SELF_TEST] Temp:  %.2f C\r\n", temp);
        HAL_UART_Transmit(&huart1, msg, len, 100);

        float accel_mag = sqrtf((float)(ax*ax + ay*ay + az*az)) / g_mpu_dev.accel_lsb_per_g;
        len = sprintf((char*)msg, "[SELF_TEST] Accel mag: %.2f g", accel_mag);
        HAL_UART_Transmit(&huart1, msg, len, 100);
        if (accel_mag > 0.8f && accel_mag < 1.2f) {
            len = sprintf((char*)msg, " [OK]\r\n");
        } else if (accel_mag > 0.5f && accel_mag < 2.0f) {
            len = sprintf((char*)msg, " [WARN]\r\n");
        } else {
            len = sprintf((char*)msg, " [FAIL]\r\n");
        }
        HAL_UART_Transmit(&huart1, msg, len, 100);
    } else {
        len = sprintf((char*)msg, "[SELF_TEST] ReadAll FAILED\r\n");
        HAL_UART_Transmit(&huart1, msg, len, 100);
    }

    len = sprintf((char*)msg, "[SELF_TEST] Complete\r\n\r\n");
    HAL_UART_Transmit(&huart1, msg, len, 100);
}
#endif /* SELF_TEST */

/* ========== 以下为 CubeMX 生成代码 ========== */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* ---- 优先尝试 HSE (8MHz 外部晶振) ---- */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        /* HSI 回退: HSI 8MHz / 2 × 16 = 64MHz */
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        RCC_OscInitStruct.HSEState = RCC_HSE_OFF;
        RCC_OscInitStruct.HSIState = RCC_HSI_ON;
        RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
        RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;  /* 4MHz × 16 = 64MHz */
        RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
        if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
            Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
        Error_Handler();
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = SENSOR_I2C_SPEED;  /* 使用 board_config.h 中的 I2C 速度配置 */
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
        Error_Handler();
}

static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 7200 - 1;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 400 - 1;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
        Error_Handler();

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
        Error_Handler();

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
        Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 460800;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
