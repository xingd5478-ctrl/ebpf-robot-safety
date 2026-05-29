#!/usr/bin/env python3
"""ESTOP latency: 50 trials for statistical distribution."""
import urllib.request, json, time, statistics, subprocess, sys

API = "http://localhost:8090"
TRIALS = 50

def drain():
    for _ in range(5):
        try: urllib.request.urlopen(API + "/api/safety_command", timeout=1)
        except: pass

def inject_and_measure():
    t1 = int(time.time() * 1000)
    req = urllib.request.Request(API + "/api/command",
        data=b'{"cmd":"ESTOP"}',
        headers={"Content-Type": "application/json"})
    try: urllib.request.urlopen(req, timeout=2)
    except: return -1

    for _ in range(40):
        try:
            r = urllib.request.urlopen(API + "/api/safety_command", timeout=1)
            d = json.loads(r.read())
            if d.get("cmd") == "ESTOP":
                t2 = int(time.time() * 1000)
                return t2 - t1
        except: pass
        time.sleep(0.05)
    return -1

latencies = []
drain()

for i in range(TRIALS):
    lat = inject_and_measure()
    if lat >= 0:
        latencies.append(lat)
        if i % 10 == 0: print(f"  {i}/{TRIALS}...", end="\r")
    drain()  # clear for next trial
    time.sleep(0.01)

print(f"\nCompleted {len(latencies)}/{TRIALS} trials")

if latencies:
    latencies.sort()
    n = len(latencies)
    print(f"  Min:     {min(latencies):.1f} ms")
    print(f"  Median:  {statistics.median(latencies):.1f} ms")
    print(f"  Mean:    {statistics.mean(latencies):.1f} ms")
    print(f"  P95:     {latencies[int(n*0.95)]:.1f} ms")
    print(f"  P99:     {latencies[int(n*0.99)]:.1f} ms")
    print(f"  Max:     {max(latencies):.1f} ms")
    print(f"  StdDev:  {statistics.stdev(latencies):.1f} ms")
else:
    print("  No valid measurements")
