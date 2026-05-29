/*
 * sensor_fusion 单元测试
 *
 * 编译 (host):
 *   gcc -std=c11 -O2 -I../stm32mpu6050/Core/Inc -I. \
 *       ../stm32mpu6050/Core/Src/sensor_fusion.c \
 *       test_sensor_fusion.c -lm -o test_sensor_fusion
 *   ./test_sensor_fusion
 */

#include "test_utils.h"
#include "sensor_fusion.h"

/* ========== 测试 1: Init/Reset 后状态正确 ========== */
static void test_init_reset(void)
{
    TEST_BEGIN("Init produces identity quaternion");
    FusionState s;
    Fusion_Init(&s);

    ASSERT_FLOAT_NEAR(s.q0, 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.q1, 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.q2, 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.q3, 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.integralFBx, 0.0f, 1e-6f);
    TEST_END();
}

static void test_reset(void)
{
    TEST_BEGIN("Reset restores identity quaternion");
    FusionState s;
    Fusion_Init(&s);

    /* 人为修改状态 */
    s.q0 = 0.5f; s.q1 = 0.5f; s.q2 = 0.5f; s.q3 = 0.5f;
    s.integralFBx = 1.0f;

    Fusion_Reset(&s);
    ASSERT_FLOAT_NEAR(s.q0, 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.q1, 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.q2, 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.q3, 0.0f, 1e-6f);
    ASSERT_FLOAT_NEAR(s.integralFBx, 0.0f, 1e-6f);
    TEST_END();
}

/* ========== 测试 2: NULL 指针安全 ========== */
static void test_null_safety(void)
{
    TEST_BEGIN("NULL state ptr does not crash");
    Fusion_Update(NULL, 0,0,0, 0,0,0, 0.01f, NULL, NULL, NULL, NULL);
    Fusion_GetEuler(NULL, NULL, NULL, NULL);
    ASSERT_TRUE(1);
    TEST_END();

    TEST_BEGIN("NULL output ptrs do not crash");
    FusionState s;
    Fusion_Init(&s);
    Fusion_Update(&s, 0,0,16384, 0,0,0, 0.01f, NULL, NULL, NULL, NULL);
    ASSERT_TRUE(1);
    TEST_END();

    TEST_BEGIN("Partial NULL outputs safe");
    Fusion_Init(&s);
    float roll, pitch;
    Fusion_Update(&s, 0,0,16384, 0,0,0, 0.01f, NULL, &roll, &pitch, NULL);
    ASSERT_TRUE(1);
    TEST_END();
}

/* ========== 测试 3: 静止状态 (只有重力) ========== */
static void test_stationary(void)
{
    TEST_BEGIN("Stationary (1g Z) produces ~0 roll/pitch");
    FusionState s;
    Fusion_Init(&s);

    float roll, pitch, yaw;
    /* 模拟静止: ax=0, ay=0, az=1g, 陀螺全零 */
    for (int i = 0; i < 5; i++) {
        Fusion_Update(&s, 0, 0, 16384,  0, 0, 0, 0.01f,
                      NULL, &roll, &pitch, &yaw);
    }

    ASSERT_FLOAT_NEAR(roll, 0.0f, 2.0f);
    ASSERT_FLOAT_NEAR(pitch, 0.0f, 2.0f);
    TEST_END();

    TEST_BEGIN("Stationary (1g X) converges toward -90 deg pitch");
    Fusion_Init(&s);
    for (int i = 0; i < 500; i++) {
        Fusion_Update(&s, 16384, 0, 0,  0, 0, 0, 0.01f,
                      NULL, &roll, &pitch, &yaw);
    }

    /* 验证方向正确且非零: 应该在 -70° ~ -90° 范围内 */
    ASSERT_TRUE(pitch < -30.0f);
    ASSERT_FALSE(pitch < -100.0f);
    TEST_END();
}

/* ========== 测试 4: 四元数输出一致性 ========== */
static void test_quaternion_output(void)
{
    TEST_BEGIN("Quaternion output matches internal state");
    FusionState s;
    Fusion_Init(&s);

    float q_out[4];
    Fusion_Update(&s, 0, 0, 16384,  0, 0, 0, 0.01f,
                  q_out, NULL, NULL, NULL);

    ASSERT_FLOAT_NEAR(q_out[0], s.q0, 1e-6f);
    ASSERT_FLOAT_NEAR(q_out[1], s.q1, 1e-6f);
    ASSERT_FLOAT_NEAR(q_out[2], s.q2, 1e-6f);
    ASSERT_FLOAT_NEAR(q_out[3], s.q3, 1e-6f);
    TEST_END();
}

/* ========== 测试 5: 单位四元数约束 ========== */
static void test_quaternion_normalized(void)
{
    TEST_BEGIN("Quaternion remains normalized after many updates");
    FusionState s;
    Fusion_Init(&s);

    float roll, pitch, yaw;
    for (int i = 0; i < 100; i++) {
        /* 模拟轻微运动 */
        Fusion_Update(&s, 100, 200, 16384,  10, -5, 3, 0.01f,
                      NULL, &roll, &pitch, &yaw);
    }

    float norm = s.q0*s.q0 + s.q1*s.q1 + s.q2*s.q2 + s.q3*s.q3;
    ASSERT_FLOAT_NEAR(norm, 1.0f, 1e-4f);
    TEST_END();
}

/* ========== 测试 6: GetEuler 与 Update 输出一致 ========== */
static void test_get_euler(void)
{
    TEST_BEGIN("GetEuler matches Update Euler output");
    FusionState s;
    Fusion_Init(&s);

    float r1, p1, y1;
    float r2, p2, y2;

    Fusion_Update(&s, 0, 0, 16384,  0, 0, 0, 0.01f,
                  NULL, &r1, &p1, &y1);
    Fusion_GetEuler(&s, &r2, &p2, &y2);

    ASSERT_FLOAT_NEAR(r1, r2, 1e-4f);
    ASSERT_FLOAT_NEAR(p1, p2, 1e-4f);
    ASSERT_FLOAT_NEAR(y1, y2, 1e-4f);
    TEST_END();
}

/* ========== main ========== */
int main(void)
{
    printf("==============================\n");
    printf("  sensor_fusion 单元测试\n");
    printf("==============================\n\n");

    test_init_reset();
    test_reset();
    test_null_safety();
    test_stationary();
    test_quaternion_output();
    test_quaternion_normalized();
    test_get_euler();

    printf("\n");
    TEST_SUMMARY();
}
