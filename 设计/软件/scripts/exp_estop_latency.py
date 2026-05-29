#!/usr/bin/env python3
"""Experiment: ESTOP latency measurement."""
import urllib.request, json, time

API = "http://localhost:8090"

# Drain
for _ in range(5):
    try: urllib.request.urlopen(API + "/api/safety_command", timeout=2)
    except: pass

# Measure ESTOP round-trip
print("Injecting ESTOP...")
t1 = int(time.time() * 1000)

req = urllib.request.Request(API + "/api/command",
    data=b'{"cmd":"ESTOP"}',
    headers={"Content-Type": "application/json"})
urllib.request.urlopen(req, timeout=2)

# Poll until detected
t2 = None
for i in range(40):
    try:
        r = urllib.request.urlopen(API + "/api/safety_command", timeout=1)
        d = json.loads(r.read())
        if d.get("cmd") == "ESTOP":
            t2 = int(time.time() * 1000)
            break
    except: pass
    time.sleep(0.05)

if t2:
    latency = t2 - t1
    print("T1 (inject): {} ms".format(t1))
    print("T2 (detect): {} ms".format(t2))
    print("Measured latency: {} ms".format(latency))
    print("Theoretical worst-case: 711 ms")
    print("Ratio: {:.2f}x".format(latency/711.0 if latency > 0 else 0))
else:
    print("ESTOP not detected within polling window")

# Verify atomic clear
try:
    r2 = urllib.request.urlopen(API + "/api/safety_command", timeout=2)
    d2 = json.loads(r2.read())
    print("Second read: cmd='{}' (expect empty)".format(d2.get("cmd","")))
except: pass
