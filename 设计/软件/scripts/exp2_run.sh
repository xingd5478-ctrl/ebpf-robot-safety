#!/bin/bash
# Experiment 2: Fault Injection Detection
set -e
PASS="xing750808"
API="http://localhost:8090"

# Reset collector for clean state
echo "$PASS" | sudo -S pkill collector 2>/dev/null || true
sleep 2

BPF_DIR="/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bpf"
COLLECTOR="/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bin/collector"
echo "$PASS" | sudo -S bash -c "export BPF_DIR=\"$BPF_DIR\" && $COLLECTOR" > /tmp/collector.log 2>&1 &
sleep 3

# Verify running
if ! curl -s $API/api/summary > /dev/null 2>&1; then
    echo "ERROR: Collector not running"
    exit 1
fi

for FAULT in 5 3 0; do
    echo ""
    echo "=========================================="
    echo "Trial: fault=$FAULT"
    echo "=========================================="

    # Get baseline counts
    s0=$(curl -s $API/api/summary)
    w0=$(echo "$s0" | python3 -c "import sys,json; print(json.load(sys.stdin).get('loop_warnings',0))")
    c0=$(echo "$s0" | python3 -c "import sys,json; print(json.load(sys.stdin).get('loop_criticals',0))")

    # Run fault injection for 22 seconds
    python3 "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/ros2/demo_control.py" --fault $FAULT > /tmp/fault_test.log 2>&1 &
    DPID=$!
    sleep 22
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null

    # Collect results
    CYCLES=$(grep -c "cycle=" /tmp/fault_test.log 2>/dev/null || echo 0)
    FAULTS_INJ=$(grep -c FAULT /tmp/fault_test.log 2>/dev/null || echo 0)

    s1=$(curl -s $API/api/summary)
    w1=$(echo "$s1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('loop_warnings',0))")
    c1=$(echo "$s1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('loop_criticals',0))")
    max_j=$(echo "$s1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('max_jitter_us',0))")
    last_j=$(echo "$s1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('last_jitter_us',0))")
    safety=$(echo "$s1" | python3 -c "import sys,json; print(json.load(sys.stdin).get('robot_safety','?'))")

    new_w=$((w1 - w0))
    new_c=$((c1 - c0))

    echo "fault=$FAULT: cycles=$CYCLES, faults_injected=$FAULTS_INJ"
    echo "new_warnings=$new_w, new_criticals=$new_c"
    echo "max_jitter_us=$max_j, last_jitter_us=$last_j"
    echo "robot_safety=$safety"

    # Jitter history analysis
    jitter_data=$(curl -s $API/api/jitter_history)
    echo "$jitter_data" | python3 -c "
import sys, json
pts = json.load(sys.stdin)
if pts:
    jitters = [p['jitter'] for p in pts]
    c = len([j for j in jitters if j > 2000])
    w = len([j for j in jitters if 500 < j <= 2000])
    n = len([j for j in jitters if j <= 500])
    print(f'jitter_points: total={len(jitters)}, CRIT(>2000)={c}, WARN(500-2000)={w}, NORM(<500)={n}')
    if jitters:
        print(f'jitter_range: min={min(jitters):.1f}, max={max(jitters):.1f}, mean={sum(jitters)/len(jitters):.1f}')
"
    sleep 2
done

echo ""
echo "=========================================="
echo "Experiment 2 Complete"
echo "=========================================="
