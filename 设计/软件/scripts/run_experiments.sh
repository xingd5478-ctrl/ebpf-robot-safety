#!/bin/bash
# eBPF Robot Safety — Non-Hardware Experiments
# Run from WSL: bash scripts/run_experiments.sh

set -e
export PATH=/usr/local/go/bin:$PATH
export BPF_DIR=bpf
export GOPROXY=https://goproxy.cn,direct
cd "$(dirname "$0")/.."

PASS=xing750808

echo "=============================================="
echo "  Experiment 2: Fault Injection (30s)"
echo "=============================================="

# Rebuild
make clean 2>/dev/null
make build-bpf 2>&1
cd cmd/collector && CGO_ENABLED=0 go build -buildvcs=false -o ../../bin/collector . 2>&1 && cd ../..
echo "Build OK"

# Setup BPF
echo "$PASS" | sudo -S pkill collector 2>/dev/null || true
sleep 1

# Start collector
echo "$PASS" | sudo -S -E ./bin/collector > /tmp/coll.log 2>&1 &
CPID=$!
sleep 3

if ! kill -0 $CPID 2>/dev/null; then
    echo "FAIL: Collector died"
    cat /tmp/coll.log
    exit 1
fi
echo "Collector running (PID $CPID)"
cat /tmp/coll.log

# --- Experiment 2: Fault Injection ---
echo ""
echo "[Exp2] Running demo_control.py --fault 3 for 30s..."
python3 ros2/demo_control.py --fault 3 > /tmp/fault_test.log 2>&1 &
DPID=$!
sleep 32
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

CYCLES=$(grep -c "cycle=" /tmp/fault_test.log 2>/dev/null || echo 0)
FAULTS=$(grep -c FAULT /tmp/fault_test.log 2>/dev/null || echo 0)
echo "Cycles: $CYCLES, Faults: $FAULTS"

# Collect API data
echo ""
echo "--- /api/summary ---"
curl -s http://localhost:8090/api/summary 2>&1
python3 -c "
import urllib.request, json
s = json.loads(urllib.request.urlopen('http://localhost:8090/api/summary').read())
print('Safety Status:', s.get('robot_safety'))
print('Loop Warnings:', s.get('loop_warnings'))
print('Loop Criticals:', s.get('loop_criticals'))
print('Last Jitter:', s.get('last_jitter_us'), 'us')
print('Max Jitter:', s.get('max_jitter_us'), 'us')
print('Serial Stalls:', s.get('serial_stalls'))
print('Sched Events:', s.get('sched_events'))
"

echo ""
echo "--- Alerts ---"
python3 -c "
import urllib.request, json
alerts = json.loads(urllib.request.urlopen('http://localhost:8090/api/alerts').read())
print('Total alerts:', len(alerts))
for a in alerts[-8:]:
    print(f'  [{a[\"level\"]}] {a[\"type\"]}: {a[\"message\"][:120]}')
"

echo ""
echo "--- Jitter History ---"
python3 -c "
import urllib.request, json
pts = json.loads(urllib.request.urlopen('http://localhost:8090/api/jitter_history').read())
print('Total points:', len(pts))
if pts:
    jitters = [p['jitter'] for p in pts]
    print('Min jitter: {:.1f} us'.format(min(jitters)))
    print('Max jitter: {:.1f} us'.format(max(jitters)))
    print('Mean jitter: {:.1f} us'.format(sum(jitters)/len(jitters)))
    criticals = [p for p in pts if p['jitter'] > 2000]
    print('Points > CRITICAL (2000us):', len(criticals))
"

# --- Experiment 3: ESTOP Latency ---
echo ""
echo "=============================================="
echo "  Experiment 3: ESTOP Loop Latency"
echo "=============================================="

# Clear existing safety command
curl -s http://localhost:8090/api/safety_command > /dev/null 2>&1 || true

# Inject ESTOP and measure
T1=$(date +%s%3N)
curl -s -X POST http://localhost:8090/api/command \
    -H "Content-Type: application/json" \
    -d '{"cmd":"ESTOP"}' > /dev/null 2>&1

# Poll until ESTOP detected
T2=""
for i in $(seq 1 20); do
    CMD=$(curl -s http://localhost:8090/api/safety_command 2>&1)
    CMDVAL=$(echo "$CMD" | python3 -c "import sys,json; print(json.load(sys.stdin).get('cmd',''))" 2>/dev/null)
    if [ "$CMDVAL" = "ESTOP" ]; then
        T2=$(date +%s%3N)
        break
    fi
    sleep 0.05
done

if [ -n "$T2" ]; then
    LAT=$((T2 - T1))
    echo "ESTOP injection time:  $T1 ms"
    echo "ESTOP detected time:   $T2 ms"
    echo "Measured latency:      ${LAT} ms"
else
    echo "ESTOP not detected within 1s polling window"
fi

# --- BPF Probes Verification ---
echo ""
echo "=============================================="
echo "  Experiment 1: BPF Probe Verification"
echo "=============================================="
echo "$PASS" | sudo -S bpftool prog list 2>/dev/null | grep -E "nanosleep|clock_nanosleep|tty|sched_switch|sched_wakeup" | head -10

echo ""
echo "--- BPF Map List ---"
echo "$PASS" | sudo -S bpftool map list 2>/dev/null | head -20

# --- Cleanup ---
echo ""
echo "$PASS" | sudo -S kill $CPID 2>/dev/null
wait $CPID 2>/dev/null
echo "=============================================="
echo "  Experiments Complete"
echo "=============================================="
