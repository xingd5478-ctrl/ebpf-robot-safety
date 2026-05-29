#!/bin/bash
# eBPF Robot Safety Experiment Setup
# Run: bash start_experiment.sh

DIR="/mnt/c/Users/xing2/Desktop/ebpf-robot-safety/ebpf-robot-safety/设计/软件"
cd "$DIR"

echo "=== Step 1: Starting collector ==="
sudo BPF_DIR=bpf ./bin/collector &
COLLECTOR_PID=$!
sleep 3

# Verify collector
if curl --noproxy "*" -s --max-time 2 http://localhost:8090/api/summary > /dev/null 2>&1; then
    echo "[OK] Collector running on :8090"
else
    echo "[FAIL] Collector not responding"
    exit 1
fi

echo ""
echo "=== Step 2: Starting TCP listener ==="
python3 ros2/tcp_listener.py 9998 &
LISTENER_PID=$!
sleep 2

echo "[OK] TCP listener on :9998"
echo ""
echo ">>> NOW: Open Windows PowerShell and run:"
echo "    python C:\\Users\\xing2\\Desktop\\bridge_com16.py"
echo ""
echo "    Wait until you see 'Connected!' then press Enter here..."
read -p ""

echo ""
echo "=== Step 3: Starting robot_control ==="
python3 ros2/robot_control.py --serial socket://127.0.0.1:9999 &
ROBOT_PID=$!
sleep 3

echo ""
echo "=== Step 4: Registering PID ==="
curl --noproxy "*" -s -X POST http://localhost:8090/api/monitor_pid \
  -H "Content-Type: application/json" -d "{\"pid\":$ROBOT_PID}"
echo ""

echo ""
echo "=== System Status ==="
curl --noproxy "*" -s http://localhost:8090/api/summary | python3 -m json.tool 2>/dev/null

echo ""
echo "=== All systems running ==="
echo "Collector PID: $COLLECTOR_PID"
echo "Listener PID: $LISTENER_PID"
echo "Robot PID:    $ROBOT_PID"
echo ""
echo "Dashboard: http://localhost:8090"
echo "Press Ctrl+C to stop all"
wait
