# 真实机器人 eBPF 实验方案

> 目标：半天补完，生成投软件学报所需的物理实验数据
> 日期：2026年6月
> 负责人：邢栋

---

## 一、实验目的

在真实四轮差速机器人上运行 eBPF 探针，完成以下关键验证：

| 编号 | 目的 | 对应论文缺陷 |
|:--:|------|------|
| A | 证明 eBPF loop_monitor 在真实机器人 SBC 上能正常工作 | "eBPF从未在机器人上跑过" |
| B | 在真实机器人运动中注入故障，测量 eBPF 检出率和延迟 | "所有实验在桌面完成" |
| C | 测量真实电机停转的 ESTOP 端到端延迟 | "ESTOP延迟只测了HTTP往返" |
| D | 拍摄实物照片用于论文 | "缺少实验现场照片" |

---

## 二、硬件清单

| 物品 | 数量 | 备注 |
|------|:--:|------|
| 四轮差速机器人 | 1 | STM32F103C8T6 + MPU6050 + AT8236，已烧录固件 |
| 机器人电池 | 1 | 满电 |
| Linux SBC | 1 | 树莓派4B 或 Jetson Nano 或 笔记本电脑(装Ubuntu 24.04) |
| USB-TTL 串口线 | 1 | FT232RL，连接 SBC 和 STM32 |
| 网线/WiFi | 1 | SBC 联网 |
| 手机（高速摄像模式） | 1 | 240fps 慢动作，拍车轮停转 |
| 卷尺 | 1 | 测量机器人行驶距离 |
| 笔记本电脑 | 1 | SSH 到 SBC，运行命令 |

**可选（提升精度）：**
| 物品 | 用途 |
|------|------|
| 示波器/逻辑分析仪 | 精确测量 ESTOP 的 GPIO 翻转延迟 |
| 杜邦线 | STM32 GPIO 引出到示波器探头 |

---

## 三、软件准备（提前做好，到现场只需运行）

### 3.1 SBC 上确认安装

```bash
# 检查 BPF 支持
uname -r                    # 应该是 5.8+ 或 6.x
ls /sys/kernel/btf/vmlinux  # 确认 BTF 可用

# 检查工具链
which clang go python3 curl

# 检查 tracepoint
ls /sys/kernel/debug/tracing/events/syscalls/ | grep nanosleep
# 应有: sys_enter_nanosleep, sys_enter_clock_nanosleep
```

### 3.2 确认串口通信

```bash
# 插上 USB-TTL 后检查
ls /dev/ttyUSB*             # 确认设备出现
# 测试串口
python3 -c "import serial; s=serial.Serial('/dev/ttyUSB0', 460800); print(s.read(32))"
```

### 3.3 编译并启动 Go collector

```bash
cd ~/ebpf-robot-safety
make build-bpf && make build-go
sudo BPF_DIR=bpf ./bin/collector &
# 确认 API 可用
curl http://localhost:8090/api/summary
```

### 3.4 确认 robot_control.py 可用

```bash
cd ~/ebpf-robot-safety/ros2
python3 robot_control.py --port /dev/ttyUSB0
# 应看到遥测数据（0xBADD 帧）
```

---

## 四、实验步骤

---

### 实验 A：正常工况基线（30 分钟）

**目的：** 采集真实机器人在正常运动时的 eBPF 三路探针数据，建立基线。

**操作：**

```bash
# 1. 启动 Go collector（记录日志）
sudo BPF_DIR=bpf ./bin/collector > /tmp/col_A.log 2>&1 &
COLLECTOR_PID=$!

# 2. 启动 Python 控制节点 + API 数据采集
python3 ros2/robot_control.py --port /dev/ttyUSB0 &
CONTROL_PID=$!

# 3. 注册 PID
curl -s -X POST http://localhost:8090/api/monitor_pid \
  -H "Content-Type: application/json" -d "{\"pid\":$CONTROL_PID}"

# 4. 每 2 秒快照一次 API，记录到 CSV
for i in $(seq 1 300); do  # 10 分钟
  curl -s http://localhost:8090/api/summary | \
    python3 -c "import json,sys,time; d=json.load(sys.stdin); \
    print(f\"{time.time()},{d.get('loop_warnings',0)},{d.get('loop_criticals',0)},{d.get('max_jitter_us',0)},{d.get('robot_safety','')},{d.get('serial_stalls',0)}\")" \
    >> /tmp/exp_A_baseline.csv
  sleep 2
done
```

**运动序列（用遥控/串口命令控制）：**

