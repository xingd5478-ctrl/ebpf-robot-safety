/*
 * CRC16 单元测试
 *
 * 编译 (host):
 *   gcc -std=c11 -O2 -I../stm32mpu6050/Core/Inc -I. \
 *       test_crc.c -lm -o test_crc
 *   ./test_crc
 *
 * 注意: data_protocol.c 中的 crc16() 无外部依赖, 可独立编译
 * 但为了简洁, 这里直接内嵌 CRC16 实现。
 */
#include "test_utils.h"
#include <stdint.h>

/* 直接从 data_protocol.c 内嵌 */
static uint16_t crc16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ========== 已知测试向量 ========== */

static void test_empty(void)
{
    TEST_BEGIN("CRC of empty buffer = 0xFFFF");
    uint16_t crc = crc16((uint8_t*)"", 0);
    ASSERT_INT_EQ(crc, 0xFFFF);
    TEST_END();
}

static void test_known_vector(void)
{
    TEST_BEGIN("CRC of \"123456789\" = 0x29B1 (CRC-CCITT)");
    uint16_t crc = crc16((uint8_t*)"123456789", 9);
    ASSERT_INT_EQ(crc, 0x29B1);
    TEST_END();
}

static void test_single_byte_variation(void)
{
    TEST_BEGIN("CRC(0x00) != CRC(0xFF) for same length");
    uint8_t buf0[1] = {0x00};
    uint8_t buf1[1] = {0xFF};
    uint16_t crc0 = crc16(buf0, 1);
    uint16_t crc1 = crc16(buf1, 1);
    ASSERT_FALSE(crc0 == crc1);
    ASSERT_FALSE(crc0 == 0xFFFF);  /* not equal to init value */
    TEST_END();
}

static void test_deterministic(void)
{
    TEST_BEGIN("CRC returns same result for same input");
    uint8_t buf[] = {0xAA, 0x55, 22, 0x01, 0x02, 0x03};
    uint16_t crc1 = crc16(buf, sizeof(buf));
    uint16_t crc2 = crc16(buf, sizeof(buf));
    ASSERT_INT_EQ(crc1, crc2);
    TEST_END();
}

static void test_multi_byte_change(void)
{
    TEST_BEGIN("Different inputs produce different CRCs");
    uint8_t buf1[] = {0xAA, 0x55, 0x01};
    uint8_t buf2[] = {0xAA, 0x55, 0x02};
    uint16_t crc1 = crc16(buf1, sizeof(buf1));
    uint16_t crc2 = crc16(buf2, sizeof(buf2));
    ASSERT_FALSE(crc1 == crc2);
    TEST_END();
}

int main(void)
{
    printf("==============================\n");
    printf("  CRC16-CCITT 单元测试\n");
    printf("==============================\n\n");

    test_empty();
    test_known_vector();
    test_single_byte_variation();
    test_deterministic();
    test_multi_byte_change();

    printf("\n");
    TEST_SUMMARY();
}
