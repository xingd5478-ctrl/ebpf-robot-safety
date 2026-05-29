#include "sensor_fusion.h"
#include <math.h>

#define ACCEL_LSB_PER_G     16384.0f
#define GYRO_LSB_PER_DPS    131.0f
#define Kp                  8.0f     /* 收敛速度, 越大对加速度修正响应越快 */
#define Ki                  0.005f
#define DEG_TO_RAD          0.0174533f
#define RAD_TO_DEG          57.29578f

void Fusion_Init(FusionState *state)
{
    if (!state) return;
    state->q0 = 1.0f;
    state->q1 = 0.0f;
    state->q2 = 0.0f;
    state->q3 = 0.0f;
    state->integralFBx = 0.0f;
    state->integralFBy = 0.0f;
    state->integralFBz = 0.0f;
}

void Fusion_Reset(FusionState *state)
{
    Fusion_Init(state);
}

void Fusion_Update(FusionState *s,
                   int16_t ax_raw, int16_t ay_raw, int16_t az_raw,
                   int16_t gx_raw, int16_t gy_raw, int16_t gz_raw,
                   float dt,
                   float *q_out,
                   float *roll, float *pitch, float *yaw)
{
    float recipNorm;
    float halfvx, halfvy, halfvz;
    float halfex, halfey, halfez;
    float qa, qb, qc;

    if (!s) return;

    /* 转换为物理单位 */
    float ax = (float)ax_raw / ACCEL_LSB_PER_G;
    float ay = (float)ay_raw / ACCEL_LSB_PER_G;
    float az = (float)az_raw / ACCEL_LSB_PER_G;
    float gx = (float)gx_raw / GYRO_LSB_PER_DPS * DEG_TO_RAD;
    float gy = (float)gy_raw / GYRO_LSB_PER_DPS * DEG_TO_RAD;
    float gz = (float)gz_raw / GYRO_LSB_PER_DPS * DEG_TO_RAD;

    /* 加速度向量归一化 */
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (norm < 1e-6f) goto output;
    recipNorm = 1.0f / norm;
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    /* 根据当前四元数估计重力方向 */
    halfvx = s->q1 * s->q3 - s->q0 * s->q2;
    halfvy = s->q0 * s->q1 + s->q2 * s->q3;
    halfvz = s->q0 * s->q0 - 0.5f + s->q3 * s->q3;

    /* 计算误差 (叉积) */
    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    /* 积分误差 */
    if (Ki > 0.0f) {
        s->integralFBx += Ki * halfex * dt;
        s->integralFBy += Ki * halfey * dt;
        s->integralFBz += Ki * halfez * dt;
        gx += s->integralFBx;
        gy += s->integralFBy;
        gz += s->integralFBz;
    } else {
        s->integralFBx = 0.0f;
        s->integralFBy = 0.0f;
        s->integralFBz = 0.0f;
    }

    /* 应用比例反馈 */
    gx += Kp * halfex;
    gy += Kp * halfey;
    gz += Kp * halfez;

    /* 一阶龙格库塔更新四元数 */
    gx *= (0.5f * dt);
    gy *= (0.5f * dt);
    gz *= (0.5f * dt);
    qa = s->q0; qb = s->q1; qc = s->q2;
    s->q0 += (-qb * gx - qc * gy - s->q3 * gz);
    s->q1 += ( qa * gx + qc * gz - s->q3 * gy);
    s->q2 += ( qa * gy - qb * gz + s->q3 * gx);
    s->q3 += ( qa * gz + qb * gy - qc * gx);

    /* 归一化四元数 */
    norm = sqrtf(s->q0 * s->q0 + s->q1 * s->q1 + s->q2 * s->q2 + s->q3 * s->q3);
    if (norm > 1e-6f) {
        recipNorm = 1.0f / norm;
        s->q0 *= recipNorm;
        s->q1 *= recipNorm;
        s->q2 *= recipNorm;
        s->q3 *= recipNorm;
    }

output:
    if (q_out) {
        q_out[0] = s->q0;
        q_out[1] = s->q1;
        q_out[2] = s->q2;
        q_out[3] = s->q3;
    }

    if (roll && pitch && yaw) {
        *roll  = atan2f(2.0f * (s->q0*s->q1 + s->q2*s->q3),
                        1.0f - 2.0f * (s->q1*s->q1 + s->q2*s->q2)) * RAD_TO_DEG;
        *pitch = asinf(2.0f * (s->q0*s->q2 - s->q3*s->q1)) * RAD_TO_DEG;
        *yaw   = atan2f(2.0f * (s->q0*s->q3 + s->q1*s->q2),
                        1.0f - 2.0f * (s->q2*s->q2 + s->q3*s->q3)) * RAD_TO_DEG;
    }
}

void Fusion_GetEuler(const FusionState *s,
                     float *roll, float *pitch, float *yaw)
{
    if (!s || !roll || !pitch || !yaw) return;
    *roll  = atan2f(2.0f * (s->q0*s->q1 + s->q2*s->q3),
                    1.0f - 2.0f * (s->q1*s->q1 + s->q2*s->q2)) * RAD_TO_DEG;
    *pitch = asinf(2.0f * (s->q0*s->q2 - s->q3*s->q1)) * RAD_TO_DEG;
    *yaw   = atan2f(2.0f * (s->q0*s->q3 + s->q1*s->q2),
                    1.0f - 2.0f * (s->q2*s->q2 + s->q3*s->q3)) * RAD_TO_DEG;
}
