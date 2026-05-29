#!/usr/bin/env python3
"""Experiment 3: ESTOP Safety Loop Latency Measurement.
Measures end-to-end latency from ESTOP injection to detection via polling.
"""
import urllib.request, json, time

API = "http://localhost:8090"
PASS = "xing750808"
TRIALS = 20

print("=" * 60)
print("Experiment 3: ESTOP Loop Latency Measurement")
print(f"Trials: {TRIALS}")
print("=" * 60)

# Drain any pending safety command
for _ in range(5):
    try:
        urllib.request.urlopen(API + "/api/safety_command", timeout=2)
    except:
        pass

latencies = []

for i in range(TRIALS):
    # Inject ESTOP
    t1_ns = time.monotonic_ns()
    try:
        req = urllib.request.Request(
            API + "/api/command",
            data=b'{"cmd":"ESTOP"}',
            headers={"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=2)
    except Exception as e:
        print(f"  Trial {i+1}: inject failed ({e})")
        continue

    # Poll until detected
    t2_ns = None
    for _ in range(40):  # max 2s polling (50ms * 40)
        try:
            r = urllib.request.urlopen(API + "/api/safety_command", timeout=1)
            d = json.loads(r.read())
            if d.get("cmd") == "ESTOP":
                t2_ns = time.monotonic_ns()
                break
        except:
            pass
        time.sleep(0.05)

    if t2_ns:
        latency_ms = (t2_ns - t1_ns) / 1e6
        latencies.append(latency_ms)
        print(f"  Trial {i+1:2d}: {latency_ms:.2f} ms")
    else:
        print(f"  Trial {i+1:2d}: NOT DETECTED (timeout)")

    time.sleep(0.1)  # brief gap between trials

print(f"\n{'='*60}")
print("RESULTS")
print(f"{'='*60}")

if latencies:
    latencies.sort()
    avg = sum(latencies) / len(latencies)
    median = latencies[len(latencies) // 2]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)] if len(latencies) >= 100 else latencies[-1]
    print(f"  Trials successful: {len(latencies)}/{TRIALS}")
    print(f"  Min:               {min(latencies):.2f} ms")
    print(f"  Max:               {max(latencies):.2f} ms")
    print(f"  Median:            {median:.2f} ms")
    print(f"  Average:           {avg:.2f} ms")
    print(f"  P95:               {p95:.2f} ms")
    print(f"\n  Theoretical worst-case: 711 ms")
    print(f"  Theoretical average:    356 ms")
    print(f"  Measured average/theoretical: {avg/356:.2f}x")
    print(f"\n  Latency breakdown:")
    print(f"    HTTP POST (inject):    ~1-2 ms")
    print(f"    Polling interval avg:  ~25 ms (50ms period / 2)")
    print(f"    HTTP GET (poll):       ~1-2 ms")
    print(f"    Total measured:        {avg:.1f} ms (dominated by 50ms polling)")

print("\nDone.")
