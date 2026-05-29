# STM32 MPU6050 固件 — 下位机

## 概述

基于 STM32F103C8T6 + FreeRTOS 的机器人下位机固件。采用 4 任务流水线架构：**采集 → 控制 → 通信 → 监控**，通过队列解耦各环节。固件端完成 MPU6050 数据采集、Madgwick 姿态融合、PID 航向控制和电机 PWM 输出，通过 32 字节 CRC16 遥测帧（0xBADD）与 Linux 上位机双向通信。

## 快速开始

### 构建

```bash
cd stm32mpu6050
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 带 Bootloader 偏移构建（应用起始 0x08002000）
cmake -B build -DBOOTLOADER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 虚拟数据测试模式（无需 MPU6050 硬件，测试 UART 吞吐量）
cmake -B build -DTEST_DUMMY_DATA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 烧录

```bash
st-flash write build/stm32mpu6050.bin 0x08000000
# 若使用 Bootloader 分区
st-flash write build/stm32mpu6050.bin 0x08002000
```

## 系统架构

### 任务流水线

```
┌──────────┐  RawSensorData_t  ┌──────────┐  ControlData_t  ┌──────────┐
│ Acquire  │──── Queue ──────→│ Control   │──── Queue ────→│  Comm    │
│ (PRIO:4) │  (g_rawDataQ)    │ (PRIO:4) │ (g_ctrlCmdQ)   │ (PRIO:3) │
└──────────┘                  └─────┬────┘                 └─────┬────┘
      │                             │                            │
      │ MPU6050 I2C                 │ Madgwick + PID             │ USART1 TX/RX
      │ 100Hz 采集                  │ 电机 PWM (TIM3, 10kHz)     │ 460800 bps
      │                             │                            │
      │                    ┌────────┴────────┐          ┌────────┴────────┐
      │                    │  电机驱动 (PWM)  │          │  Linux 上位机    │
      │                    │  AT8236 双路     │          │  (eBPF 监控)     │
      │                    └─────────────────┘          └─────────────────┘
      │
      ├──────────────────────────┐
      │                    ┌─────┴─────┐
      └────────────────────│ Monitor   │
                           │ (PRIO:0)  │
                           │ 5s 周期    │
                           └───────────┘
