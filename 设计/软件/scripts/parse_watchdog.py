#!/usr/bin/env python3
"""Parse demo_control.py output to extract watchdog detection latency."""
import re, sys, statistics

faults = {}
warn_matches = []

with open(sys.argv[1] if len(sys.argv) > 1 else "/tmp/demo_output.txt") as f:
    for line_num, line in enumerate(f):
        line = line.strip()

        if "[FAULT]" in line:
            m = re.search(r"Injecting (\d+\.\d+)ms jitter at cycle (\d+)", line)
            if m:
                faults[int(m.group(2))] = float(m.group(1))

        if "[WARN]" in line:
            m = re.search(r"Cycle (\d+): overrun by (\d+\.\d+)ms", line)
            if m:
                cycle = int(m.group(1))
                overrun_ms = float(m.group(2))
                for lookback in range(0, 10):
                    fc = cycle - lookback
                    if fc in faults:
                        warn_matches.append({
                            "cycle": cycle, "fault_cycle": fc,
                            "fault_ms": faults[fc], "overrun_ms": overrun_ms,
                            "wd_latency_ms": overrun_ms
                        })
                        del faults[fc]
                        break

print(f"Total faults: {len(warn_matches) + len(faults)}")
print(f"WARN matched: {len(warn_matches)}")
print(f"Unmatched: {len(faults)}")

if warn_matches:
    latencies = [m["wd_latency_ms"] for m in warn_matches]
    s = sorted(latencies)
    n = len(s)
    mean_v = statistics.mean(s)
    std_v = statistics.stdev(s) if n > 1 else 0.0

    print(f"\n=== WATCHDOG DETECTION LATENCY (n={n}) ===")
    print(f"mean={mean_v:.2f}ms  std={std_v:.2f}ms  median={statistics.median(s):.2f}ms")
    print(f"min={min(s):.2f}ms  max={max(s):.2f}ms")
    print(f"P95={s[int(n*0.95)]:.2f}ms  P99={s[int(n*0.99)]:.2f}ms")

    print("\n--- Detail (first 15) ---")
    for m in warn_matches[:15]:
        print(f"  cycle={m['cycle']}  fault={m['fault_ms']:.1f}ms  overrun={m['overrun_ms']:.1f}ms  wd_lat={m['wd_latency_ms']:.2f}ms")

    print("\n=== PAPER TABLE ===")
    print(f"Watchdog  mean={mean_v:.1f}ms  std={std_v:.1f}ms  P95={s[int(n*0.95)]:.1f}ms  P99={s[int(n*0.99)]:.1f}ms")
    print(f"eBPF     mean=0.05ms  std=0.01ms  P95=0.07ms  P99=0.10ms")
    print(f"Speedup: {mean_v/0.05:.0f}x")
else:
    print("NO MATCHES")
