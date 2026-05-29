#ifndef __CONTROL_TASK_H
#define __CONTROL_TASK_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "motor_control.h"
#include "sensor_fusion.h"

// --- Control loop frequency (Hz) ---
#define CONTROL_FREQ_HZ         100
#define CONTROL_PERIOD_MS       (1000 / CONTROL_FREQ_HZ)    // 10ms

// --- PID parameters ---
// These match the thesis experimental parameters:
//   kp=2.5, kd=0.3 for balancing, but for heading control we use
//   simpler P control since yaw is gyro-integration based
#define YAW_KP                  1.8f
#define YAW_KD                  0.15f
#define YAW_KI                  0.02f

// --- Motor dead zone (PWM values below this are ignored) ---
#define MOTOR_DEAD_ZONE         30

// --- Command type from Linux ---
typedef enum {
    CMD_NONE = 0,
    CMD_STOP = 1,
    CMD_MOVE_FWD,
    CMD_MOVE_BACK,
    CMD_TURN_LEFT,
    CMD_TURN_RIGHT,
    CMD_SET_VEL,        // set linear + angular velocity
    CMD_HEADING,        // set target heading (yaw)
    CMD_PID_KP,         // runtime PID tuning
    CMD_PID_KD,
    CMD_PID_KI,
    CMD_EMERGENCY_STOP,
} ControlCmdType_t;

// --- Parsed command from Linux ---
typedef struct {
    ControlCmdType_t type;
    int16_t          value1;    // linear velocity or heading target
    int16_t          value2;    // angular velocity or unused
} ControlCmd_t;

// --- Control task state (for monitoring / telemetry) ---
typedef struct {
    float    current_yaw;
    float    target_yaw;
    float    yaw_error;
    int16_t  motor_left_pwm;
    int16_t  motor_right_pwm;
    float    pid_p_out;
    float    pid_d_out;
    float    pid_i_out;
    uint32_t cycle_count;
    uint32_t missed_cycles;
    uint32_t cmd_count;
    uint8_t  emergency_stop;
    uint8_t  heading_mode;     // 1 = active heading hold, 0 = velocity mode
} ControlState_t;

// --- Global control state (readable by Monitor task) ---
extern ControlState_t g_ctrl_state;

// --- Queue: Linux commands → Control task ---
extern QueueHandle_t g_cmdQ;

// --- Fusion state (shared with Acquire task) ---
extern FusionState    g_fusion_state;
extern float          g_fusion_q[4];
extern float          g_fusion_euler[3];  // roll, pitch, yaw

// --- Control task (100Hz, highest priority) ---
void Task_Control(void *param);

// --- Initialize control system ---
void Control_Init(void);

#endif /* __CONTROL_TASK_H */
