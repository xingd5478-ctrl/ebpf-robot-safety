#include "motor_control.h"

// --- Pin mapping ---
// Left motors (L1, L2): share same PWM and direction (mechanically linked on same side)
// Right motors (R1, R2): share same PWM and direction
//
// PWM (TIM3, 4 channels, 10kHz):
//   TIM3_CH1: PA6  → Left side ENA
//   TIM3_CH2: PA7  → Right side ENA
//
// Direction (GPIO):
//   PB12: Left IN1
//   PB13: Left IN2
//   PB14: Right IN1
//   PB15: Right IN2

#define DIR_PORT            GPIOB
#define DIR_PIN_LEFT_IN1    GPIO_PIN_12
#define DIR_PIN_LEFT_IN2    GPIO_PIN_13
#define DIR_PIN_RIGHT_IN1   GPIO_PIN_14
#define DIR_PIN_RIGHT_IN2   GPIO_PIN_15

static TIM_HandleTypeDef htim3_motor;

void Motor_Init(void)
{
    // --- GPIO clock ---
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    // --- Direction pins: PB12-PB15 as push-pull outputs ---
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin   = DIR_PIN_LEFT_IN1 | DIR_PIN_LEFT_IN2 |
                 DIR_PIN_RIGHT_IN1 | DIR_PIN_RIGHT_IN2;
    HAL_GPIO_Init(DIR_PORT, &gpio);

    // All direction pins low initially
    HAL_GPIO_WritePin(DIR_PORT,
        DIR_PIN_LEFT_IN1 | DIR_PIN_LEFT_IN2 | DIR_PIN_RIGHT_IN1 | DIR_PIN_RIGHT_IN2,
        GPIO_PIN_RESET);

    // --- PWM pins: PA6, PA7 as AF push-pull ---
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);

    // --- TIM3 PWM configuration ---
    // 72MHz / 72 = 1MHz timer clock → ARR=99 → 10kHz PWM, 0-999 duty range
    htim3_motor.Instance               = TIM3;
    htim3_motor.Init.Prescaler         = 72 - 1;
    htim3_motor.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3_motor.Init.Period            = 1000 - 1;   // 0..999 range
    htim3_motor.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3_motor.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3_motor);

    // --- PWM output channels ---
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.Pulse      = 0;  // start at 0% duty

    HAL_TIM_PWM_ConfigChannel(&htim3_motor, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim3_motor, &oc, TIM_CHANNEL_2);

    // --- Start PWM outputs ---
    HAL_TIM_PWM_Start(&htim3_motor, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3_motor, TIM_CHANNEL_2);
}

void Motor_Set(MotorChannel_t ch, MotorDir_t dir, uint16_t duty)
{
    if (duty > 999) duty = 999;

    uint16_t gpio_in1, gpio_in2;

    // Select IN1/IN2 pins based on left or right side
    if (ch == MOTOR_L1 || ch == MOTOR_L2) {
        gpio_in1 = DIR_PIN_LEFT_IN1;
        gpio_in2 = DIR_PIN_LEFT_IN2;
    } else {
        gpio_in1 = DIR_PIN_RIGHT_IN1;
        gpio_in2 = DIR_PIN_RIGHT_IN2;
    }

    // Set direction
    switch (dir) {
    case MOTOR_FWD:
        HAL_GPIO_WritePin(DIR_PORT, gpio_in1, GPIO_PIN_SET);
        HAL_GPIO_WritePin(DIR_PORT, gpio_in2, GPIO_PIN_RESET);
        break;
    case MOTOR_BACK:
        HAL_GPIO_WritePin(DIR_PORT, gpio_in1, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DIR_PORT, gpio_in2, GPIO_PIN_SET);
        break;
    default:
        HAL_GPIO_WritePin(DIR_PORT, gpio_in1, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DIR_PORT, gpio_in2, GPIO_PIN_RESET);
        duty = 0;
        break;
    }

    // Set PWM duty
    uint32_t pulse = (uint32_t)duty;
    if (ch == MOTOR_L1 || ch == MOTOR_L2) {
        __HAL_TIM_SET_COMPARE(&htim3_motor, TIM_CHANNEL_1, pulse);
    } else {
        __HAL_TIM_SET_COMPARE(&htim3_motor, TIM_CHANNEL_2, pulse);
    }
}

void Motor_Stop(MotorChannel_t ch)
{
    Motor_Set(ch, MOTOR_STOP, 0);
}

void Motor_ApplyCmd(const RobotCmd_t *cmd)
{
    if (!cmd) return;

    // Emergency stop overrides everything
    if (cmd->emergency_stop) {
        Motor_EmergencyStop();
        return;
    }

    // Differential drive kinematics:
    // v_left  = linear_vel - angular_vel    (in PWM units)
    // v_right = linear_vel + angular_vel

    int32_t v_left  = (int32_t)cmd->linear_vel - (int32_t)cmd->angular_vel;
    int32_t v_right = (int32_t)cmd->linear_vel + (int32_t)cmd->angular_vel;

    // Clamp to valid PWM range
    if (v_left > 999)  v_left = 999;
    if (v_left < -999) v_left = -999;
    if (v_right > 999) v_right = 999;
    if (v_right < -999) v_right = -999;

    // Decompose into direction + duty for each side
    MotorDir_t dir_left;
    uint16_t duty_left;
    if (v_left >= 0) {
        dir_left  = MOTOR_FWD;
        duty_left = (uint16_t)v_left;
    } else {
        dir_left  = MOTOR_BACK;
        duty_left = (uint16_t)(-v_left);
    }

    MotorDir_t dir_right;
    uint16_t duty_right;
    if (v_right >= 0) {
        dir_right  = MOTOR_FWD;
        duty_right = (uint16_t)v_right;
    } else {
        dir_right  = MOTOR_BACK;
        duty_right = (uint16_t)(-v_right);
    }

    // Apply to both motors on each side
    Motor_Set(MOTOR_L1, dir_left,  duty_left);
    Motor_Set(MOTOR_L2, dir_left,  duty_left);
    Motor_Set(MOTOR_R1, dir_right, duty_right);
    Motor_Set(MOTOR_R2, dir_right, duty_right);
}

void Motor_EmergencyStop(void)
{
    // Immediate full brake: IN1=IN2=HIGH (brake) or IN1=IN2=LOW (coast)
    // For H-bridge, both low = coast (safer for not damaging drivers)
    HAL_GPIO_WritePin(DIR_PORT,
        DIR_PIN_LEFT_IN1 | DIR_PIN_LEFT_IN2 | DIR_PIN_RIGHT_IN1 | DIR_PIN_RIGHT_IN2,
        GPIO_PIN_RESET);

    // Kill PWM
    __HAL_TIM_SET_COMPARE(&htim3_motor, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim3_motor, TIM_CHANNEL_2, 0);
}

void Motor_Disable(void)
{
    HAL_TIM_PWM_Stop(&htim3_motor, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim3_motor, TIM_CHANNEL_2);
}

void Motor_Enable(void)
{
    HAL_TIM_PWM_Start(&htim3_motor, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3_motor, TIM_CHANNEL_2);
}
