#!/usr/bin/env python3
"""Upgraded experiments for journal quality:
  1. ESTOP latency: 50 trials
  2. Serial monitor: clean baseline + controlled stall
  3. Sched monitor: PID registration + CPU stress
"""
import urllib.request, json, subprocess, time, os, struct

API = "http://localhost:8090"
PASS = "xing750808"
RESULTS = {}

def api_get(endpoint, timeout=5):
    return json.loads(urllib.request.urlopen(API + endpoint, timeout=timeout).read())

# ============================================================
# Experiment 3+: ESTOP Latency — 50 trials
# ============================================================
print("=" * 60)
print("Experiment 3+: ESTOP Latency — 50 trials")
print("=" * 60)

# Drain
for _ in range(5):
    try: urllib.request.urlopen(API + "/api/safety_command", timeout=2)
    except: pass

latencies = []
for i in range(50):
    t1_ns = time.monotonic_ns()
    try:
        req = urllib.request.Request(API + "/api/command",
            data=b'{"cmd":"ESTOP"}',
            headers={"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=2)
    except:
        continue

    t2_ns = None
    for _ in range(40):
        try:
            r = urllib.request.urlopen(API + "/api/safety_command", timeout=0.5)
            if json.loads(r.read()).get("cmd") == "ESTOP":
                t2_ns = time.monotonic_ns()
                break
        except:
            pass
        time.sleep(0.05)
    if t2_ns:
        latencies.append((t2_ns - t1_ns) / 1e6)
    time.sleep(0.05)

if latencies:
    latencies.sort()
    n = len(latencies)
    avg = sum(latencies) / n
    med = latencies[n // 2]
    p95 = latencies[int(n * 0.95)]
    p99 = latencies[int(n * 0.99)] if n >= 100 else latencies[-1]
    print(f"  Trials: {n}/50")
    print(f"  Min: {min(latencies):.2f}ms  Max: {max(latencies):.2f}ms")
    print(f"  Median: {med:.2f}ms  Mean: {avg:.2f}ms")
    print(f"  P95: {p95:.2f}ms  P99: {p99:.2f}ms")
    RESULTS['estop_n'] = n
    RESULTS['estop_mean'] = f"{avg:.2f}ms"
    RESULTS['estop_p95'] = f"{p95:.2f}ms"

# ============================================================
# Experiment 4+: Serial Monitor — Clean Baseline + Stall
# ============================================================
print("\n" + "=" * 60)
print("Experiment 4+: Serial Monitor — Baseline + Controlled Stall")
print("=" * 60)

# Reset collector for clean serial stats
subprocess.run(f"echo {PASS} | sudo -S pkill collector", shell=True)
time.sleep(2)
subprocess.Popen(
    f"echo {PASS} | sudo -S bash -c 'export BPF_DIR=\"/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bpf\" && /home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bin/collector'",
    shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)

# Baseline: run robot_control.py continuously for 10s
print("Phase 1: Continuous reading (10s baseline)...")
s0 = api_get("/api/summary")
proc = subprocess.Popen(
    ["python3", "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/ros2/robot_control.py",
     "--serial", "/dev/ttyUSB1", "--freq", "100"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(10)
proc.terminate()
proc.wait()

s1_cont = api_get("/api/summary")
rx_cont = s1_cont['serial_rx_bytes'] - s0.get('serial_rx_bytes', 0)
stall_cont = s1_cont['serial_stalls'] - s0.get('serial_stalls', 0)
print(f"  Continuous 10s: rx_bytes={rx_cont}, stalls={stall_cont}")
RESULTS['serial_rx_10s'] = rx_cont
RESULTS['serial_stall_baseline'] = stall_cont

# Controlled stall: wait 3s then resume reading
print("Phase 2: Controlled stall (3s pause)...")
time.sleep(3)

proc2 = subprocess.Popen(
    ["python3", "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/ros2/robot_control.py",
     "--serial", "/dev/ttyUSB1", "--freq", "100"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)
proc2.terminate()
proc2.wait()

s2_stall = api_get("/api/summary")
stall_total = s2_stall['serial_stalls'] - s0.get('serial_stalls', 0)
print(f"  After stall: total_stalls={stall_total} (new stalls from pause: {stall_total - stall_cont})")
RESULTS['serial_stall_total'] = stall_total
RESULTS['serial_stall_delta'] = stall_total - stall_cont

# ============================================================
# Experiment 6+: Sched Monitor — CPU Stress
# ============================================================
print("\n" + "=" * 60)
print("Experiment 6+: Sched Monitor — CPU Stress Test")
print("=" * 60)

# Run robot_control.py with CPU stress for 5s
proc3 = subprocess.Popen(
    ["python3", "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/ros2/robot_control.py",
     "--serial", "/dev/ttyUSB1", "--freq", "100"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)

s_before = api_get("/api/summary")
print(f"  Before stress: sched_events={s_before['sched_events']}, serial_stalls={s_before['serial_stalls']}")

# CPU stress
subprocess.run(["stress", "--cpu", "4", "--timeout", "5s"], capture_output=True)
time.sleep(1)

s_after = api_get("/api/summary")
proc3.terminate()
proc3.wait()

sched_delta = s_after['sched_events'] - s_before['sched_events']
stall_delta = s_after['serial_stalls'] - s_before['serial_stalls']
print(f"  After stress: sched_events={s_after['sched_events']} (+{sched_delta}), serial_stalls={s_after['serial_stalls']} (+{stall_delta})")
RESULTS['sched_events_before'] = s_before['sched_events']
RESULTS['sched_events_after'] = s_after['sched_events']
RESULTS['stall_during_stress'] = stall_delta

# ============================================================
# Summary
# ============================================================
print("\n" + "=" * 60)
print("UPGRADED EXPERIMENT RESULTS")
print("=" * 60)
for k, v in RESULTS.items():
    print(f"  {k}: {v}")
print("\nDone.")
