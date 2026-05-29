# Ubuntu 物理机实验指南

## 需要拷贝到 Ubuntu 的文件

将 Windows 上 `C:\Users\xing2\Desktop\ebpf-robot-safety` 整个文件夹拷到 Ubuntu：

```bash
# 方法1：U盘拷贝
# 把 ebpf-robot-safety 文件夹拷到 U 盘，再拷到 Ubuntu ~/ 下

# 方法2：直接从 Windows 分区读取
# Ubuntu 可以挂载 Windows NTFS 分区，直接 cp
sudo mkdir -p /mnt/windows
sudo mount /dev/nvme0n1p3 /mnt/windows  # 根据实际分区调整
cp -r "/mnt/windows/Users/xing2/Desktop/ebpf-robot-safety" ~/
```

**最少需要拷贝的内容（在 设计/软件/ 目录下）：**
```
设计/软件/
├── bpf/                    # 3个 eBPF 探针 + common.h
├── cmd/collector/          # Go 采集器源码
├── ros2/demo_control.py    # 故障注入脚本
├── ros2/robot_control.py   # 真实控制节点
├── frontend/index.html     # Dashboard
├── scripts/                # 实验脚本
├── Makefile
└── bin/collector           # 已编译的二进制（需重新编译）
```

---

## 第一步：安装依赖

```bash
# 更新包管理器
sudo apt update

# Go 1.23+
sudo apt install -y golang-go

# clang + libbpf（编译 eBPF）
sudo apt install -y clang llvm libbpf-dev

# Python 依赖
sudo apt install -y python3-pip
pip3 install pyserial

# bpftool（查看探针状态）
sudo apt install -y linux-tools-common linux-tools-generic

# 验证
uname -r           # 应 >= 5.8
clang --version    # 应 >= 12
go version         # 应 >= 1.22
ls /sys/kernel/btf/vmlinux  # 存在 = CO-RE 支持
```

---

## 第二步：编译

```bash
cd ~/ebpf-robot-safety

# 编译 eBPF 探针
make build-bpf

# 编译 Go 采集器
export GOPROXY=https://goproxy.cn,direct
cd cmd/collector
CGO_ENABLED=0 go build -buildvcs=false -o ../../bin/collector .
cd ../..
```

---

## 第三步：实验一 —— NOMINAL 状态验证

**目的**：证明在物理 Linux 上系统正常运行时为绿色 NOMINAL。

```bash
# 启动采集器
sudo BPF_DIR=bpf ./bin/collector

# 开另一个终端，查看状态
curl -s http://localhost:8090/api/summary | python3 -m json.tool | grep robot_safety

# 预期输出："robot_safety": "NOMINAL"
# （WSL2 上是 CRITICAL，物理 Linux 上应该是 NOMINAL）
```

在 NOMINAL 状态下打开 Dashboard (`http://localhost:8090`) 截图一张。

---

## 第四步：实验二 —— 故障注入对比

**目的**：在物理 Linux 上跑三组故障注入，与 WSL2 数据对比。

```bash
# 确保 collector 在运行
# 跑三组实验
for FAULT in 5 3 0; do
    echo "=== fault=$FAULT ==="
    python3 ros2/demo_control.py --fault $FAULT &
    sleep 22
    kill %1 2>/dev/null
    curl -s http://localhost:8090/api/summary | python3 -c "
import sys,json; d=json.load(sys.stdin)
print(f'Warnings: {d[\"loop_warnings\"]}, Criticals: {d[\"loop_criticals\"]}')
print(f'Safety: {d[\"robot_safety\"]}')
"
done
```

**预期**：
- fault=5：CRITICAL（故障确实触发了）
- fault=3：WARNING 或 CRITICAL  
- fault=0：NOMINAL（物理 Linux 正常调度，不像 WSL2 那样抖动 >20ms）

---

## 第五步：实验三 —— serial_monitor 验证

**目的**：在物理 Linux 上验证串口探针。

```bash
# 1. 插上 STM32 的 USB 线
# 2. 确认串口设备
ls /dev/ttyUSB* /dev/ttyACM*
# 应该看到 /dev/ttyUSB0 或 /dev/ttyACM0

# 3. 启动 collector（此时 serial_monitor 应该能挂载了）
sudo BPF_DIR=bpf ./bin/collector
# 看日志里有没有 "[collector] serial_monitor active"

# 4. 正常运行时查看 serial_stalls
curl -s http://localhost:8090/api/summary | python3 -c "
import sys,json; d=json.load(sys.stdin)
print(f'serial_stalls: {d[\"serial_stalls\"]}')
"  
# 正常应该为 0

# 5. 拔掉 STM32 USB 线，等 5 秒
# 再次查看 serial_stalls 应该增加了
curl -s http://localhost:8090/api/summary | python3 -c "
import sys,json; d=json.load(sys.stdin)
print(f'serial_stalls: {d[\"serial_stalls\"]}')
"
# 预期 > 0（检测到停顿）
```

---

## 第六步：实验四 —— CPU 开销实测

**目的**：用 perf 测量 eBPF 探针的实际 CPU 开销。

```bash
# 1. 无 eBPF 基线
sudo perf stat -e cycles,instructions -a sleep 30 2>&1 | grep -E "cycles|instructions"

# 2. 启动 collector（加载 eBPF 探针）
sudo BPF_DIR=bpf ./bin/collector &
sleep 2

# 3. 有 eBPF 时测量
sudo perf stat -e cycles,instructions -a sleep 30 2>&1 | grep -E "cycles|instructions"

# 4. 停止 collector
sudo pkill collector

# 对比两次测量的 instructions 差值
```

**预期**：eBPF 探针引入的额外指令数 < 0.1%（与论文 5.5 节理论计算一致）。

---

## 实验结果记录

| 实验 | WSL2 | 物理 Ubuntu | 说明 |
|------|:---:|:---:|------|
| NOMINAL 验证 | CRITICAL | CRITICAL | 物理Ubuntu桌面环境也有30ms级基线抖动 |
| fault=5 | 62 CRITICAL | 5点CRITICAL (max 25ms) | 64:1子采样+进程噪声导致点数少 |
| fault=3 | 2W+2C | 3点CRITICAL (max 75ms) | 同上 |
| fault=0 | 3C (异常) | 6点CRITICAL (max 35ms) | 物理机桌面进程噪声 |
| serial_monitor | 未挂载 | **成功挂载** | tty_write+tty_read两个kprobe |
| ESTOP延迟 | <2ms (WSL2) | **3.28ms (avg, 20次)** | HTTP往返+50ms轮询 |
| CPU 开销 | 理论 0.02% | **实测 <噪声水平** | perf stat无法区分 |
| BPF内存 | 理论 901KB | **实测 1.07MB** | bpftool map list实测 |
| Collector RSS | 理论 15-20MB | **实测 14MB** | ps aux确认 |
