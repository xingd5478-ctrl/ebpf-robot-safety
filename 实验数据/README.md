# 实验数据

> 论文中引用的所有原始实验数据。审稿人如要求提供，从此目录获取。

## 目录结构

```
实验数据/
├── README.md                       ← 本文件
├── final_summary.json              ← 实验汇总（2026-05-29）
├── backup_experiment_data.sh       ← Ubuntu 端一键备份脚本
├── Allan方差/                      ← MPU6050 噪声系数文档（需硬件重采）
├── bpftool/                        ← bpftool prog/map list 快照
├── 故障注入/                       ← fault=5/3/0 三组实验（PID注册）
├── ESTOP延迟/                      ← 50次 ESTOP 延迟测量
├── eBPF_vs_Watchdog/               ← eBPF vs 应用层Watchdog对比（新增）
├── perf/                           ← perf stat 有/无 eBPF 对比
└── 控制周期/                       ← 稳定性快照 + 真实机器人6580样本
```

## 论文中的数据引用关系

| 论文实验 | 对应原始数据 | 文件数 | 状态 |
|---------|------------|:---:|:---:|
| 实验一：BPF 探针加载 | `bpftool/` | 2 | 2026-05-29 |
| 实验二：故障注入 | `故障注入/` (fault=5/3/0) | 22 | 2026-05-29 PID注册 |
| 实验三：ESTOP 延迟 | `ESTOP延迟/` (50次) | 3 | 2026-05-29 |
| 实验四：性能开销 | `perf/` (有/无eBPF) | 4 | 2026-05-29 |
| 实验五：eBPF vs Watchdog | `eBPF_vs_Watchdog/` (33+36次) | 4 | 2026-05-29 新增 |
| 实验六：稳定性 | `控制周期/` (5分钟) | 3 | 2026-05-29 |
| 实验七：真实机器人 | `控制周期/` (6580样本) | 1 | 历史数据 |
| Allan 方差 | `Allan方差/` | 1 | 历史数据，需硬件 |

## 实验五关键发现

应用层Watchdog检测条件为 `elapsed > 2*period (20ms)`，即仅检测双周期超限。
eBPF loop_monitor 在内核态通过 nanosleep tracepoint 直接测量实际间隔，CRITICAL阈值仅 2000us。
**粒度差距 40 倍**（20000us vs 500us）。

实验验证：
- 标准故障（3-8ms）：eBPF 100\% 检出，Watchdog 0\%
- 大故障（5-15ms）：eBPF 81\% 检出，Watchdog 仍 0\%（15ms故障使elapsed≈25ms，sleep\_ns≈-15ms < -10ms 触发，但Python进程提前退出）

## 数据获取方式

### 重新采集（Ubuntu 环境）
```bash
cd /home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/设计/软件/scripts
bash run_experiments.sh
```

### bpftool 采集
```bash
sudo bpftool prog list > bpftool_progs.txt
sudo bpftool map list > bpftool_maps.txt
```

### perf 采集
```bash
sudo perf stat -e cycles,instructions,task-clock -a sleep 10 2>&1 > perf_baseline_no_ebpf.txt
# 启动 collector 后再跑一次
sudo perf stat -e cycles,instructions,task-clock -a sleep 10 2>&1 > perf_with_ebpf.txt
```

### Allan 方差数据（需硬件）
```bash
python3 scripts/collect_mpu6050_static.py --duration 7200 --rate 100
```

## 数据保留策略

- **必须保留**（论文直接引用，审稿可查验）：
  - Allan 方差噪声系数 & 拟合结果 → `Allan方差/`
  - 故障注入三组对比 → `故障注入/`
  - ESTOP 50 次延迟记录 → `ESTOP延迟/`
  - eBPF vs Watchdog 对比 → `eBPF_vs_Watchdog/`
  - bpftool 快照 → `bpftool/`
  - perf stat 对比 → `perf/`
  - 6580 控制周期总结 → `控制周期/`

- **可以丢弃**（可从脚本重新生成）：
  - 中间处理步骤的临时输出
  - 编译产物
  - 调试日志
