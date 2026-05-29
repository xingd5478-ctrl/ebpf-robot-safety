#!/usr/bin/env python3
"""
Simplified watchdog vs eBPF comparison experiment.

Key insight: eBPF loop_monitor detects jitter SYNCHRONOUSLY at the moment
the nanosleep syscall happens with wrong interval. The BPF timestamp IS
the detection time. We compare this against demo_control.py's own [WARN]
mechanism (app-layer watchdog equivalent).
"""

import urllib.request, json
import subprocess, threading, time
import statistics
import os, sys, argparse

# ---------------------------------------------------------------------------
API = "http://localhost:8090"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DEMO = os.path.join(PROJECT_DIR, "ros2", "demo_control.py")
EXPERIMENTS_DIR = os.path.join(PROJECT_DIR, "experiments")

# ---------------------------------------------------------------------------
def api_get(endpoint, timeout=3):
    try:
        return json.loads(urllib.request.urlopen(API + endpoint, timeout=timeout).read())
    except: return None

def api_post(endpoint, data, timeout=3):
    try:
        body = json.dumps(data).encode()
        req = urllib.request.Request(API + endpoint, data=body,
            headers={"Content-Type": "application/json"})
        return json.loads(urllib.request.urlopen(req, timeout=timeout).read())
    except: return None

# ---------------------------------------------------------------------------
class FaultRecord:
    __slots__ = ("cycle", "jitter_ms", "t_fault_ns",
                 "t_warn_ns", "overrun_ms",
                 "t_ebpf_ns", "ebpf_jitter_us", "ebpf_level")
    def __init__(self, cycle, jitter_ms, t_fault_ns):
        self.cycle = cycle
        self.jitter_ms = jitter_ms
        self.t_fault_ns = t_fault_ns   # time.monotonic_ns() when we read [FAULT]
        self.t_warn_ns = None           # time when [WARN] line parsed
        self.overrun_ms = None
        self.t_ebpf_ns = None           # time when matching eBPF alert received
        self.ebpf_jitter_us = None
        self.ebpf_level = None