| 时间段 | 机器人动作 | 命令 |
|--------|---------|------|
| 0-2min | 静止（采集空闲基线） | STOP |
| 2-4min | 直线行驶 | FWD 400 |
| 4-6min | S形机动 | FWD 400 + LEFT 200（每秒交替） |
| 6-8min | 直线行驶 | FWD 600 |
| 8-10min | 停止 | STOP |

**采集数据：**

| 文件 | 内容 |
|------|------|
| `/tmp/col_A.log` | collector 完整日志 |
| `/tmp/exp_A_baseline.csv` | 每秒 API 快照（300行） |
| `照片_A1.jpg` | 机器人+SBC整体照片 |
| `照片_A2.jpg` | Dashboard 截图（正常工况，绿色NOMINAL） |

**成功标准：**
- 所有 API 快照正常返回
- 正常工况下 CRITICAL=0（无严重抖动）
- loop_criticals 在整个测试期间 < 3

---

### 实验 B：故障注入检测（1 小时）

**目的：** 在真实机器人运动中注入故障，测量 eBPF 检测性能。

**故障注入方法：**

| 方法 | 操作 | 模拟场景 |
|------|------|---------|
| **B1-Python延迟** | 在 robot_control.py 中 `time.sleep(5ms)` 随机注入 | 控制进程被阻塞 |
| **B2-CPU压力** | `stress --cpu 4 --timeout 10` | CPU资源竞争 |
| **B3-串口断开** | 物理拔出 USB-TTL，3秒后重插 | 通信链路中断 |

**操作（B1：Python 延迟注入）：**

```bash
# 修改 robot_control.py 加入故障注入
cp robot_control.py robot_control_fault.py
# 在主循环中加入 (每 5 秒触发一次):
#   if self.cycle_count % 500 == 0:
#       t_fault = time.monotonic_ns()
#       print(f"[FAULT] {t_fault}")
#       time.sleep(random.uniform(0.003, 0.008))

python3 robot_control_fault.py --port /dev/ttyUSB0 &
PID=$!
curl -s -X POST http://localhost:8090/api/monitor_pid \
  -H "Content-Type: application/json" -d "{\"pid\":$PID}"

# 机器人直线行驶 5 分钟
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB0', 460800)
s.write(b'FWD 400\n')
time.sleep(300)
s.write(b'STOP\n')
"
```

**操作（B2：CPU 压力）：**

```bash
# 记录基线
curl -s http://localhost:8090/api/summary > /tmp/before_stress.json

# 施加 CPU 压力
stress --cpu 4 --timeout 10 &
STRESS_PID=$!

# 持续记录 API
for i in $(seq 1 20); do
  curl -s http://localhost:8090/api/summary >> /tmp/stress_api.log
  sleep 1
done

# 记录压力后
curl -s http://localhost:8090/api/summary > /tmp/after_stress.json
```

**操作（B3：串口断开）：**

```bash
# 记录当前 serial_stalls
curl -s http://localhost:8090/api/summary | python3 -c "import json,sys; print(json.load(sys.stdin)['serial_stalls'])"

# 物理拔出 USB 线
echo "拔出 USB 线，等待 5 秒"
sleep 5

# 重新插入
echo "重新插入 USB 线"

# 检查 serial_stalls 增加
curl -s http://localhost:8090/api/summary | python3 -c "import json,sys; print(json.load(sys.stdin)['serial_stalls'])"
```

**每种子实验重复次数：**

| 子实验 | 重复次数 | 机器人状态 | 耗时 |
|--------|:--:|------|:--:|
| B1-Python延迟 | 10 次 | 直线行驶 | 15min |
| B2-CPU压力 | 5 次 | 直线行驶 | 10min |
| B3-串口断开 | 5 次 | 静止 | 5min |

**采集数据：**

| 文件 | 内容 |
|------|------|
| 每次 B1 实验的 fault 时间戳 + eBPF alert 时间戳 | 检测延迟原始数据 |
| `/tmp/exp_B_results.csv` | 汇总表 |
| Dashboard 截图 | CRITICAL 状态（红色告警） |

**成功标准：**
- B1：eBPF 100% 检出 Python 注入的延迟
- B2：CPU 压力期间 loop_criticals 增加
- B3：serial_monitor 检测到串口断开

---

### 实验 C：ESTOP 物理闭环延迟（30 分钟）

**目的：** 测量从 eBPF 检测异常到真实电机停转的端到端延迟。

**操作步骤：**

1. 机器人置于空旷地面，车轮离地或在地上划一条起始线
2. 手机架设好，240fps 慢动作模式对准车轮
3. 启动 robot_control.py（FWD 400，直线行驶）
4. 手动触发 ESTOP：
   ```bash
   curl -s -X POST http://localhost:8090/api/command \
     -H "Content-Type: application/json" -d '{"cmd":"ESTOP"}'
   # 脚本自动记录 POST 的时间戳 T_send
   ```
