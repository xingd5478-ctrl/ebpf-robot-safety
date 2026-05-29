# eBPF 机器人安全监控系统 — 使用手册

> 邢栋 · 2026年5月  
> 适用环境：Ubuntu 24.04 (Kernel 5.15+)，物理机或树莓派

---

## 目录

1. [环境要求](#1-环境要求)
2. [快速开始（5分钟）](#2-快速开始5分钟)
3. [编译与构建](#3-编译与构建)
4. [运行系统](#4-运行系统)
5. [API 参考](#5-api-参考)
6. [实验脚本](#6-实验脚本)
7. [Dashboard 使用](#7-dashboard-使用)
8. [故障排查](#8-故障排查)
9. [项目文件地图](#9-项目文件地图)

---

## 1. 环境要求

### 硬件
| 组件 | 最低要求 | 推荐 |
|------|---------|------|
| SBC/PC | 任意 x86_64 Linux 机器 | 树莓派4B / Jetson Nano / 笔记本 |
| STM32 | STM32F103C8T6 (已烧录固件) | 四轮差速机器人底盘 |
| 串口线 | FT232RL USB-TTL | 460800 bps |
| 内存 | 512 MB | 2 GB+ |

### 软件
```bash
# 必需
Linux Kernel 5.8+          # eBPF CO-RE 支持
clang 14+                  # BPF 编译
Go 1.21+                   # collector 编译运行
Python 3.9+                # 控制节点 + 实验脚本

# 可选
bpftool                    # 调试 BPF 程序
curl                       # 测试 API
perf                       # 性能测量
```

### 确认 BPF 环境可用
```bash
# 1. 检查内核版本
uname -r
# 应显示 5.8 或更高

# 2. 检查 BTF 支持（CO-RE 需要）
ls /sys/kernel/btf/vmlinux
# 文件存在 = OK

# 3. 检查 tracepoint 可用（loop_monitor 需要）
ls /sys/kernel/debug/tracing/events/syscalls/ | grep nanosleep
# 应显示: sys_enter_nanosleep  sys_enter_clock_nanosleep
# 如果没有，说明 debugfs 未挂载或 WSL2 不支持
```

---

## 2. 快速开始（5分钟）

```bash
# 第一步：编译
cd ebpf-robot-safety/设计/软件
make build-bpf    # 编译 3 个 BPF 探针
make build-go     # 编译 Go collector

# 第二步：启动 collector（需要 sudo）
sudo BPF_DIR=bpf ./bin/collector
# 看到 "[collector] Robot Safety Monitor running — API on :8090" = 成功

# 第三步：另开终端，验证 API
curl http://localhost:8090/api/summary | python3 -m json.tool
# 应该返回 JSON，包含 loop_criticals、robot_safety 等字段

# 第四步：运行模拟控制节点
python3 ros2/demo_control.py --fault 3
# 模拟 100Hz 控制循环，每 3 秒注入一次抖动

# 第五步：打开 Dashboard
# 浏览器访问 http://localhost:8090
# 或在 frontend/index.html 直接打开
```

---

## 3. 编译与构建

### 3.1 BPF 探针编译

```bash
make build-bpf
```

这会用 clang 编译 `bpf/` 下的三个 `.bpf.c` 文件：
- `loop_monitor.bpf.o` — 控制周期抖动监测
- `serial_monitor.bpf.o` — 串口通信延迟监测
- `sched_monitor.bpf.o` — 任务调度延迟监测

如果编译失败：
```bash
# 检查 clang 版本
clang --version  # 需要 14+

# 检查内核头文件
ls /usr/include/linux/bpf.h
# 如果没有: sudo apt install linux-headers-$(uname -r)

# 手动编译单个探针
clang -O2 -g -Wall -target bpf -D__TARGET_ARCH_x86 \
  -I/usr/include -I./bpf \
  -c bpf/loop_monitor.bpf.c -o bpf/loop_monitor.bpf.o
```

### 3.2 Go Collector 编译

```bash
make build-go
```

这会编译 `cmd/collector/main.go` → `bin/collector`。

如果编译失败：
```bash
# 检查 Go 版本
go version  # 需要 1.21+

# 手动编译
cd cmd/collector
CGO_ENABLED=0 go build -buildvcs=false -o ../../bin/collector .
cd ../..
```

### 3.3 全部编译

```bash
make all    # = make build-bpf + make build-go
```

---

## 4. 运行系统

### 4.1 启动 Go Collector（核心服务）

```bash
# 基础启动
sudo BPF_DIR=bpf ./bin/collector

# 后台运行 + 日志
sudo BPF_DIR=bpf ./bin/collector > /tmp/collector.log 2>&1 &

# 查看日志
tail -f /tmp/collector.log
```

启动成功的标志：
```
[collector] loop_monitor active
[collector] PID registration enabled
[collector] serial_monitor active
[collector] sched_monitor active
[collector] Robot Safety Monitor running — API on :8090
```

### 4.2 运行模拟控制节点（无硬件测试）

```bash
# 正常模式（100Hz，无故障）
python3 ros2/demo_control.py

# 故障注入模式（每 3 秒注入 3-8ms 延迟）
python3 ros2/demo_control.py --fault 3

# 故障注入模式（每 5 秒注入）
python3 ros2/demo_control.py --fault 5
```

注册 PID 让 eBPF 监控此进程：
```bash
# 另开终端
DPID=$(pgrep -f demo_control.py)
curl -s -X POST http://localhost:8090/api/monitor_pid \
  -H "Content-Type: application/json" \
  -d "{\"pid\":$DPID}"
# 返回 {"status":"ok"} = 注册成功
```

### 4.3 连接真实 STM32 机器人

```bash
# 1. 确认串口
ls /dev/ttyUSB*    # 找到 STM32 连接的串口

# 2. 启动 collector（如未启动）
sudo BPF_DIR=bpf ./bin/collector &
sleep 3

# 3. 启动控制节点
python3 ros2/robot_control.py --port /dev/ttyUSB0

# 4. 注册 PID
PID=$(pgrep -f robot_control.py)
curl -s -X POST http://localhost:8090/api/monitor_pid \
  -H "Content-Type: application/json" \
  -d "{\"pid\":$PID}"

# 5. 发送控制命令
python3 ros2/check_com.py   # 检查串口数据
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB0', 460800)
s.write(b'FWD 400\n')
time.sleep(2)
s.write(b'STOP\n')
"
```

---

## 5. API 参考

Collector 运行在 `http://localhost:8090`，提供以下端点：

| 端点 | 方法 | 用途 | 示例 |
|------|:--:|------|------|
| `/api/summary` | GET | 系统总览（状态、抖动、告警数） | `curl localhost:8090/api/summary` |
| `/api/loop` | GET | 原始 loop 事件列表 | `curl localhost:8090/api/loop` |
| `/api/serial` | GET | 串口监测数据 | `curl localhost:8090/api/serial` |
| `/api/sched` | GET | 调度延迟事件 | `curl localhost:8090/api/sched` |
| `/api/alerts` | GET | 告警事件列表 | `curl localhost:8090/api/alerts` |
| `/api/monitor_pid` | POST | 注册 PID 到 eBPF 监控白名单 | `curl -X POST ... -d '{"pid":1234}'` |
| `/api/command` | POST | 发送运动命令 | `curl -X POST ... -d '{"cmd":"ESTOP"}'` |
| `/api/safety_command` | GET | 读取最新安全命令（无阻塞） | `curl localhost:8090/api/safety_command` |
| `/api/robot_telemetry` | POST | 上报机器人遥测数据 | `curl -X POST ... -d '{...}'` |

### /api/summary 返回字段说明

```json
{
  "robot_safety": "NOMINAL",     // NOMINAL / WARNING / CRITICAL
  "loop_warnings": 0,            // WARNING 级别事件累计
  "loop_criticals": 0,           // CRITICAL 级别事件累计
  "last_jitter_us": 123.45,      // 最近一次抖动值（微秒）
  "max_jitter_us": 250.0,        // 历史最大抖动
  "serial_stalls": 0,            // 串口停顿累计
  "serial_rx_bytes": 0,          // 串口接收字节数
  "serial_tx_bytes": 0,          // 串口发送字节数
  "sched_events": 0,             // 调度延迟事件数
  "avg_wait_ms": 0,              // 平均调度等待（ms）
  "timestamp": 1779768145466     // 时间戳
}
```

### 发送运动命令

```bash
# 基本运动
curl -X POST http://localhost:8090/api/command \
  -H "Content-Type: application/json" \
  -d '{"cmd":"FWD 400"}'

# 可用命令
STOP            # 停止
ESTOP           # 紧急停止（PWM 置零，锁定）
FWD  <0-999>    # 前进
BACK <0-999>    # 后退
LEFT <0-999>    # 左转
RIGHT <0-999>   # 右转
VEL  <lin> <ang># 速度控制
HEAD <deg>      # 航向保持
```

---

## 6. 实验脚本

所有脚本在 `scripts/` 目录下，从项目根目录运行。

### 6.1 故障注入实验
```bash
# 基本故障注入（3 组对比）
python3 scripts/exp_fault_injection.py

# 多轮试验
python3 scripts/exp_multi_trial.py
```

### 6.2 ESTOP 延迟测量
```bash
# 单次实验
python3 scripts/exp_estop_latency.py

# 50 次独立实验
python3 scripts/exp_estop_50.py
```

### 6.3 Watchdog 对比实验
```bash
# eBPF vs 应用层 watchdog（需要 collector 运行中）
python3 scripts/exp_watchdog_vs_ebpf.py --fault 3 --trials 30
```

### 6.4 STM32 数据抖动分析
```bash
# 从 STM32 项目 CSV 中提取控制周期抖动
python3 scripts/analyze_stm32_jitter.py
```

### 6.5 一键运行全部实验
```bash
bash scripts/run_experiments.sh
```

---

## 7. Dashboard 使用

### 启动
- Collector 启动后，Dashboard 自动服务在 `http://localhost:8090`
- 也可以直接打开 `frontend/index.html`（纯静态 HTML）

### 界面说明
- **安全状态指示灯**：绿色 NOMINAL / 黄色 WARNING / 红色 CRITICAL（脉冲动画）
- **指标卡片**：控制抖动、串口停顿、调度延迟、活跃告警
- **实时抖动曲线**：ECharts 折线图，标注 WARNING(500μs) 和 CRITICAL(2000μs) 阈值线
- **告警事件列表**：最近 20 条告警（类型、级别、数值、时间）
- **控制面板**：FWD / LEFT / STOP / RIGHT / BACK / ESTOP 六按钮

### 注意
- Dashboard 每 2 秒轮询一次 API
- 如果页面空白，检查 collector 是否运行：`curl localhost:8090/api/summary`

---

## 8. 故障排查

### 8.1 Collector 启动失败

```bash
# 检查端口占用
sudo lsof -i :8090
# 如果有旧进程：sudo kill <PID>

# 检查 BPF 文件
ls -la bpf/*.bpf.o
# 如果缺失：make build-bpf

# 检查权限
sudo BPF_DIR=bpf ./bin/collector
# BPF 程序加载需要 root 权限
```

### 8.2 BPF 探针加载失败

```bash
# 检查内核版本
uname -r

# 检查 tracepoint 是否存在
ls /sys/kernel/debug/tracing/events/syscalls/sys_enter_nanosleep

# 如果 tracepoint 不存在（如 WSL2）：
# - loop_monitor 将无法工作（改用物理 Ubuntu）
# - sched_monitor 可能仍可用（scheduler tracepoints 通常存在）
# - serial_monitor 在 WSL2 也不可用

# 查看 collector 日志
grep "warn\|error\|fail" /tmp/collector.log
```

### 8.3 serial_monitor 不触发（Kernel 6.17+）

Kernel 6.17 的 TTY 子系统重构后，kprobe/tty_write 不再被 USB-serial 驱动触发。
- **症状**：bpftool 显示 kprobe 挂载成功，但 tty_map 为空
- **解决**：使用 Kernel 5.15/6.1/6.6 LTS 版本，或改用 fentry 挂载点

```bash
# 检查当前内核
uname -r
# 如果是 6.17+：已知兼容性问题，需后续适配
```

### 8.4 Python 控制节点连接不上 STM32

```bash
# 检查串口设备
ls /dev/ttyUSB* /dev/ttyACM*

# 检查权限
sudo chmod 666 /dev/ttyUSB0
# 或把用户加入 dialout 组
sudo usermod -a -G dialout $USER
# （需要重新登录）

# 测试串口
python3 -c "
import serial
s = serial.Serial('/dev/ttyUSB0', 460800, timeout=1)
data = s.read(32)
print(f'Received {len(data)} bytes: {data.hex()}')
"
```

### 8.5 Dashboard 连不上 API

```bash
# 检查 collector 是否运行
curl http://localhost:8090/api/summary

# 如果连接被拒绝：collector 未启动
# 如果超时：检查防火墙
sudo ufw status
sudo ufw allow 8090
```

---

## 9. 项目文件地图

```
ebpf-robot-safety/
├── 设计/软件/                    ← 软件系统（工作目录）
│   ├── Makefile                  #    一键编译
│   ├── bpf/                      ←   eBPF 内核探针（C）
│   │   ├── common.h              #      共享类型定义
│   │   ├── loop_monitor.bpf.c    #      控制周期抖动
│   │   ├── serial_monitor.bpf.c  #      串口通信延迟
│   │   └── sched_monitor.bpf.c   #      任务调度延迟
│   ├── cmd/collector/            ←   Go collector
│   │   └── main.go               #      BPF加载 + Ring Buffer消费 + REST API
│   ├── bin/collector             ←   编译好的可执行文件
│   ├── ros2/                     ←   Python 控制节点
│   │   ├── robot_control.py      #      真实机器人（连接 STM32）
│   │   ├── demo_control.py       #      模拟节点（无硬件测试）
│   │   ├── serial_bridge.py      #      串口桥接
│   │   └── check_com.py          #      串口自检
│   ├── scripts/                  ←   实验与分析脚本
│   │   ├── exp_watchdog_vs_ebpf.py #    watchdog 对比实验 ★
│   │   ├── analyze_stm32_jitter.py #    STM32 数据抖动分析 ★
│   │   ├── exp_fault_injection.py  #    故障注入实验
│   │   ├── exp_estop_50.py         #    50次 ESTOP 实验
│   │   ├── exp_multi_trial.py      #    多轮试验
│   │   └── run_experiments.sh      #    一键运行 ★
│   ├── frontend/index.html       ←   React Dashboard
│   └── tests/test_integration.py ←   集成测试（35项）
│
├── 论文/                         ← 论文产出
│   ├── paper/                    ←   期刊论文
│   │   ├── journal_paper.tex     #      LaTeX 源文件
│   │   ├── journal_paper.pdf     #      编译好的 PDF
│   │   └── figures/              #      图表
│   └── thesis/                   ←   毕业论文
│       ├── document.tex
│       ├── document.pdf
│       └── figures/
│
├── 文档/                         ← 项目文档
│   ├── README.md                 ←   项目总览
│   └── docs/
│       ├── eBPF 机器人安全监控系统 — 使用手册.md ← 你正在看的这个
│       ├── eBPF 实验方案.md                     ← 补实验方案
│       ├── 论文思路与实验总结.md                 ← 研究历程
│       ├── eBPF机器人安全监控系统 —— 未来展望：技术演进与研究方向.md ← 后续方向
│       ├── Ubuntu 原生系统实验指南.md            ← 物理机实验步骤
│       └── ubuntu_experiments.md                ← Ubuntu 实验记录
│
└── 设计/stm32mpu6050/            ← STM32 下位机固件
```

---

## 附录：常用命令速查

```bash
# ===== 构建 =====
make all                         # 编译一切
make build-bpf                   # 只编译 BPF
make build-go                    # 只编译 Go
make clean                       # 清理

# ===== 运行 =====
sudo BPF_DIR=bpf ./bin/collector # 启动 collector
python3 ros2/demo_control.py     # 模拟控制节点
python3 ros2/demo_control.py --fault 3  # 带故障注入

# ===== 监控 =====
curl localhost:8090/api/summary  # 查看状态
curl localhost:8090/api/alerts   # 查看告警
curl localhost:8090/api/loop     # 查看 loop 事件

# ===== 实验 =====
python3 scripts/exp_watchdog_vs_ebpf.py --fault 3 --trials 30
python3 scripts/exp_estop_50.py
python3 scripts/analyze_stm32_jitter.py

# ===== 调试 =====
sudo bpftool prog list           # 查看已加载 BPF 程序
sudo bpftool map list            # 查看 BPF maps
sudo bpftool map dump name loop_events  # 导出 ring buffer
tail -f /tmp/collector.log       # 查看 collector 日志

# ===== 论文 =====
cd paper && xelatex journal_paper.tex  # 编译期刊论文
cd thesis && xelatex document.tex       # 编译毕业论文
```