```

**任务详情**：

| 任务 | 优先级 | 周期 | 职责 |
|------|:-----:|:----:|------|
| Task_Acquire | 4 | 100Hz | MPU6050 I2C 读取（6 轴数据），DWT 计时 |
| Task_Control | 4 | 100Hz | Madgwick 姿态融合 + PID 航向控制 + 电机 PWM 输出 |
| Task_Comm | 3 | 100Hz | 双向串口通信：0xBADD 遥测帧发送 + ASCII 命令接收解析 |
| Task_Monitor | 0 | 5s | 系统健康报告（堆栈余量、WDT 状态、CPU 利用率 ~4.3%） |

### 健壮性设计

- **硬件 IWDG**：空闲任务喂狗，超时约 16 秒
- **应用看门狗 (TaskWDT)**：每个任务周期性签到，3 次容错
- **ESTOP 紧急停止**：电机 PWM 硬件置零 + 软件锁定，需显式解锁
- **UART TX 互斥锁**：防止多任务并发发送
- **I2C 错误恢复**：连续 3 次通信失败自动重新初始化 MPU6050
- **CRC16 帧校验**：遥测帧和命令帧均采用 CRC16-CCITT 保护
- **DWT 微秒级性能追踪**：记录每帧采集抖动（遥测帧字节 26-27）

## 文件说明

### 核心模块 (`Core/Src/` + `Core/Inc/`)

| 文件 | 职责 |
|------|------|
| `main.c` | 系统初始化、时钟配置 (HSE 8MHz × 9 = 72MHz)、外设初始化、启动调度器 |
| `bsp_mpu6050.c / .h` | MPU6050 驱动：I2C 读写、寄存器封装、量程/DLPF/采样率配置、自检 |
| `control_task.c / .h` | Madgwick 姿态融合 + PID 航向控制 + 电机状态管理 |
| `motor_control.c / .h` | 电机 PWM 输出（TIM3, 10kHz）+ ESTOP 紧急停止 |
| `sensor_fusion.c / .h` | Madgwick/Mahony 互补滤波算法 |
| `data_protocol.c / .h` | 32 字节 CRC16 遥测帧协议（0xBADD）+ ASCII 命令解析 |
| `system_config.c / .h` | Flash 持久化配置存储：加载/保存/校验/默认值 |
| `flash_config.c / .h` | Flash 底层擦写操作（页擦除、字节写入） |
| `task_watchdog.c / .h` | 应用级任务监控：注册、签到、超时检测、故障回调 |
| `tasks/app_tasks.c / .h` | 四任务实现：Acquire/Control/Comm/Monitor + 队列/信号量/互斥锁创建 |

### 配置文件

| 文件 | 职责 |
|------|------|
| `board_config.h` | 硬件抽象：板级引脚映射、I2C 速度、UART 宏定义 |
| `FreeRTOSConfig.h` | FreeRTOS 内核配置：堆大小、优先级、钩子函数 |
| `system_config.h` | 系统配置结构定义与默认值 |
| `STM32F103XX_FLASH.ld` | 标准链接脚本（无 Bootloader, 起始 0x08000000） |
| `STM32F103XX_FLASH_APP.ld` | 带偏移链接脚本（APP 起始 0x08002000） |
| `CMakeLists.txt` | CMake 构建系统配置 |

## 命令接口

### 运动控制命令（ASCII 文本，Linux → STM32）

| 命令 | 功能 | 示例 |
|------|------|------|
| `STOP` | 停止（PWM = 0） | `STOP` |
| `ESTOP` | 紧急停止（PWM 置零，需手动解锁） | `ESTOP` |
| `FWD <pwm>` | 前进 | `FWD 400` |
| `BACK <pwm>` | 后退 | `BACK 300` |
| `LEFT <pwm>` | 左转 | `LEFT 200` |
| `RIGHT <pwm>` | 右转 | `RIGHT 200` |
| `VEL <lin> <ang>` | 速度控制 | `VEL 0.5 0.3` |
| `HEAD <deg>` | 航向保持 | `HEAD 90` |

命令通过 FreeRTOS 队列（g_ctrlCmdQ）传递至 Task_Control，由 `parse_command()` 解析。命令 ID（0-8）通过遥测帧字节 25 回显确认。

## 通信协议

### STM32 → Linux：32 字节 CRC16 遥测帧（0xBADD，100 fps）

CRC16-CCITT (poly=0x1021, init=0xFFFF) 校验。

| 字节 | 内容 | 说明 |
|------|------|------|
| 0~1 | 帧头 0xBADD | 帧同步标识 |
| 2 | 序列号 (0-255) | 丢帧检测 |
| 3~4 | 当前偏航角 (int16, ÷10 = °) | Madgwick 融合输出 |
| 5~6 | 目标偏航角 (int16) | PID 设定值 |
| 7~8 | 左电机 PWM (int16) | TIM3 CH1 |
| 9~10 | 右电机 PWM (int16) | TIM3 CH2 |
| 11~16 | 加速度计 XYZ (int16 × 3) | MPU6050 原始值 |
| 17~22 | 陀螺仪 XYZ (int16 × 3) | MPU6050 原始值 |
| 23 | 紧急停止标志 (bool) | ESTOP 激活 = 1 |
| 24 | 航向模式 (bool) | Heading hold = 1 |
| 25 | 最后命令 ID (0-8) | 命令回显 |
| 26~27 | 采集抖动 (uint16, ÷10 = μs) | DWT 实测 |
| 28 | 丢帧计数 (uint8) | 累积丢帧 |
| 29~30 | CRC16-CCITT | 对字节 0-29 |
| 31 | 填充 | 对齐 |

### Linux → STM32：ASCII 文本命令

| 命令 | 功能 |
|------|------|
| `STOP` | 停止（PWM = 0） |
| `ESTOP` | 紧急停止（PWM 置零，锁定） |
| `FWD <pwm>` | 前进 (0-999) |
| `BACK <pwm>` | 后退 (0-999) |
| `LEFT <pwm>` | 左转 (0-999) |
| `RIGHT <pwm>` | 右转 (0-999) |
| `VEL <lin> <ang>` | 速度控制（线速度 + 角速度） |
| `HEAD <deg>` | 航向保持（目标角度） |

## 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 控制频率 | 100Hz | FreeRTOS 四任务同步周期 |
| 串口波特率 | 460800 bps | USART1，8N1 |
| 遥测帧大小 | 32 字节 | 0xBADD 帧头 + CRC16 |
| PID 控制 | P=2.0, I=0.0, D=0.15 | 航向控制（试验参数） |
| 电机 PWM 频率 | 10kHz | TIM3 CH1/CH2 |
| Flash 占用 | ~45.9KB / 64KB (70%) | ARM GCC 13.2.1 编译 |
| RAM 占用 | ~15.4KB / 20KB (75%) | FreeRTOS 堆 8KB |
| CPU 利用率 | ~4.3% | RM 可调度理论上界 78% |

## 常见问题

### Q: 传感器初始化失败？

A: 检查 I2C 接线（SCL PB6, SDA PB7），确认 MPU6050 地址正确 (0x68)。固件会进入诊断模式继续运行并输出告警。

### Q: 如何修改采样率？

A: 修改 `system_config.h` 中 `SYSCFG_DEFAULT_RATE_HZ` 或 `app_tasks.c` 中任务周期后重新编译。

### Q: 任务栈溢出怎么办？

A: Monitor 任务每 5 秒输出各任务堆栈余量。在 `app_tasks.h` 中增大对应任务的 `STACK_xxx` 值。

### Q: 波特率不匹配？

A: 当前固件默认 460800。如需修改，在 `main.c` 的 `MX_USART1_UART_Init()` 中修改 `huart1.Init.BaudRate`。

---

**文档版本**：V3.0  
**最后更新**：2026年5月28日  
**适用对象**：需要理解或修改下位机固件的开发者
