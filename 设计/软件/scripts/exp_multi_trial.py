#!/usr/bin/env python3
"""Multi-trial experiment: fault=3, fault=0."""
import urllib.request, json, subprocess, time

API = "http://localhost:8090"

def get(endpoint):
    return json.loads(urllib.request.urlopen(API + endpoint, timeout=5).read())

# Drain safety
for _ in range(3):
    try: urllib.request.urlopen(API + "/api/safety_command", timeout=2)
    except: pass

results = []

for fault in [3, 0]:
    # Get baseline
    s0 = get("/api/summary")
    w0 = s0.get("loop_warnings", 0)
    c0 = s0.get("loop_criticals", 0)

    print("\n" + "="*50)
    print("Trial: fault={}".format(fault))
    print("="*50)

    t1 = time.time()
    proc = subprocess.Popen(
        ["python3", "ros2/demo_control.py", "--fault", str(fault)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(22)
    proc.terminate()
    proc.wait()

    s = get("/api/summary")
    a = get("/api/alerts")
    j = get("/api/jitter_history")

    new_w = s.get("loop_warnings", 0) - w0
    new_c = s.get("loop_criticals", 0) - c0
    new_a = len(a)

    print("New warnings:   {}".format(new_w))
    print("New criticals:  {}".format(new_c))
    print("Last jitter:    {:.0f} us".format(s.get("last_jitter_us", 0)))
    print("Max jitter:     {:.0f} us".format(s.get("max_jitter_us", 0)))
    print("Safety:         {}".format(s.get("robot_safety", "?")))
    print("Jitter points:  {}".format(len(j)))

    if j:
        crits = len([p for p in j if p["jitter"] > 2000])
        warns = len([p for p in j if 500 < p["jitter"] <= 2000])
        norms = len([p for p in j if p["jitter"] <= 500])
        print("CRIT(>2000us):  {}".format(crits))
        print("WARN(500-2000): {}".format(warns))
        print("NORM(<500us):   {}".format(norms))

    results.append({
        "fault": fault, "new_warnings": new_w, "new_criticals": new_c,
        "max_jitter": s.get("max_jitter_us", 0), "safety": s.get("robot_safety", "?"),
        "alerts": new_a, "points": len(j)
    })

print("\n" + "="*50)
print("SUMMARY TABLE")
print("="*50)
print("fault | warnings | criticals | max_jitter | safety")
for r in results:
    print("  {:3d}  |    {:4d}   |    {:4d}    | {:8.0f} | {}".format(
        r["fault"], r["new_warnings"], r["new_criticals"],
        r["max_jitter"], r["safety"]))
print("\nDone.")
