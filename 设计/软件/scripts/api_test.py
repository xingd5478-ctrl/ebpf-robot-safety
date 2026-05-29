#!/usr/bin/env python3
"""Quick API and frontend verification."""
import urllib.request, json

API = "http://localhost:8090"
ok = 0
fail = 0

def check(name, condition, detail=""):
    global ok, fail
    if condition:
        ok += 1
        print("  PASS  {}".format(name))
    else:
        fail += 1
        print("  FAIL  {}  {}".format(name, detail))

# GET endpoints
endpoints = ["/api/summary", "/api/loop", "/api/serial", "/api/sched",
             "/api/alerts", "/api/jitter_history", "/api/safety_command"]
for ep in endpoints:
    try:
        resp = urllib.request.urlopen(API + ep, timeout=3)
        check("GET " + ep, resp.status == 200,
              "HTTP {}".format(resp.status))
    except Exception as e:
        check("GET " + ep, False, str(e))

# POST endpoints
for ep, data in [
    ("/api/command", {"cmd": "STOP"}),
    ("/api/robot_telemetry", {"yaw_deg": 45.0, "motor_left": 300,
     "motor_right": 250, "emergency_stop": False, "jitter_us": 12.5,
     "missed_cycles": 1, "cycle_count": 1234}),
]:
    try:
        req = urllib.request.Request(API + ep, data=json.dumps(data).encode(),
                                     headers={"Content-Type": "application/json"})
        resp = urllib.request.urlopen(req, timeout=3)
        check("POST " + ep, resp.status == 200,
              "HTTP {}".format(resp.status))
    except Exception as e:
        check("POST " + ep, False, str(e))

# JSON validity
try:
    s = json.loads(urllib.request.urlopen(API + "/api/summary").read())
    check("JSON: robot_safety present", "robot_safety" in s)
    check("JSON: loop_warnings present", "loop_warnings" in s)
    check("JSON: jitter_history is list", isinstance(
        json.loads(urllib.request.urlopen(API + "/api/jitter_history").read()), list))
except Exception as e:
    check("JSON validity", False, str(e))

# Frontend
try:
    html = urllib.request.urlopen(API + "/").read().decode()
    check("Frontend: contains React", "React" in html)
    check("Frontend: contains ECharts", "echarts" in html)
    check("Frontend: has control buttons", "FWD" in html or "STOP" in html)
except Exception as e:
    check("Frontend", False, str(e))

# ESTOP atomicity
try:
    urllib.request.urlopen(API + "/api/safety_command")
    req = urllib.request.Request(API + "/api/command",
        data=b'{"cmd":"ESTOP"}', headers={"Content-Type": "application/json"})
    urllib.request.urlopen(req)
    c1 = json.loads(urllib.request.urlopen(API + "/api/safety_command").read())
    c2 = json.loads(urllib.request.urlopen(API + "/api/safety_command").read())
    check("ESTOP: first read = ESTOP", c1.get("cmd") == "ESTOP",
          "got {}".format(c1.get("cmd")))
    check("ESTOP: second read = empty", c2.get("cmd") == "",
          "got {}".format(c2.get("cmd")))
except Exception as e:
    check("ESTOP atomicity", False, str(e))

print("")
print("{} passed, {} failed".format(ok, fail))
if fail == 0:
    print("All systems operational!")
