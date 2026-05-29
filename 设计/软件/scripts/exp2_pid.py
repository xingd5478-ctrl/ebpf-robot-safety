#!/usr/bin/env python3
"""Experiment 2 v2: Track demo PID for clean fault injection detection."""
import urllib.request, json, subprocess, time, os

API = "http://localhost:8090"
PASS = "xing750808"
BPF_DIR = "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bpf"
COLLECTOR = "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/bin/collector"
DEMO = "/home/xingdong/桌面/ebpf-robot-safety/ebpf-robot-safety/ros2/demo_control.py"

def restart_collector():
    subprocess.run(f"echo {PASS} | sudo -S pkill collector", shell=True, capture_output=True)
    time.sleep(2)
    subprocess.Popen(
        f"echo {PASS} | sudo -S bash -c 'export BPF_DIR=\"{BPF_DIR}\" && {COLLECTOR}'",
        shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    for _ in range(5):
        try:
            urllib.request.urlopen(API + "/api/summary", timeout=2)
            return True
        except:
            time.sleep(1)
    return False

def api_get(endpoint):
    return json.loads(urllib.request.urlopen(API + endpoint, timeout=5).read())

def run_trial(fault, duration=25):
    print(f"\n{'='*60}")
    print(f"Trial: fault={fault}, duration={duration}s")
    print(f"{'='*60}")

    if not restart_collector():
        print("ERROR: Collector failed")
        return None

    # Get baseline
    s0 = api_get("/api/summary")
    loop_events_before = len(api_get("/api/loop"))
    print(f"Baseline: loop_events={loop_events_before}, W={s0['loop_warnings']}, C={s0['loop_criticals']}")

    # Start demo and capture PID
    proc = subprocess.Popen(
        ["python3", DEMO, "--fault", str(fault)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    demo_pid = proc.pid
    print(f"Demo PID: {demo_pid}")

    time.sleep(duration)

    proc.terminate()
    proc.wait()
    stdout, _ = proc.communicate()

    # Parse demo output
    fault_lines = [l for l in stdout.decode().split('\n') if '[FAULT]' in l]
    overrun_lines = [l for l in stdout.decode().split('\n') if '[WARN]' in l]
    faults_injected = len(fault_lines)
    overruns = len(overrun_lines)
    print(f"Demo: faults_injected={faults_injected}, overruns={overruns}")

    # Extract last cycle count
    total_cycles = 0
    for line in stdout.decode().split('\n'):
        if 'cycle=' in line:
            try:
                total_cycles = int(line.split('cycle=')[1].split()[0])
            except: pass

    # Get final state
    s1 = api_get("/api/summary")
    loop_events_after = api_get("/api/loop")

    # Filter loop events by demo PID
    demo_events = [e for e in loop_events_after if e.get('PID', -1) == demo_pid]
    new_events = loop_events_after[loop_events_before:]

    new_w = s1['loop_warnings'] - s0['loop_warnings']
    new_c = s1['loop_criticals'] - s0['loop_criticals']

    print(f"\nResults:")
    print(f"  Total cycles:        {total_cycles}")
    print(f"  Faults injected:     {faults_injected}")
    print(f"  Demo overruns:       {overruns}")
    print(f"  New loop events:     {len(new_events)}")
    print(f"  Demo PID events:     {len(demo_events)}")
    print(f"  New warnings:        {new_w}")
    print(f"  New criticals:       {new_c}")
    print(f"  Last jitter (us):    {s1['last_jitter_us']:.1f}")
    print(f"  Max jitter (us):     {s1['max_jitter_us']:.1f}")
    print(f"  Safety status:       {s1['robot_safety']}")

    # Show demo PID events detail
    if demo_events:
        print(f"\n  Demo PID jitter events:")
        for e in demo_events[-10:]:
            jitter_us = e.get('JitterNs', 0) / 1000.0
            print(f"    jitter={jitter_us:.1f}us severity={e.get('Severity','?')}")

    # Sched monitor data
    sched = api_get("/api/sched")
    print(f"  Sched events:        {s1['sched_events']}")
    print(f"  Sched avg wait:      {s1['avg_wait_ms']:.1f}ms")
    print(f"  Sched max wait:      {s1['max_wait_ms']:.1f}ms")

    return {
        'fault': fault,
        'cycles': total_cycles,
        'faults_injected': faults_injected,
        'overruns': overruns,
        'new_loop_events': len(new_events),
        'demo_events': len(demo_events),
        'new_w': new_w, 'new_c': new_c,
        'last_jitter': s1['last_jitter_us'],
        'max_jitter': s1['max_jitter_us'],
        'safety': s1['robot_safety'],
    }

# Keep collector running for sched data
results = []
for fault in [5, 3, 0]:
    r = run_trial(fault, 25)
    if r: results.append(r)
    time.sleep(3)

print(f"\n{'='*70}")
print("EXPERIMENT 2 SUMMARY")
print(f"{'='*70}")
print(f"{'fault':>6} | {'cycles':>7} | {'injected':>9} | {'overruns':>9} | {'events':>7} | {'demo_ev':>8} | {'new_W':>6} | {'new_C':>6} | {'safety':>10}")
print("-" * 70)
for r in results:
    print(f"{r['fault']:>6} | {r['cycles']:>7} | {r['faults_injected']:>9} | {r['overruns']:>9} | {r['new_loop_events']:>7} | {r['demo_events']:>8} | {r['new_w']:>6} | {r['new_c']:>6} | {r['safety']:>10}")

print("\nDone. Collector left running for next experiments.")
