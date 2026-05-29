# eBPF Robot Safety Monitor

> 基于 eBPF 的机器人实时控制安全监控系统  
> 邢栋 · 2026

---

## 一句话

在 Linux 内核里埋三个探针，无侵入地监控机器人控制循环的**周期抖动**、**串口通信延迟**和**任务调度延迟**——发现异常实时告警。

---

## 目录结构

```
ebpf-robot-safety/
├── 论文/                         # 论文产出
│   ├── paper/                    # 期刊论文（软件学报投稿）
│   │   ├── journal_paper.tex     #  LaTeX 源文件（主文件）
│   │   ├── journal_paper.pdf     #  编译好的 PDF
│   │   ├── journal_paper.docx    #  Word 版本
│   │   ├── figures/              #  论文插图（PDF矢量图）
│   │   ├── 文献报告.md           #  21篇参考文献验证报告
│   │   └── 期刊投稿策略.md       #  投稿策略与时间线
│   └── thesis/                   # 毕业论文
│       ├── document.tex          #  LaTeX 源文件
│       ├── document.pdf          #  编译好的 PDF
│       ├── figures/              #  论文插图
│       ├── experiments.md        #  实验方案与进度
│       └── 参考文献报告.md       #  33篇参考文献验证报告
├── 设计/
│   ├── 软件/                     # eBPF + Go + Python 上位机
│   │   ├── bpf/
│   │   │   ├── common.h          #  共享类型定义
│   │   │   ├── loop_monitor.bpf.c # 控制周期抖动探针
│   │   │   ├── serial_monitor.bpf.c # 串口通信延迟探针
│   │   │   └── sched_monitor.bpf.c # 任务调度延迟探针
│   │   ├── cmd/collector/
│   │   │   ├── main.go           #  Go 采集器 + REST API
│   │   │   ├── go.mod
│   │   │   └── go.sum
│   │   ├── frontend/index.html   #  Dashboard
│   │   ├── ros2/                 #  Python 控制节点（含故障注入）
│   │   │   ├── robot_control.py  #   真实机器人控制
│   │   │   ├── demo_control.py   #   模拟控制（故障注入）
│   │   │   ├── check_com.py      #   串口自检
│   │   │   └── serial_bridge.py  #   串口桥接
│   │   ├── scripts/              #  实验与分析脚本
│   │   │   ├── exp_watchdog_vs_ebpf.py # eBPF vs watchdog 对比
│   │   │   ├── exp_fault_injection.py  # 故障注入实验
│   │   │   ├── exp_estop_50.py   #   50次 ESTOP 实验
│   │   │   ├── analyze_stm32_jitter.py # STM32 数据抖动分析
│   │   │   └── run_experiments.sh #  一键运行
│   │   ├── tests/                #  集成测试（35项）
│   │   ├── bin/                  #  编译好的二进制
│   │   └── Makefile
│   └── stm32mpu6050/             # STM32F103 下位机固件（完整 CubeMX 项目）
│       ├── Core/
│       │   ├── Inc/              #  头文件（含 tasks/ 子目录）
│       │   └── Src/              #  源文件
│       │       ├── main.c        #   主入口 + IWDG + FreeRTOS
│       │       ├── control_task.c #   Madgwick + PID 控制
│       │       ├── data_protocol.c #  CRC16 + 帧编解码
│       │       ├── motor_control.c #  电机 PWM + ESTOP
│       │       └── tasks/app_tasks.c # 四任务调度 + 命令解析
│       ├── Drivers/              #  HAL 库
│       ├── Middlewares/          #  FreeRTOS
│       ├── CMakeLists.txt
│       ├── flash_v2.py           #  烧录脚本
│       └── startup_stm32f103xb.s
└── 文档/                         # 项目文档
    ├── README.md                 #  ← 你正在看的文件
    └── docs/                     #  实验指南、论文思路、未来展望等
```

---

## 环境要求

| 组件 | 最低版本 | 说明 |
|------|----------|------|
| Linux 内核 | 5.8+ | eBPF CO-RE 支持，WSL2 自带 6.6+ |
| clang/LLVM | 12+ | 编译 eBPF C 程序 |
| libbpf | 1.0+ | `apt install libbpf-dev` |
| Go | 1.22+ | 编译采集器 |
| Python | 3.9+ | 运行模拟控制节点 |
| 浏览器 | 任意 | 打开 Dashboard |

