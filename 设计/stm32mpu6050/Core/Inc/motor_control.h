#ifndef __MOTOR_CONTROL_H
#define __MOTOR_CONTROL_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Motor channel enum ---
typedef enum {
    MOTOR_L1 = 0,   // left side motor 1
    MOTOR_L2 = 1,   // left side motor 2
    MOTOR_R1 = 2,   // right side motor 1
    MOTOR_R2 = 3,   // right side motor 2
    MOTOR_MAX = 4,
} MotorChannel_t;

// --- Motor direction ---
typedef enum {
    MOTOR_STOP = 0,
    MOTOR_FWD  = 1,   // forward
    MOTOR_BACK = 2,   // backward
} MotorDir_t;

// --- Robot-level motion command ---
typedef struct {
    int16_t linear_vel;     // linear velocity (PWM, -999..999)
    int16_t angular_vel;    // angular velocity (PWM, -999..999)
    uint8_t emergency_stop; // 1 = immediate stop, override everything
} RobotCmd_t;

// --- Initialize TIM3 PWM + GPIO direction pins ---
void Motor_Init(void);

// --- Set single motor: direction + PWM duty ---
// duty: 0-999 (0=stop, 999=full speed)
void Motor_Set(MotorChannel_t ch, MotorDir_t dir, uint16_t duty);

// --- Stop single motor ---
void Motor_Stop(MotorChannel_t ch);

// --- Apply robot-level command (differential drive kinematics) ---
// linear_vel: forward speed PWM (0-999)
// angular_vel: turning rate PWM (0-999)
void Motor_ApplyCmd(const RobotCmd_t *cmd);

// --- Emergency stop all motors immediately ---
void Motor_EmergencyStop(void);

// --- Disable PWM outputs (coast) ---
void Motor_Disable(void);

// --- Enable PWM outputs ---
void Motor_Enable(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_CONTROL_H */