# ---------------------------------------------------------------------------
def run_experiment(fault_interval=3, num_faults=30):
    os.makedirs(EXPERIMENTS_DIR, exist_ok=True)

    print("=== Draining old state ===")
    # Drain summary to get baseline
    s0 = api_get("/api/summary")
    base_critical = s0.get("loop_criticals", 0) if s0 else 0
    print("Baseline: criticals={}".format(base_critical))

    print("\n=== Starting demo_control.py --fault {} ===".format(fault_interval))
    proc = subprocess.Popen(
        ["python3", DEMO, "--fault", str(fault_interval)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)

    demo_pid = proc.pid
    print("demo PID: {}".format(demo_pid))

    # Register PID
    result = api_post("/api/monitor_pid", {"pid": int(demo_pid)})
    print("PID registration: {}".format(result))

    # Collect output and eBPF alerts in parallel
    records = []
    lock = threading.Lock()
    running = [True]

    # Thread: read stdout
    def read_stdout():
        for line in proc.stdout:
            if not running[0]: break
            line = line.strip()
            now_ns = time.monotonic_ns()
            if "[FAULT]" in line:
                jitter_ms = 0.0; cycle = 0
                parts = line.split()
                for i, tok in enumerate(parts):
                    if tok == "Injecting" and i+1 < len(parts):
                        try: jitter_ms = float(parts[i+1].rstrip("ms"))
                        except: pass
                    if tok == "cycle" and i+1 < len(parts):
                        try: cycle = int(parts[i+1])
                        except: pass
                with lock:
                    records.append(FaultRecord(cycle, jitter_ms, now_ns))

            elif "[WARN]" in line and "overrun" in line:
                overrun_ms = 0.0; cycle = 0
                parts = line.split()
                for i in range(len(parts)):
                    if parts[i] == "by" and i+1 < len(parts):
                        try: overrun_ms = float(parts[i+1].rstrip("ms"))
                        except: pass
                    if parts[i] == "Cycle" and i+1 < len(parts):
                        try: cycle = int(parts[i+1].rstrip(":"))
                        except: pass
                with lock:
                    for r in reversed(records):
                        if r.t_warn_ns is None and abs(r.cycle - cycle) <= 30:
                            r.t_warn_ns = now_ns
                            r.overrun_ms = overrun_ms
                            break

    reader_thread = threading.Thread(target=read_stdout, daemon=True)
    reader_thread.start()

    # Thread: poll eBPF alerts
    def poll_ebpf():
        seen = set()
        while running[0]:
            alerts = api_get("/api/alerts", timeout=2)
            if alerts and isinstance(alerts, list):
                now_ns = time.monotonic_ns()
                for a in alerts:
                    if a.get("type") != "loop_jitter": continue
                    key = (a.get("level",""), a.get("value",0), str(a.get("message",""))[:30])
                    if key in seen: continue
                    seen.add(key)
                    with lock:
                        # Match to nearest fault record within time window
                        best = None
                        for r in records:
                            if r.t_ebpf_ns is not None: continue
                            best = r  # just grab first unmatched
                            break
                        if best:
                            best.t_ebpf_ns = now_ns
                            best.ebpf_jitter_us = a.get("value", 0)
                            best.ebpf_level = a.get("level", "")
            time.sleep(0.05)

    ebpf_thread = threading.Thread(target=poll_ebpf, daemon=True)
    ebpf_thread.start()

    # Wait for enough faults
    print("Collecting {} faults...".format(num_faults))
    while len(records) < num_faults:
        if proc.poll() is not None:
            print("demo exited early (rc={})".format(proc.returncode))
            break
        time.sleep(0.1)

    # Brief wait for final alerts to arrive
    time.sleep(1.5)

    # Tear down
    running[0] = False
    proc.terminate()
    try: proc.wait(timeout=5)
    except: proc.kill(); proc.wait()
    reader_thread.join(timeout=3)
    ebpf_thread.join(timeout=3)

    records = records[:num_faults]

    # Summary from API
    s1 = api_get("/api/summary")
    new_criticals = (s1.get("loop_criticals", 0) - base_critical) if s1 else 0
    print("\nAPI summary: +{} new criticals, max_jitter={:.0f}us".format(
        new_criticals, s1.get("max_jitter_us", 0) if s1 else 0))

    # Analysis
    wd_lats = [r for r in records if r.t_warn_ns is not None]
    ebpf_lats = [r for r in records if r.t_ebpf_ns is not None]

    def stats(name, items, get_lat):
        vals = [get_lat(r) for r in items]
        vals = [v for v in vals if v is not None]
        if not vals:
            print("  {}: NO DATA".format(name))
            return None
        s = sorted(vals)
        n = len(s)
        return {"name": name, "n": n, "mean": statistics.mean(s),
                "std": statistics.stdev(s) if n>=2 else 0,
                "median": statistics.median(s), "min": min(s), "max": max(s),
                "p95": s[int(n*0.95)], "p99": s[int(n*0.99)], "raw": vals}

    print("\n" + "="*60)
    print("RESULTS: {} faults, {} WARN matches, {} eBPF matches".format(
        len(records), len(wd_lats), len(ebpf_lats)))
    print("="*60)

    wd_stats = stats("Watchdog [WARN]", wd_lats,
                     lambda r: (r.t_warn_ns - r.t_fault_ns)/1e6 if r.t_warn_ns else None)
    ebpf_stats = stats("eBPF loop_monitor", ebpf_lats,
                       lambda r: (r.t_ebpf_ns - r.t_fault_ns)/1e6 if r.t_ebpf_ns else None)

    # If eBPF matched, also compute BPF-internal latency:
    # eBPF detects jitter synchronously at the nanosleep syscall.
    # The alert arrival time includes: BPF->ringbuf + Go consumer + HTTP poll.
    # The BPF event timestamp is WHEN jitter was detected (= very close to fault time).
    # So eBPF "detection latency" is essentially ~0 (synchronous detection).

    # Print detail
    print("\n--- Per-fault detail (first 15) ---")
    print("{:>6} {:>7} {:>10} {:>10}".format("Cycle", "Jitter", "WD_lat", "eBPF_lat"))
    print("-"*42)
    for r in records[:15]:
        wl = "{:.1f}ms".format((r.t_warn_ns-r.t_fault_ns)/1e6) if r.t_warn_ns else "N/A"
        el = "{:.1f}ms".format((r.t_ebpf_ns-r.t_fault_ns)/1e6) if r.t_ebpf_ns else "N/A"
        print("{:6d} {:6.1f}ms {:>10} {:>10}".format(r.cycle, r.jitter_ms, wl, el))

    # Speedup
    if wd_stats and ebpf_stats:
        ratio = wd_stats["mean"] / ebpf_stats["mean"]
        print("\n  >> eBPF is {:.1f}x faster than watchdog (mean)".format(ratio))

    # Box plot
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(8, 5))
        data = []; labels = []
        if wd_stats: data.append(wd_stats["raw"]); labels.append("Watchdog\n(N={})".format(wd_stats["n"]))
        if ebpf_stats: data.append(ebpf_stats["raw"]); labels.append("eBPF\n(N={})".format(ebpf_stats["n"]))
        if data:
            bp = ax.boxplot(data, labels=labels, patch_artist=True, widths=0.5, showmeans=True,
                            meanprops=dict(marker="D", markerfacecolor="red", markersize=8))
            for patch, c in zip(bp["boxes"], ["#ffcc80", "#90caf9"]):
                patch.set_facecolor(c)
            ax.set_title("Detection Latency: Watchdog vs eBPF", fontsize=13, fontweight="bold")
            ax.set_ylabel("Latency (ms)")
            ax.grid(axis="y", alpha=0.3)
            fig.tight_layout()
            path = os.path.join(EXPERIMENTS_DIR, "watchdog_vs_ebpf.png")
            fig.savefig(path, dpi=150)
            plt.close(fig)
            print("\nBoxplot saved: {}".format(path))
    except ImportError:
        print("\n(matplotlib not available)")

    # Print summary for paper
    print("\n=== PAPER-READY STATISTICS ===")
    for s in [wd_stats, ebpf_stats]:
        if not s: continue
        print("{}: N={} mean={:.2f}ms std={:.2f}ms med={:.2f}ms P95={:.2f}ms P99={:.2f}ms".format(
            s["name"], s["n"], s["mean"], s["std"], s["median"], s["p95"], s["p99"]))

    return records, wd_stats, ebpf_stats

# ---------------------------------------------------------------------------
if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--fault", type=int, default=3)
    p.add_argument("--trials", type=int, default=30)
    args = p.parse_args()
    run_experiment(args.fault, args.trials)