**如果你用的是本项目的开发环境（WSL2 Ubuntu 24.04），以上已全部配好。**

---

## 快速开始（5 分钟跑通）

### 第一步：编译

```bash
cd ebpf-robot-safety/设计/软件

# 编译三个 eBPF 探针
make build-bpf

# 编译 Go 采集器
make build-go
```

### 第二步：启动监控器

```bash
sudo BPF_DIR=bpf ./bin/collector
```

看到以下输出表示成功：

```
[collector] loop_monitor active
[collector] serial_monitor active
[collector] sched_monitor active
[collector] Robot Safety Monitor running — API on :8090
```

### 第三步：启动模拟机器人

打开另一个终端：

```bash
cd ebpf-robot-safety/设计/软件
python3 ros2/demo_control.py --fault 5
```

`--fault 5` 表示每 5 秒注入一次控制周期抖动（模拟机器人受到干扰）。

### 第四步：打开仪表盘

浏览器访问 **http://localhost:8090**

你会看到：
- 顶部状态灯（NOMINAL / WARNING / CRITICAL）
- 四个指标卡片（控制抖动、串口停顿、调度延迟、告警数）
- 控制周期抖动实时曲线
- 安全告警事件列表

---

## 三个探针详解

### 探针 1：loop_monitor — 控制周期抖动

| 项目 | 内容 |
|------|------|
| Hook 点 | `kprobe/hrtimer_start` |
| 监控对象 | 高精度定时器的**实际触发间隔** vs **期望周期** |
| 告警阈值 | 抖动 > 500us → WARNING，抖动 > 2ms → CRITICAL |
| 为什么重要 | 控制周期不稳 → PID 输出不准 → 机器人抖动/失稳 |

**STM32 类比**：FreeRTOS 的 `vTaskGetRunTimeStats()` 检查控制任务是否按时执行。

### 探针 2：serial_monitor — 串口通信延迟

| 项目 | 内容 |
|------|------|
| Hook 点 | `kprobe/tty_write` + `kprobe/tty_read` |
| 监控对象 | Linux SBC ↔ STM32 的 UART/USB 串口通信 |
| 告警阈值 | 写间隔 < 100us → 缓冲区溢出风险，读间隔 > 100ms → 传感器停顿 |
| 为什么重要 | 串口断连 → 传感器数据停滞 → 控制环开环 → 机器人失控 |

**STM32 类比**：你固件里的 UART CRC16 校验 + ACK 确认 + 序列号检测。

### 探针 3：sched_monitor — 任务调度延迟

| 项目 | 内容 |
|------|------|
| Hook 点 | `tracepoint/sched/sched_switch` |
| 监控对象 | ROS2 控制节点被唤醒后**等待多久才能上 CPU** |
| 告警阈值 | 调度延迟 > 10ms → WARNING |
| 为什么重要 | CPU 被其他进程抢占 → ROS2 节点得不到执行 → 实时性破坏 |

**STM32 类比**：FreeRTOS 的 `uxTaskGetStackHighWaterMark()` + `eTaskGetState()`。

---

## 使用场景

### A. 纯软件模拟（无硬件）

适合开发和调试，不需要任何硬件。

```bash
# 终端1
sudo BPF_DIR=bpf ./bin/collector

# 终端2
python3 ros2/demo_control.py --fault 5

# 浏览器
http://localhost:8090
```

### B. 接真实 STM32（有硬件）

把你的 STM32 通过 UART 接到电脑。

```bash
# 终端1: 监控器不变
sudo BPF_DIR=bpf ./bin/collector

# 终端2: 真实控制
python3 ros2/demo_control.py --serial /dev/ttyS16

# 这时 serial_monitor 探针会捕获真实 UART 流量
# Dashboard 上 "Serial Stalls" 会从 0 变成实际数值
```

### C. 部署到真实机器人

如果机器人主控是 Linux 单板（树莓派/Jetson/NUC）：

