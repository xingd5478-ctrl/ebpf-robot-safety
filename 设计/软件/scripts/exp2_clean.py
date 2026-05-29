#!/usr/bin/env python3
"""Experiment 2: Clean fault injection detection.
Restarts collector between each trial for clean data.
"""
import urllib.request, json, subprocess, time, os, sys

API = "http://localhost:8090"
PASS = "xing750808"
BPF_DIR = "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bpf"
COLLECTOR = "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bin/collector"
DEMO = "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/ros2/demo_control.py"

def restart_collector():
    """Kill and restart collector for clean state."""
    subprocess.run(f"echo {PASS} | sudo -S pkill collector", shell=True, capture_output=True)
    time.sleep(2)
    subprocess.Popen(
        f"echo {PASS} | sudo -S bash -c 'export BPF_DIR=\"{BPF_DIR}\" && {COLLECTOR}'",
        shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    # Verify
    for _ in range(5):
        try:
            urllib.request.urlopen(API + "/api/summary", timeout=2)
            return True
        except:
            time.sleep(1)
    return False

def get_summary():
    return json.loads(urllib.request.urlopen(API + "/api/summary", timeout=5).read())

def get_jitter_history():
    return json.loads(urllib.request.urlopen(API + "/api/jitter_history", timeout=5).read())

results = []
trial_duration = 25  # seconds

for fault in [5, 3, 0]:
    print(f"\n{'='*60}")
    print(f"Trial: fault={fault}")
    print(f"{'='*60}")

    if not restart_collector():
        print("ERROR: Collector failed to start")
        continue

    # Verify clean state
    s0 = get_summary()
    print(f"Baseline: loop_warnings={s0['loop_warnings']}, loop_criticals={s0['loop_criticals']}, safety={s0['robot_safety']}")

    # Start demo controller
    proc = subprocess.Popen(
        ["python3", DEMO, "--fault", str(fault)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Wait for trial duration
    time.sleep(trial_duration)

    # Kill controller
    proc.terminate()
    proc.wait()
    stdout, stderr = proc.communicate()

    # Count actual fault injections from output
    fault_lines = [l for l in stdout.decode().split('\n') if '[FAULT]' in l]
    faults_injected = len(fault_lines)

    # Count cycles from output
    cycle_lines = [l for l in stdout.decode().split('\n') if 'cycle=' in l]
    total_cycles = 0
    for line in cycle_lines:
        try:
            c = int(line.split('cycle=')[1].split()[0])
            total_cycles = max(total_cycles, c)
        except:
            pass

    # Get results
    s1 = get_summary()
    jh = get_jitter_history()

    new_w = s1['loop_warnings'] - s0['loop_warnings']
    new_c = s1['loop_criticals'] - s0['loop_criticals']

    print(f"\nResults for fault={fault}:")
    print(f"  Cycles completed:    {total_cycles}")
    print(f"  Faults injected:     {faults_injected}")
    print(f"  New WARNINGS:        {new_w}")
    print(f"  New CRITICALS:       {new_c}")
    print(f"  Last jitter (us):    {s1['last_jitter_us']:.1f}")
    print(f"  Max jitter (us):     {s1['max_jitter_us']:.1f}")
    print(f"  Safety status:       {s1['robot_safety']}")
    print(f"  Jitter points:       {len(jh)}")

    if jh:
        jitters = [p['jitter'] for p in jh]
        crits = sum(1 for j in jitters if j > 2000)
        warns = sum(1 for j in jitters if 500 < j <= 2000)
        norms = sum(1 for j in jitters if j <= 500)
        print(f"  CRIT (>2000us):      {crits}")
        print(f"  WARN (500-2000us):   {warns}")
        print(f"  NORM (<500us):       {norms}")
        print(f"  Jitter range:        {min(jitters):.1f} - {max(jitters):.1f} us")
        print(f"  Jitter mean:         {sum(jitters)/len(jitters):.1f} us")

    detection_rate = (new_c / faults_injected * 100) if faults_injected > 0 else 0
    false_positive_rate = (new_c / faults_injected * 100) if faults_injected == 0 and new_c > 0 else 0

    results.append({
        'fault': fault,
        'cycles': total_cycles,
        'faults_injected': faults_injected,
        'new_warnings': new_w,
        'new_criticals': new_c,
        'last_jitter': s1['last_jitter_us'],
        'max_jitter': s1['max_jitter_us'],
        'safety': s1['robot_safety'],
        'jitter_points': len(jh),
    })

    time.sleep(2)

# Print summary table
print(f"\n{'='*70}")
print("EXPERIMENT 2 SUMMARY TABLE")
print(f"{'='*70}")
print(f"{'fault':>6} | {'cycles':>7} | {'faults_inj':>10} | {'new_WARN':>9} | {'new_CRIT':>9} | {'max_jit(us)':>12} | {'safety':>10}")
print("-" * 70)
for r in results:
    print(f"{r['fault']:>6} | {r['cycles']:>7} | {r['faults_injected']:>10} | {r['new_warnings']:>9} | {r['new_criticals']:>9} | {r['max_jitter']:>12.1f} | {r['safety']:>10}")

# Calculate detection rate for fault=5 and fault=3
for r in results:
    if r['fault'] > 0 and r['faults_injected'] > 0:
        rate = r['new_criticals'] / r['faults_injected'] * 100
        print(f"\nfault={r['fault']}: detection rate = {r['new_criticals']}/{r['faults_injected']} = {rate:.0f}%")

print("\nDone.")