5. 手机拍到车轮完全停转
6. 分析视频：从 T_send 到车轮停转的帧数 → 延迟

**精确方法（如果有示波器）：**

1. STM32 固件中 GPIO PB12 在收到 ESTOP 时拉低
2. 在 SBC 发送 ESTOP POST 的同时，Python 触发另一个 GPIO 翻转
3. 示波器测两个 GPIO 的时间差 = 端到端 ESTOP 延迟

**重复 20 次**

**采集数据：**

| 文件 | 内容 |
|------|------|
| `/tmp/exp_C_latency.csv` | 每次实验的时间戳和延迟 |
| `照片_C1.jpg` | 实验场景（机器人在跑，手机在拍） |
| `慢动作_C1.mp4` | 关键一次实验的慢动作视频 |

**成功标准：**
- 20 次全部成功（电机停转）
- ESTOP 延迟均值 < 10ms
- 与桌面测量的 3.20ms 形成对比

---

### 实验 D：拍照存档（穿插进行）

| 照片内容 | 用途 |
|------|------|
| 机器人 + SBC + 连线整体照片 | 论文 Fig.1 系统实物图 |
| Dashboard 截图（正常/告警/紧急三态） | 论文 Fig.X 监控界面 |
| 示波器波形（如有） | 论文 Fig.Y ESTOP延迟波形 |
| 实验场景（机器人在跑，旁边电脑屏幕上Dashboard） | 答辩PPT |

---

## 五、数据采集表格模板

### 实验 B 数据表

| 序号 | 故障类型 | 注入时间(T0) | eBPF检测时间(T1) | 延迟(T1-T0)/ms | 机器人状态 |
|:--:|------|------|------|------|------|
| 1 | B1-延迟 | | | | 直线 |
| 2 | B1-延迟 | | | | 直线 |
| ... | ... | ... | ... | ... | ... |

### 实验 C 数据表

| 序号 | ESTOP发送时间(T_send) | 视频帧数(发送→停转) | 延迟/ms | 备注 |
|:--:|------|:--:|------|------|
| 1 | | | | |
| 2 | | | | |
| ... | ... | ... | ... | ... |

---

## 六、时间安排（半天）

| 时间 | 内容 | 累计 |
|------|------|:--:|
| 09:00-09:30 | 环境搭建（连线、确认串口、启动collector） | 30min |
| 09:30-10:00 | **实验 A：正常工况基线** | 1h |
| 10:00-11:00 | **实验 B：故障注入检测**（B1+B2+B3） | 2h |
| 11:00-11:30 | **实验 C：ESTOP 闭环延迟** | 2.5h |
| 11:30-12:00 | 实验 D：拍照 + 数据初筛 | 3h |
| 12:00-12:30 | 补漏 + 复测可疑数据 | 3.5h |
| **12:30** | **收工，还机器人** | |

---

## 七、实验完成后的论文插入位置

| 实验数据 | 插入论文位置 | 替换/新增 |
|------|------|------|
| 实验A 正常基线数据 | §3.2 实验一 | 替换 WSL2 数据为真实 SBC 数据 |
| 实验B 故障注入结果 | §3.5 实验五 | 替换桌面 watchdog 对比数据 |
| 实验C ESTOP延迟 | §3.3 实验三 | 增强表3，加入电机停转环节 |
| 机器人实物照片 | §2.1 或 §3.1 | 新增架构/实验环境图 |
| Dashboard截图 | §2.2 | 新增监控界面图 |

---

## 八、备选方案

如果 SBC（树莓派/Jetson）不可用，可以用**笔记本电脑装 Ubuntu 24.04**替代：
- 笔记本通过 USB-TTL 连接 STM32
- 运行 Go collector + Python 控制节点
- 机器人在桌面上/地面上运动（车轮空转或短距离行驶）
- 对 eBPF 探针来说，SBC 和笔记本没有区别——都是 Linux 内核

---

## 九、快速检查清单

- [ ] STM32 固件已烧录（含 ESTOP 响应）
- [ ] 机器人在此之前至少成功运行过一次（确认电池、电机、串口正常）
- [ ] SBC/笔记本上 Go collector 编译通过
- [ ] SBC/笔记本上 robot_control.py 能连接 STM32
- [ ] `/sys/kernel/debug/tracing/events/syscalls/` 下有 nanosleep 相关 tracepoint
- [ ] 手机充满电（拍慢动作）
- [ ] 卷尺（测量行驶距离）
- [ ] USB-TTL 线+备用的（容易坏）
- [ ] 提前在机器人旁放好 SBC/笔记本的电源插座
