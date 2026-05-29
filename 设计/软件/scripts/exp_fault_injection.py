#!/usr/bin/env python3
"""Experiment: fault injection + eBPF detection."""
import urllib.request, json, subprocess, time, sys, os

API = "http://localhost:8090"

def get(endpoint):
    return json.loads(urllib.request.urlopen(API + endpoint, timeout=5).read())

# Drain safety commands
for _ in range(3):
    try: urllib.request.urlopen(API + "/api/safety_command", timeout=2)
    except: pass

# Read baseline
s0 = get("/api/summary")
print("Baseline: warnings={} criticals={}".format(
    s0.get("loop_warnings",0), s0.get("loop_criticals",0)))

# Run demo with fault injection
print("\nRunning demo_control.py --fault 5 for 30s...")
t1 = time.time()
proc = subprocess.Popen(
    ["python3", "ros2/demo_control.py", "--fault", "5"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(32)
proc.terminate()
proc.wait()
t2 = time.time()
print("Duration: {}s".format(int(t2-t1)))

# Collect results
s = get("/api/summary")
a = get("/api/alerts")
j = get("/api/jitter_history")

print("\n" + "="*50)
print("EXPERIMENT RESULTS")
print("="*50)
print("Safety status:     {}".format(s.get("robot_safety","?")))
print("Loop warnings:     {}".format(s.get("loop_warnings",0)))
print("Loop criticals:    {}".format(s.get("loop_criticals",0)))
print("Last jitter:       {:.0f} us".format(s.get("last_jitter_us",0)))
print("Max jitter:        {:.0f} us".format(s.get("max_jitter_us",0)))
print("Total alerts:      {}".format(len(a)))
print("Jitter data points: {}".format(len(j)))

if j:
    crits = [p for p in j if p["jitter"] > 2000]
    warns = [p for p in j if 500 < p["jitter"] <= 2000]
    norms = [p for p in j if p["jitter"] <= 500]
    print("  >2000us (CRITICAL): {}".format(len(crits)))
    print("  500-2000us (WARNING): {}".format(len(warns)))
    print("  <500us (NOMINAL):    {}".format(len(norms)))

# Show recent alerts
print("\nRecent alerts:")
for alert in a[-5:]:
    print("  [{}] {}: {}".format(alert.get("level","?"), alert.get("type","?"),
          alert.get("message","?")[:100]))

print("\nDone.")
