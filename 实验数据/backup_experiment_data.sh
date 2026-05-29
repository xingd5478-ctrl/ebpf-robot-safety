#!/bin/bash
# =============================================================================
# 实验数据紧急备份脚本
# 用法: 重启进 Ubuntu → 挂载 Windows 分区 → 运行本脚本
# =============================================================================
set -e

# ---- 第一步: 挂载 Windows 分区 ----
WIN_MNT="/mnt/windows"
if ! mountpoint -q "$WIN_MNT"; then
    echo "[*] 挂载 Windows 分区..."
    sudo mkdir -p "$WIN_MNT"
    # 自动探测 Windows NTFS 分区
    WIN_PART=$(sudo fdisk -l 2>/dev/null | grep -i "ntfs\|Microsoft basic data" | head -1 | awk '{print $1}')
    if [ -z "$WIN_PART" ]; then
        echo "ERROR: 找不到 Windows NTFS 分区，手动指定: $0 /dev/nvme0n1pX"
        exit 1
    fi
    sudo mount "$WIN_PART" "$WIN_MNT"
    echo "  已挂载: $WIN_PART → $WIN_MNT"
fi

# ---- 第二步: 确定备份目标 ----
BACKUP_DIR="$WIN_MNT/Users/xing2/Desktop/ebpf-robot-safety/实验数据"
mkdir -p "$BACKUP_DIR/Allan方差" "$BACKUP_DIR/ESTOP延迟" "$BACKUP_DIR/故障注入" \
         "$BACKUP_DIR/bpftool" "$BACKUP_DIR/perf" "$BACKUP_DIR/控制周期"

echo ""
echo "============================================"
echo "  开始采集，请稍候..."
echo "============================================"

# ---- 第三步: bpftool 快照 ----
echo "[1/5] bpftool 快照..."
sudo bpftool prog list > "$BACKUP_DIR/bpftool/prog_list.txt" 2>/dev/null || echo "  (跳过，可能无 bpftool)"
sudo bpftool map list  > "$BACKUP_DIR/bpftool/map_list.txt"  2>/dev/null || echo "  (跳过)"
echo "  OK"

# ---- 第四步: perf stat 对比 ----
echo "[2/5] perf stat CPU 开销..."
sudo perf stat -e cycles,instructions -a sleep 10 2>&1 | tee "$BACKUP_DIR/perf/baseline_no_ebpf.txt" || echo "  (跳过 perf)"
echo "  OK"

# ---- 第五步: API 数据采样 ----
echo "[3/5] API 数据采样 (30 秒)..."
if curl -s http://localhost:8090/api/summary > /dev/null 2>&1; then
    for i in $(seq 1 15); do
        curl -s http://localhost:8090/api/summary >> "$BACKUP_DIR/控制周期/api_summary_samples.jsonl"
        echo "" >> "$BACKUP_DIR/控制周期/api_summary_samples.jsonl"
        sleep 2
    done
    curl -s http://localhost:8090/api/loop  > "$BACKUP_DIR/控制周期/loop_events.json" 2>/dev/null
    curl -s http://localhost:8090/api/sched > "$BACKUP_DIR/控制周期/sched_events.json" 2>/dev/null
    curl -s http://localhost:8090/api/serial > "$BACKUP_DIR/控制周期/serial_events.json" 2>/dev/null
    echo "  OK"
else
    echo "  (collector 未运行，跳过)"
fi

# ---- 第六步: 搜索 /tmp 下的残留 ----
echo "[4/5] 搜索 /tmp 残留数据..."
for pattern in "exp_*" "*.csv" "collector*" "robot_*"; do
    FOUND=$(find /tmp /home -maxdepth 3 -name "$pattern" -type f 2>/dev/null | head -20)
    if [ -n "$FOUND" ]; then
        echo "$FOUND" >> "$BACKUP_DIR/recovered_files_list.txt"
        for f in $FOUND; do
            cp "$f" "$BACKUP_DIR/" 2>/dev/null && echo "  已恢复: $(basename $f)"
        done
    fi
done
echo "  OK"

# ---- 第七步: 搜索 Allan 方差原始数据 ----
echo "[5/5] 搜索 Allan 方差原始数据..."
find /tmp /home ~/Desktop ~/Documents -maxdepth 4 \
    \( -name "*allan*" -o -name "*mpu6050*" -o -name "*gyro*" -o -name "*imu_data*" -o -name "*static*" \) \
    -type f 2>/dev/null | head -20 > "$BACKUP_DIR/recovered_files_list.txt"
cat "$BACKUP_DIR/recovered_files_list.txt"
echo "  OK"

# ---- 完成 ----
echo ""
echo "============================================"
echo "  备份完成!"
echo "  数据位置: $BACKUP_DIR"
echo "============================================"
ls -la "$BACKUP_DIR"/*/
echo ""
echo "重启回 Windows 后可以在:"
echo "  C:\\Users\\xing2\\Desktop\\ebpf-robot-safety\\实验数据\\"
echo "查看所有备份文件。"
