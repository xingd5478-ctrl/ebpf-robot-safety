#include "control_task.h"
#include "tasks/app_tasks.h"
#include "bsp_mpu6050.h"
#include "data_protocol.h"
#include <math.h>

// --- Global state ---
ControlState_t g_ctrl_state = {0};
QueueHandle_t  g_cmdQ = NULL;

// --- Shared fusion output (updated by Control task) ---
FusionState g_fusion_state = {0};
float       g_fusion_q[4]  = {1.0f, 0.0f, 0.0f, 0.0f};
float       g_fusion_euler[3] = {0.0f, 0.0f, 0.0f};

// --- Timestamp for control dt calculation ---
static uint32_t s_last_ctrl_tick = 0;

void Control_Init(void)
{
    g_ctrl_state = (ControlState_t){0};
    Fusion_Init(&g_fusion_state);
}

// Wrap angle difference to [-180, 180]
static float angle_diff(float target, float current)
{
    float diff = target - current;
    while (diff > 180.0f)  diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}

void Task_Control(void *param)
{
    (void)param;
    TickType_t last_wake = xTaskGetTickCount();
    ControlCmd_t cmd;
    RawSensorData_t raw;
    float prev_yaw_error = 0.0f;
    float i_error = 0.0f;
    uint8_t has_data = 0;

    // Wait for first sensor data to arrive
    vTaskDelay(pdMS_TO_TICKS(50));

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        uint32_t now = xTaskGetTickCount();
        uint32_t dt_ticks = now - s_last_ctrl_tick;
        float dt = (float)dt_ticks / 1000.0f;
        s_last_ctrl_tick = now;

        g_ctrl_state.cycle_count++;

        if (dt_ticks > CONTROL_PERIOD_MS * 2.5f) {
            g_ctrl_state.missed_cycles++;
        }

        // --- Check for incoming commands (non-blocking) ---
        while (xQueueReceive(g_cmdQ, &cmd, 0) == pdPASS) {
            g_ctrl_state.cmd_count++;

            switch (cmd.type) {
            case CMD_STOP:
                g_ctrl_state.heading_mode = 0;
                Motor_ApplyCmd(&(RobotCmd_t){0});
                break;

            case CMD_EMERGENCY_STOP:
                g_ctrl_state.emergency_stop = 1;
                g_ctrl_state.heading_mode  = 0;
                Motor_EmergencyStop();
                break;

            case CMD_SET_VEL:
                g_ctrl_state.heading_mode = 0;
                Motor_ApplyCmd(&(RobotCmd_t){
                    .linear_vel  = cmd.value1,
                    .angular_vel = cmd.value2,
                });
                break;

            case CMD_HEADING:
                g_ctrl_state.heading_mode = 1;
                g_ctrl_state.target_yaw   = (float)cmd.value1;
                i_error = 0.0f;
                break;

            case CMD_MOVE_FWD:
                g_ctrl_state.heading_mode = 0;
                Motor_ApplyCmd(&(RobotCmd_t){
                    .linear_vel = cmd.value1,
                });
                break;

            case CMD_MOVE_BACK:
                g_ctrl_state.heading_mode = 0;
                Motor_ApplyCmd(&(RobotCmd_t){
                    .linear_vel = (int16_t)(-(int32_t)cmd.value1),
                });
                break;

            case CMD_TURN_LEFT:
                g_ctrl_state.heading_mode = 0;
                Motor_ApplyCmd(&(RobotCmd_t){
                    .angular_vel = (int16_t)(-(int32_t)cmd.value1),
                });
                break;

            case CMD_TURN_RIGHT:
                g_ctrl_state.heading_mode = 0;
                Motor_ApplyCmd(&(RobotCmd_t){
                    .angular_vel = cmd.value1,
                });
                break;

            default:
                break;
            }
        }

        // --- If emergency stop active, skip everything ---
        if (g_ctrl_state.emergency_stop) {
            continue;
        }

        // --- Drain sensor queue, keep latest frame ---
        // Latest-sample policy: if multiple frames queued, use the newest
        // to minimize control latency.
        {
            RawSensorData_t tmp;
            while (xQueueReceive(g_sensorDataQ, &tmp, 0) == pdPASS) {
                raw = tmp;
                has_data = 1;
            }
        }

        if (!has_data) continue;

        // --- Run Madgwick fusion using raw sensor data from Acquire task ---
        {
            float dt_fusion = dt > 0.001f ? dt : 0.01f;
            Fusion_Update(&g_fusion_state,
                raw.ax, raw.ay, raw.az,
                raw.gx, raw.gy, raw.gz,
                dt_fusion,
                g_fusion_q,
                &g_fusion_euler[0], &g_fusion_euler[1], &g_fusion_euler[2]);
        }

        g_ctrl_state.current_yaw = g_fusion_euler[2];

        // --- Heading PID control ---
        if (g_ctrl_state.heading_mode) {
            float yaw_error = angle_diff(g_ctrl_state.target_yaw, g_ctrl_state.current_yaw);
            g_ctrl_state.yaw_error = yaw_error;

            float p_out = YAW_KP * yaw_error;
            float d_out = 0.0f;
            if (dt > 0.001f) {
                d_out = YAW_KD * (yaw_error - prev_yaw_error) / dt;
            }
            prev_yaw_error = yaw_error;

            i_error += YAW_KI * yaw_error * dt;
            if (i_error > 300.0f) i_error = 300.0f;
            if (i_error < -300.0f) i_error = -300.0f;

            g_ctrl_state.pid_p_out = p_out;
            g_ctrl_state.pid_d_out = d_out;
            g_ctrl_state.pid_i_out = i_error;

            float pid_out = p_out + d_out + i_error;
            if (fabsf(yaw_error) < 2.0f) {
                pid_out = 0.0f;
            }

            int16_t ang_pwm = (int16_t)pid_out;
            if (ang_pwm > 500)  ang_pwm = 500;
            if (ang_pwm < -500) ang_pwm = -500;

            g_ctrl_state.motor_left_pwm  = -ang_pwm;
            g_ctrl_state.motor_right_pwm = ang_pwm;

            Motor_ApplyCmd(&(RobotCmd_t){
                .linear_vel  = 0,
                .angular_vel = ang_pwm,
            });
        }
    }
}