```bash
# 1. 交叉编译（如果架构不同）或直接在板子上编译
# 2. 拷到机器人上
scp bin/collector robot@192.168.1.100:/opt/safety-monitor/
scp bpf/*.bpf.o robot@192.168.1.100:/opt/safety-monitor/bpf/
scp frontend/index.html robot@192.168.1.100:/opt/safety-monitor/frontend/

# 3. 在机器人上启动
ssh robot@192.168.1.100
cd /opt/safety-monitor
sudo BPF_DIR=bpf ./collector

# 4. 局域网内任意设备打开 http://192.168.1.100:8090
```

---

## API 接口

| 端点 | 说明 | 示例 |
|------|------|------|
| `GET /api/summary` | 全局安全状态摘要 | `curl localhost:8090/api/summary` |
| `GET /api/loop` | 控制周期事件历史 | `curl localhost:8090/api/loop` |
| `GET /api/serial` | 串口通信事件历史 | `curl localhost:8090/api/serial` |
| `GET /api/sched` | 调度延迟事件历史 | `curl localhost:8090/api/sched` |
| `GET /api/alerts` | 安全告警列表 | `curl localhost:8090/api/alerts` |
| `GET /` | 安全仪表盘 | 浏览器打开 |

### `/api/summary` 返回示例

```json
{
  "loop_warnings": 3,
  "loop_criticals": 1,
  "last_jitter_us": 850.0,
  "max_jitter_us": 7800.0,
  "serial_tx_bytes": 0,
  "serial_rx_bytes": 0,
  "serial_stalls": 0,
  "sched_events": 15420,
  "avg_wait_ms": 0.8,
  "max_wait_ms": 12.5,
  "robot_safety": "WARNING",
  "timestamp": 1779600000000
}
```

---

## 制造故障（测试告警）

### 制造控制周期抖动

```bash
python3 ros2/demo_control.py --fault 3   # 每3秒注入一次抖动
python3 ros2/demo_control.py --fault 10  # 每10秒注入一次
python3 ros2/demo_control.py --freq 200 --fault 2  # 200Hz循环 + 2秒抖动
```

### 制造串口干扰（需要硬件）

```bash
# 在 STM32 上故意停止发送数据
# 或者拔掉 UART 线
# Dashboard 会在 < 100ms 内检测到 serial_stall
```

### 制造 CPU 竞争

```bash
# 在机器人板子上跑一个 CPU 压力程序
stress --cpu 4 --timeout 30s
# sched_monitor 会检测到 ROS2 节点调度延迟飙升
```

---

## 论文贡献点

如果以此项目写论文，可突出的创新点：

1. **eBPF 首次应用于机器人实时控制安全** — 目前无人做，是空白领域
2. **三路探针协同** — 控制 + 通信 + 调度，覆盖机器人"实时性三角"
3. **非侵入式** — 零代码修改，纯内核态监控，< 1% CPU 开销
4. **CPS 安全视角** — 物理信息系统（Cyber-Physical System）的运行时安全保障
5. **STM32 + Linux 异构系统** — 同时监控嵌入式端（串口）和 Linux 端（内核）

---

## 故障排查

### BPF 加载失败

```bash
# 检查内核版本
uname -r  # 需要 5.8+

# 检查 BTF 支持
ls /sys/kernel/btf/vmlinux  # 存在 = 支持 CO-RE

# 重新编译
make clean && make build-bpf
```

### 探针没有数据

```bash
# 检查探针是否附着
sudo bpftool prog list | grep -E "hrtimer|tty|sched_switch"

# 检查 ring buffer 是否有事件
sudo bpftool map list | grep events
```

### 端口被占用

```bash
sudo fuser -k 8090/tcp
```

---

## 与作者其他项目的关系

| 项目 | 技术栈 | 提供的模块 |
|------|--------|-----------|
| **STM32-MPU6050** | C + FreeRTOS + 传感器 | 嵌入式控制 + Kalman 滤波经验 |
| **eBPF-net-obs** | C + Go + React | eBPF 探针架构 + Go 采集器 + Dashboard |
| **本项目（Robot Safety）** | 上面两个融合 | 控制周期 + 串口 + 调度 三路监控 |
