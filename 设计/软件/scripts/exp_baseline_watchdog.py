#!/usr/bin/env python3
"""Baseline comparison: Application-layer watchdog vs eBPF monitoring."""
import time, json, urllib.request, threading, statistics, sys

API = "http://localhost:8090"

# ============================================================
# Application-layer Watchdog (traditional approach)
# ============================================================
class AppWatchdog:
    """Traditional app-layer safety monitor for comparison with eBPF."""
    def __init__(self, period_ms=10, warn_us=500, crit_us=2000):
        self.period_ns = period_ms * 1_000_000
        self.warn_ns = warn_us * 1000
        self.crit_ns = crit_us * 1000
        self.last_checkin = time.monotonic_ns()
        self.warnings = 0
        self.criticals = 0
        self.jitters = []
        self.alerts = []
        self.running = True
        self._thread = threading.Thread(target=self._monitor, daemon=True)
        self._thread.start()

    def checkin(self):
        now = time.monotonic_ns()
        interval = now - self.last_checkin
        self.last_checkin = now
        jitter = abs(interval - self.period_ns) / 1000  # us
        self.jitters.append(jitter)
        if jitter > self.crit_ns / 1000:
            self.criticals += 1
            self.alerts.append(("critical", jitter))
        elif jitter > self.warn_ns / 1000:
            self.warnings += 1
            self.alerts.append(("warning", jitter))

    def _monitor(self):
        """Background thread: detect missed checkins (watchdog timeout)."""
        while self.running:
            time.sleep(0.05)  # 50ms polling
            elapsed = (time.monotonic_ns() - self.last_checkin) / 1e6
            if elapsed > self.crit_ns / 1e6:
                self.criticals += 1
                self.alerts.append(("critical", elapsed))

    def stop(self):
        self.running = False

# ============================================================
# eBPF Monitoring (via collector API)
# ============================================================
class EbpfMonitor:
    """Read eBPF results from Go collector API."""
    def __init__(self):
        self.baseline = self._read()

    def _read(self):
        try:
            s = json.loads(urllib.request.urlopen(API + "/api/summary", timeout=2).read())
            return {"warnings": s.get("loop_warnings", 0),
                    "criticals": s.get("loop_criticals", 0)}
        except: return {"warnings": 0, "criticals": 0}

    def delta(self):
        now = self._read()
        dw = now["warnings"] - self.baseline["warnings"]
        dc = now["criticals"] - self.baseline["criticals"]
        return dw, dc

# ============================================================
# Run comparison
# ============================================================
def run_trial(label, fault_interval, duration=20):
    """Run one trial with both monitoring methods."""
    print(f"\n{'='*50}")
    print(f"Trial: {label} (fault={fault_interval})")
    print(f"{'='*50}")

    # Init both monitors
    wd = AppWatchdog()
    ebpf = EbpfMonitor()

    t0 = time.time()
    period_ns = 10_000_000  # 10ms = 100Hz
    next_t = time.monotonic_ns()
    cycle = 0

    while time.time() - t0 < duration:
        # Simulated control loop
        cycle += 1
        wd.checkin()

        # Inject fault
        if fault_interval > 0 and cycle % (fault_interval * 100) == 0:
            import random
            delay = random.uniform(0.003, 0.008)
            time.sleep(delay)

        # Maintain period
        next_t += period_ns
        sleep_ns = next_t - time.monotonic_ns()
        if sleep_ns > 0:
            time.sleep(sleep_ns / 1e9)

    wd.stop()
    dw, dc = ebpf.delta()

    # Collect eBPF jitter data
    try:
        j = json.loads(urllib.request.urlopen(API + "/api/jitter_history", timeout=2).read())
        ebpf_jitters = [p["jitter"] for p in j]
    except: ebpf_jitters = []

    result = {
        "label": label,
        "cycles": cycle,
        "watchdog_warnings": wd.warnings,
        "watchdog_criticals": wd.criticals,
        "watchdog_max_jitter": max(wd.jitters) if wd.jitters else 0,
        "watchdog_mean_jitter": statistics.mean(wd.jitters) if wd.jitters else 0,
        "ebpf_warnings": dw,
        "ebpf_criticals": dc,
        "ebpf_max_jitter": max(ebpf_jitters) if ebpf_jitters else 0,
        "ebpf_mean_jitter": statistics.mean(ebpf_jitters) if ebpf_jitters else 0,
        "ebpf_points": len(ebpf_jitters),
    }

    # Print
    print(f"  Cycles: {cycle}")
    print(f"  Watchdog: {wd.warnings}W + {wd.criticals}C  max_jitter={result['watchdog_max_jitter']:.0f}us")
    print(f"  eBPF:     {dw}W + {dc}C  max_jitter={result['ebpf_max_jitter']:.0f}us  points={result['ebpf_points']}")

    return result

# Run all trials
results = []
for fault in [5, 3, 0]:
    for rep in range(3):
        r = run_trial(f"fault={fault}_rep{rep+1}", fault, 15)
        results.append(r)
        time.sleep(1)

# Summary
print(f"\n{'='*50}")
print("SUMMARY: Watchdog vs eBPF Comparison")
print(f"{'='*50}")
print(f"{'Trial':<20} {'WD_W':>5} {'WD_C':>5} {'eBPF_W':>6} {'eBPF_C':>6} {'WD_max':>8} {'eBPF_max':>8}")
for r in results:
    print(f"{r['label']:<20} {r['watchdog_warnings']:5d} {r['watchdog_criticals']:5d} "
          f"{r['ebpf_warnings']:6d} {r['ebpf_criticals']:6d} "
          f"{r['watchdog_max_jitter']:8.0f} {r['ebpf_max_jitter']:8.0f}")

# Aggregate by fault setting
print(f"\n{'Fault':<10} {'WD_C_mean':>10} {'eBPF_C_mean':>10} {'WD_max_mean':>12} {'eBPF_max_mean':>12}")
for fault in [5, 3, 0]:
    grp = [r for r in results if r["label"].startswith(f"fault={fault}")]
    wdc = statistics.mean([r["watchdog_criticals"] for r in grp])
    ec = statistics.mean([r["ebpf_criticals"] for r in grp])
    wdm = statistics.mean([r["watchdog_max_jitter"] for r in grp])
    em = statistics.mean([r["ebpf_max_jitter"] for r in grp])
    print(f"fault={fault:<4} {wdc:10.1f} {ec:10.1f} {wdm:12.0f} {em:12.0f}")

print("\nDone.")
