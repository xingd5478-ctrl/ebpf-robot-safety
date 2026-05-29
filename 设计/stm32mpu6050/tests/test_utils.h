#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ========== 测试框架宏 ========== */
static int g_test_passed = 0;
static int g_test_failed = 0;
static int g_test_assert_count = 0;

#define TEST_BEGIN(name) do { \
    printf("  TEST: %-35s ", name); \
    g_test_assert_count = 0; \
} while(0)

#define TEST_END() do { \
    if (g_test_assert_count == 0) { \
        printf("PASS (no asserts)\n"); \
        g_test_passed++; \
    } else { \
        printf("PASS\n"); \
        g_test_passed++; \
    } \
} while(0)

#define ASSERT_INT_EQ(a, b) do { \
    g_test_assert_count++; \
    if ((a) != (b)) { \
        printf("\n    ASSERT FAIL at %s:%d: expected %d, got %d", \
               __FILE__, __LINE__, (int)(b), (int)(a)); \
        printf("\n    "); \
        g_test_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FLOAT_NEAR(a, b, eps) do { \
    g_test_assert_count++; \
    float _diff = fabsf((float)(a) - (float)(b)); \
    if (_diff > (eps)) { \
        printf("\n    ASSERT FAIL at %s:%d: expected %.6f, got %.6f (diff=%.6f > %.6f)", \
               __FILE__, __LINE__, (double)(b), (double)(a), (double)_diff, (double)(eps)); \
        printf("\n    "); \
        g_test_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    g_test_assert_count++; \
    if (!(cond)) { \
        printf("\n    ASSERT FAIL at %s:%d: expected TRUE", __FILE__, __LINE__); \
        printf("\n    "); \
        g_test_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    g_test_assert_count++; \
    if ((cond)) { \
        printf("\n    ASSERT FAIL at %s:%d: expected FALSE", __FILE__, __LINE__); \
        printf("\n    "); \
        g_test_failed++; \
        return; \
    } \
} while(0)

/* 输出测试汇总 */
#define TEST_SUMMARY() do { \
    int total = g_test_passed + g_test_failed; \
    printf("\n========================================\n"); \
    printf("  Total: %d, Passed: %d, Failed: %d\n", \
           total, g_test_passed, g_test_failed); \
    printf("========================================\n"); \
    return g_test_failed > 0 ? 1 : 0; \
} while(0)

#endif /* TEST_UTILS_H */
