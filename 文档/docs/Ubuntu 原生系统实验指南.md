# Ubuntu 原生系统实验指南

> 重启 → 选 Ubuntu → 按下面步骤执行

---

## 第一步：挂载 Windows 分区（访问项目文件）

```bash
# 找到 Windows 分区（通常是 /dev/nvme0n1p3 或 /dev/sda3）
sudo fdisk -l | grep -i "windows\|ntfs\|Basic data" | head -5

# 挂载（把 /dev/nvme0n1pX 换成上面找到的分区）
sudo mkdir -p /mnt/windows
sudo mount /dev/nvme0n1p3 /mnt/windows

# 验证项目文件能访问
ls /mnt/windows/Users/xing2/Desktop/ebpf-robot-safety/设计/软件/bpf/
```

## 第二步：安装依赖（一次性，5分钟）

```bash
sudo apt update
sudo apt install -y golang-go clang llvm libbpf-dev linux-tools-common linux-tools-generic python3-pip make
pip3 install pyserial --break-system-packages
```

## 第三步：确认内核支持 eBPF

```bash
uname -r                          # 应该 >= 5.8
ls /sys/kernel/btf/vmlinux        # 存在 = CO-RE 支持
ls /sys/kernel/debug/tracing/events/syscalls/ | grep nanosleep  # tracepoint 可用
```

## 第四步：编译

```bash
cd /mnt/windows/Users/xing2/Desktop/ebpf-robot-safety/设计/软件

# 编译 BPF 探针
make build-bpf

# 编译 Go 采集器（国内用代理）
export GOPROXY=https://goproxy.cn,direct
make build-go
```

## 第五步：连接小车 STM32

```bash
# 插上 USB-TTL 线后检查
ls /dev/ttyUSB* /dev/ttyACM*
# 应该看到 /dev/ttyUSB0 或 /dev/ttyACM0

# 快速验证 STM32 在发送数据
python3 -c "import serial; s=serial.Serial('/dev/ttyUSB0',460800,timeout=2); d=s.read(32); print('Header:', d[:2].hex() if len(d)>=2 else 'NO DATA'); s.close()"
# 输出 Header: badd = 正常
```

## 第六步：启动实验

```bash
cd /mnt/windows/Users/xing2/Desktop/ebpf-robot-safety/设计/软件

# 终端1: 启动 collector
sudo BPF_DIR=bpf ./bin/collector

# 终端2（新开一个终端）: 启动 robot_control
cd /mnt/windows/Users/xing2/Desktop/ebpf-robot-safety/设计/软件
python3 ros2/robot_control.py --serial /dev/ttyUSB0
# 看到 Serial opened 和 cycle=1000... 就是成功了
```

## 第七步：注册 PID + 跑实验

```bash
# 终端3: 注册 PID
curl -X POST http://localhost:8090/api/monitor_pid \
  -H "Content-Type: application/json" \
  -d "{\"pid\":$(pgrep -f robot_control | head -1)}"

# 查看状态
curl -s http://localhost:8090/api/summary | python3 -m json.tool | grep -E "robot_safety|last_jitter|serial_tx|robot_yaw"

# 浏览器打开 Dashboard
firefox http://localhost:8090
```

## 第八步：逐项跑实验

```bash
# 实验二：故障注入（终端2里 Ctrl+C 停掉 robot_control，换成这个）
python3 ros2/demo_control.py --fault 5   # 25秒后 Ctrl+C
python3 ros2/demo_control.py --fault 3   # 25秒后 Ctrl+C
python3 ros2/demo_control.py --fault 0   # 对照组

# 实验三：ESTOP 延迟
for i in $(seq 1 10); do
  T1=$(date +%s%N)
  curl -s -X POST http://localhost:8090/api/command -H "Content-Type: application/json" -d '{"cmd":"ESTOP"}'
  T2=$(date +%s%N)
  echo "第${i}次: $(( (T2-T1)/1000 ))us"
  sleep 0.5
done

# 实验七（增强版）：电机运动 + CPU 压力
# 先让电机转起来
python3 -c "import serial; s=serial.Serial('/dev/ttyUSB0',460800); s.write(b'FWD 400\r\n'); import time; time.sleep(0.5); s.close()"
# 然后启动 robot_control 采集数据
python3 ros2/robot_control.py --serial /dev/ttyUSB0 &
# 施加 CPU 压力
stress --cpu 2 --timeout 30s
# 收集数据
curl -s http://localhost:8090/api/summary | python3 -m json.tool

# 全部完成后发送停止命令
python3 -c "import serial; s=serial.Serial('/dev/ttyUSB0',460800); s.write(b'STOP\r\n'); s.close()"
```

---

## 和 WSL2 环境的关键区别

| | WSL2 | 原生 Ubuntu |
|------|:--:|:--:|
| eBPF 监控 robot_control | ❌ (进程在 Windows) | ✅ (同系统) |
| serial_monitor 抓 TTY | ❌ (TCP 桥接) | ✅ (真实 TTY) |
| ESPOP 串口直发 | ❌ (需桥接) | ✅ (直接 tty_write) |
| motor 命令 | 需切进程 | 同进程搞定 |

**原生 Ubuntu 上，五条实验链路全部闭环，审稿人挑不出架构问题。**
