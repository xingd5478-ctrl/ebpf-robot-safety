# 单元测试

## 概述

针对固件核心模块的 PC 端单元测试，可在 Windows/Linux 上直接编译运行，无需 STM32 硬件。

---

## 测试列表

| 测试文件 | 测试对象 | 说明 |
|---------|---------|------|
| `test_crc.c` | `crc16()` | CRC16-CCITT 算法验证（多项式 0x1021，初始值 0xFFFF） |
| `test_protocol.c` | 帧解析/组包 | 二进制帧的组包、解析、CRC 校验正确性 |
| `test_sensor_fusion.c` | `Fusion_Update()` | 固定输入下的四元数输出正确性、边界条件 |

---

## 快速运行

### Windows（已编译）

```bash
cd tests
.\test_crc.exe
.\test_protocol.exe
.\test_sensor_fusion.exe
```

### Linux / 重新编译

```bash
cd tests
gcc -o test_crc test_crc.c ../stm32mpu6050/Core/Src/data_protocol.c -I../stm32mpu6050/Core/Inc -lm
./test_crc
```

或用 `run_tests.sh` 一键运行：

```bash
chmod +x run_tests.sh
./run_tests.sh
```

---

## 测试框架

使用 `test_utils.h` 提供的简单宏实现：

```c
TEST_ASSERT(condition, "description");  // 断言测试
TEST_RUN(test_function);                // 运行测试
```

不依赖第三方测试框架（如 Unity/CMock），保持零依赖。未来可迁移至 CTest。

---

## 扩展测试

在 `tests/` 目录下新建 `test_xxx.c`，包含 `test_utils.h`，实现测试函数，然后添加到 `run_tests.sh` 即可。

---

**文档版本**：V1.1  
**最后更新**：2026年5月7日  
**适用对象**：需要验证固件模块正确性的开发者
