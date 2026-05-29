<p align="center">
  <img src="https://img.shields.io/badge/eBPF-Linux_Kernel-FF9900?style=flat&logo=linux&logoColor=white" alt="eBPF">
  <img src="https://img.shields.io/badge/Go-1.22+-00ADD8?style=flat&logo=go&logoColor=white" alt="Go">
  <img src="https://img.shields.io/badge/Python-3.9+-3776AB?style=flat&logo=python&logoColor=white" alt="Python">
  <img src="https://img.shields.io/badge/ROS2-Humble-22314E?style=flat&logo=ros&logoColor=white" alt="ROS2">
  <img src="https://img.shields.io/badge/STM32-F103-blue?style=flat&logo=stmicroelectronics" alt="STM32">
  <img src="https://img.shields.io/badge/Overhead-<1%25_CPU-success?style=flat" alt="Performance">
</p>

<h1 align="center">基于 eBPF 的机器人实时控制<br>安全监控系统</h1>

<p align="center"><strong>在 Linux 内核里埋三个探针，无侵入地守护机器人的每一次控制循环</strong><br>
邢栋 · 天津中德应用技术大学 机械工程学院 · 2026</p>

---

## 项目概述

机器人控制系统的**实时性失效**是导致安全事故的主要根源——控制周期抖动、串口通信中断、CPU 调度延迟三者构成了"实时性三角"。传统方案（硬件 Watchdog、应用层心跳）要么粒度太粗，要么具有侵入性。

本项目利用 **eBPF（Extended Berkeley Packet Filter）** 技术，在 Linux 内核中部署三个轻量级探针，实现对机器人控制循环的**非侵入式、微秒级、全覆盖**安全监控。

---

## 核心创新

<table>
<tr>
<td width="50%">

### 三个内核探针

| 探针 | Hook 点 | 监控对象 | 告警阈值 |
|:---|:---|:---|:---|
| `loop_monitor` | `kprobe/hrtimer_start` | 控制周期抖动 | >500μs WARNING<br>>2ms CRITICAL |
| `serial_monitor` | `kprobe/tty_write`<br>`kprobe/tty_read` | 串口通信延迟 | 读间隔>100ms 停顿 |
| `sched_monitor` | `tracepoint/sched_switch` | ROS2 调度延迟 | >10ms WARNING |

</td>
<td width="50%">

### 关键指标

- **检测延迟** < 100ms（eBPF vs Watchdog 200ms+）
- **CPU 开销** < 1%（内核态零拷贝 ring buffer）
- **非侵入** — 零行应用代码修改
- **部署** — 单二进制 `collector`，无外部依赖
- **架构** — Go 采集器 + REST API + Web Dashboard

</td>
</tr>
</table>

---

## 系统架构

```
┌──────────────────────────────────────────────────────────────┐
│                    Linux 单板（树莓派 / Jetson / NUC）         │
│                                                              │
│   ┌───────────── Kernel Space ─────────────┐                 │
│   │                                         │                 │
│   │  ┌──────────┐ ┌──────────┐ ┌──────────┐│                 │
│   │  │loop_monitor│ │serial_mon│ │sched_mon ││  ← eBPF 探针  │
│   │  │ hrtimer   │ │ tty_r/w  │ │sched_sw  ││                 │
│   │  └─────┬─────┘ └────┬─────┘ └────┬─────┘│                 │
│   │        │             │            │      │                 │
│   │        └─────────┬───┴────────────┘      │                 │
│   │                  │ Ring Buffer            │                 │
│   └──────────────────┼───────────────────────┘                 │
│                      │                                         │
│   ┌────────────────── User Space ─────────────┐                │
│   │                                           │                │
│   │   Go Collector (libbpf + REST :8090)       │                │
│   │        │          │          │             │                │
│   │   ┌────┴───┐ ┌───┴───┐ ┌───┴──────┐      │                │
│   │   │Web 仪表盘│ │REST API│ │告警推送  │      │                │
│   │   └────────┘ └───────┘ └──────────┘      │                │
│   └───────────────────────────────────────────┘                │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                    STM32F103（嵌入式 MCU）                        │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│   │ FreeRTOS  │  │ MPU6050  │  │ PID 控制  │  │ IWDG     │      │
│   │ 4 任务    │  │ 100Hz    │  │ Madgwick  │  │ 硬件看门狗│      │
│   └──────────┘  └──────────┘  └──────────┘  └──────────┘      │
└──────────────────────────────────────────────────────────────────┘
```

---

## 探针详解

### 1. loop_monitor — 控制周期抖动

Hook 到 `hrtimer_start`，捕获高精度定时器的实际触发间隔与期望周期的偏差。

> 控制周期不稳 → PID 输出不准 → 机器人抖动/失稳

### 2. serial_monitor — 串口通信安全

Hook 到 `tty_write` / `tty_read`，监控 Linux SBC ↔ STM32 的 UART 通信健康度。

> 串口断连 → 传感器数据停滞 → 控制环开环 → 失控

### 3. sched_monitor — 任务调度延迟

Hook 到 `sched_switch` tracepoint，监控 ROS2 控制节点被唤醒后等待 CPU 的时间。

> CPU 被抢占 → ROS2 节点得不到执行 → 实时性破坏

---

## 快速开始（5 分钟）

```bash
# 1. 编译探针 + 采集器
cd 设计/软件
make build-bpf
make build-go

# 2. 启动安全监控器（需要 root 权限加载 eBPF）
sudo BPF_DIR=bpf ./bin/collector
# 输出: [collector] Robot Safety Monitor running — API on :8090

# 3. 启动模拟机器人（含故障注入）
python3 ros2/demo_control.py --fault 5

# 4. 打开仪表盘
# 浏览器访问 http://localhost:8090
```

---

## 实验验证

| 实验 | 内容 | 关键发现 |
|:---|:---|:---|
| 故障注入 | 控制周期抖动检测 | eBPF 检测延迟 < 100ms，优于 Watchdog 4.8× |
| ESTOP 延迟 | 50 次紧急停止测试 | HTTP ESTOP 平均延迟可控 |
| eBPF vs Watchdog | 对比实验 | eBPF 检测粒度 μs 级，Watchdog ms 级 |
| CPU 开销 | perf 性能测试 | eBPF 运行时 < 1% CPU 开销 |
| 真实机器人 | 完整系统测试 | 三路探针协同工作正常 |

---

## API 接口

| 端点 | 说明 |
|:---|:---|
| `GET /api/summary` | 全局安全状态（NOMINAL / WARNING / CRITICAL） |
| `GET /api/loop` | 控制周期抖动事件历史 |
| `GET /api/serial` | 串口通信事件历史 |
| `GET /api/sched` | 调度延迟事件历史 |
| `GET /api/alerts` | 安全告警列表 |
| `GET /` | 实时安全仪表盘 |

---

## 技术栈

| 层 | 技术 |
|:---|:---|
| 内核探针 | C (eBPF) + libbpf + CO-RE + clang/LLVM |
| 数据采集 | Go + cilium/ebpf + net/http |
| 前端 | HTML5 + Chart.js（实时波形） |
| 控制节点 | Python 3 + ROS2 |
| 嵌入式 | C + FreeRTOS + STM32 HAL + IWDG |

---

<p align="center"><sub>本项目为大疆创新实习申请作品附件 · DJI Internship Portfolio</sub></p>
