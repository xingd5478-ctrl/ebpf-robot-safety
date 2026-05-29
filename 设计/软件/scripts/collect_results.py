#!/usr/bin/env python3
"""Collect experiment results from running Go collector API."""
import urllib.request, json, time

API = "http://localhost:8090"

def get(endpoint):
    try:
        return json.loads(urllib.request.urlopen(API + endpoint, timeout=2).read())
    except Exception as e:
        return {"error": str(e)}

def post(endpoint, data):
    try:
        req = urllib.request.Request(API + endpoint, data=json.dumps(data).encode(),
                                     headers={"Content-Type": "application/json"})
        return urllib.request.urlopen(req, timeout=2).status
    except Exception as e:
        return str(e)

print("=" * 50)
print("Experiment Results")
print("=" * 50)

# --- Snapshot ---
summary = get("/api/summary")
alerts = get("/api/alerts")
jitter_history = get("/api/jitter_history")

print("\n--- /api/summary ---")
for k in ["loop_warnings", "loop_criticals", "last_jitter_us", "max_jitter_us",
          "serial_stalls", "serial_rx_bytes", "serial_tx_bytes",
          "sched_events", "avg_wait_ms", "max_wait_ms", "robot_safety"]:
    print("  {}: {}".format(k, summary.get(k)))

print("\n--- /api/alerts (total: {}) ---".format(len(alerts)))
for a in alerts[-6:]:
    print("  [{}] {}: {}".format(a.get("level","?"), a.get("type","?"), a.get("message","?")[:100]))

print("\n--- /api/jitter_history (total: {}) ---".format(len(jitter_history)))
if jitter_history:
    jitters = [p["jitter"] for p in jitter_history]
    print("  min: {:.1f} us".format(min(jitters)))
    print("  max: {:.1f} us".format(max(jitters)))
    print("  mean: {:.1f} us".format(sum(jitters)/len(jitters)))
    print("  points > 500us (WARNING): {}".format(len([j for j in jitters if j > 500])))
    print("  points > 2000us (CRITICAL): {}".format(len([j for j in jitters if j > 2000])))

# --- ESTOP Latency ---
print("\n--- ESTOP Latency Test ---")

# Drain any queued safety commands
for _ in range(5):
    try:
        urllib.request.urlopen(API + "/api/safety_command", timeout=1)
    except:
        pass

# Inject and measure
t1 = int(time.time() * 1000)
post("/api/command", {"cmd": "ESTOP"})

t2 = None
for _ in range(40):
    try:
        cmd_data = get("/api/safety_command")
        if cmd_data.get("cmd") == "ESTOP":
            t2 = int(time.time() * 1000)
            break
    except:
        pass
    time.sleep(0.05)

if t2:
    print("  T1 (inject): {} ms".format(t1))
    print("  T2 (detect): {} ms".format(t2))
    print("  Measured latency: {} ms".format(t2 - t1))
    print("  Theoretical worst-case: ~711 ms")
else:
    print("  ESTOP not detected (timeout)")

print("\nDone.")
