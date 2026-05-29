#!/usr/bin/env python3
"""Run multiple experiment trials with different fault settings."""
import urllib.request, json, time, subprocess, sys, os

API = "http://localhost:8090"

def get_all():
    s = json.loads(urllib.request.urlopen(API + "/api/summary").read())
    a = json.loads(urllib.request.urlopen(API + "/api/alerts").read())
    j = json.loads(urllib.request.urlopen(API + "/api/jitter_history").read())
    return s, a, j

results = []

for fault_setting in [3, 5, 0]:
    print("\n" + "="*50)
    print("Trial: fault={}".format(fault_setting))
    print("="*50)

    # Start collector
    subprocess.run(["sudo", "-S", "pkill", "collector"],
                   input="xing750808\n", text=True, capture_output=True)
    time.sleep(1)

    proc = subprocess.Popen(
        ["sudo", "-S", "-E", "./bin/collector"],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        env={**os.environ, "BPF_DIR": "bpf", "PATH": "/usr/local/go/bin:" + os.environ.get("PATH","")}
    )
    proc.stdin.write(b"xing750808\n")
    proc.stdin.flush()
    time.sleep(3)

    # Run demo
    demo = subprocess.Popen(
        ["python3", "ros2/demo_control.py", "--fault", str(fault_setting)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    time.sleep(15)
    demo.terminate()
    demo.wait()

    # Collect
    try:
        s, a, j = get_all()
        cycles = s.get("loop_warnings",0) + s.get("loop_criticals",0)  # rough proxy
        print("  loop_warnings: {}".format(s.get("loop_warnings",0)))
        print("  loop_criticals: {}".format(s.get("loop_criticals",0)))
        print("  last_jitter_us: {:.0f}".format(s.get("last_jitter_us",0)))
        print("  max_jitter_us: {:.0f}".format(s.get("max_jitter_us",0)))
        print("  robot_safety: {}".format(s.get("robot_safety","?")))
        print("  total_alerts: {}".format(len(a)))
        print("  jitter_points: {}".format(len(j)))

        results.append({
            "fault": fault_setting,
            "loop_warnings": s.get("loop_warnings",0),
            "loop_criticals": s.get("loop_criticals",0),
            "last_jitter": s.get("last_jitter_us",0),
            "max_jitter": s.get("max_jitter_us",0),
            "safety": s.get("robot_safety","?"),
            "alerts": len(a),
            "jitter_pts": len(j),
        })
    except Exception as e:
        print("  ERROR: {}".format(e))
        results.append({"fault": fault_setting, "error": str(e)})

    # Stop collector
    proc.terminate()
    proc.wait()
    time.sleep(1)

print("\n" + "="*50)
print("SUMMARY")
print("="*50)
for r in results:
    print("  fault={}: warnings={} criticals={} max_jitter={:.0f}us safety={} alerts={} pts={}".format(
        r.get("fault","?"), r.get("loop_warnings",0), r.get("loop_criticals",0),
        r.get("max_jitter",0), r.get("safety","?"), r.get("alerts",0), r.get("jitter_pts",0)
    ))
