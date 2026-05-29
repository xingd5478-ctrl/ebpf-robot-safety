#!/bin/bash
# Build and run all host-side unit tests
# Usage: ./run_tests.sh

set -e

CC="${CC:-gcc}"
CFLAGS="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter"
INC="-I../stm32mpu6050/Core/Inc -I."

PASS=0
FAIL=0

# 1. sensor_fusion 测试
echo ""
echo "=============================="
echo "  Building sensor_fusion test"
echo "=============================="
$CC $CFLAGS $INC \
    ../stm32mpu6050/Core/Src/sensor_fusion.c \
    test_sensor_fusion.c -lm -o test_sensor_fusion 2>&1
echo "Running..."
if ./test_sensor_fusion; then
    PASS=$((PASS+1))
else
    FAIL=$((FAIL+1))
fi

# 2. CRC 测试
echo ""
echo "======================"
echo "  Building CRC test"
echo "======================"
$CC $CFLAGS $INC \
    test_crc.c -lm -o test_crc 2>&1
echo "Running..."
if ./test_crc; then
    PASS=$((PASS+1))
else
    FAIL=$((FAIL+1))
fi

# 3. Protocol 测试
echo ""
echo "=============================="
echo "  Building Protocol test"
echo "=============================="
$CC $CFLAGS $INC \
    test_protocol.c -lm -o test_protocol 2>&1
echo "Running..."
if ./test_protocol; then
    PASS=$((PASS+1))
else
    FAIL=$((FAIL+1))
fi

echo ""
echo "=============================="
echo "  All test suites: $PASS passed, $FAIL failed"
echo "=============================="
exit $FAIL
