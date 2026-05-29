#!/usr/bin/env python3
"""Extract real robot control-loop jitter from STM32-MPU6050 CSV data."""
import csv, statistics, os

BASE = "/mnt/c/Users/xing2/Desktop/STM32-MPU6050-System - 副本/实验/实验数据"

FILES = [
    f"{BASE}/3/3.(1)/data/PARAM1_Empirical.csv",
    f"{BASE}/3/3.(1)/data/PARAM2_AllanFixed.csv",
    f"{BASE}/3/3.(1)/data/PARAM3_AllanAdaptive.csv",
    f"{BASE}/3/3.(1)/data/PARAM4_GyroOnly.csv",
    f"{BASE}/3/3.(3)/set1_fixed_currentyaw.csv",
    f"{BASE}/3/3.(3)/set3_fixed_currentyaw.csv",
    f"{BASE}/4/pitch/dynamic/raw.csv",
    f"{BASE}/4/roll/dynamic/raw.csv",
]

all_gaps = []
all_jitter = []

for fpath in FILES:
    if not os.path.exists(fpath):
        print(f"MISSING: {fpath}")
        continue

    gaps = []
    prev = None
    with open(fpath, encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames or []
        tc = None
        for c in ["time_ms", "elapsed_sec", "t"]:
            if c in fields: tc = c; break
        if not tc:
            continue

        for row in reader:
            val = row.get(tc, "").strip()
            if not val: continue
            try: t = float(val)
            except: continue
            t_ms = int(t * 1000) if tc in ("elapsed_sec", "t") and t < 10000 else int(t)
            if prev is not None:
                gap = t_ms - prev
                if 0 < gap < 500:
                    gaps.append(gap)
                    all_gaps.append(gap)
            prev = t_ms

    if not gaps: continue
    jitter = [abs(g - 10) for g in gaps]
    all_jitter.extend(jitter)
    s = sorted(gaps)
    n = len(gaps)
    n500 = sum(1 for j in jitter if j < 0.5)
    nW = sum(1 for j in jitter if 0.5 <= j < 2.0)
    nC = sum(1 for j in jitter if j >= 2.0)
    print(f"{os.path.basename(fpath)}: n={n} gap={statistics.mean(gaps):.1f}±{statistics.stdev(gaps):.1f}ms  jitter={statistics.mean(jitter):.3f}ms  <500us={100*n500/n:.0f}%  WARN={100*nW/n:.0f}%  CRIT={100*nC/n:.0f}%")

if all_jitter:
    s = sorted(all_jitter)
    n = len(s)
    print(f"\n=== AGGREGATE (n={n}, {len(FILES)} files) ===")
    print(f"Jitter: mean={statistics.mean(s):.3f}ms  std={statistics.stdev(s):.3f}ms")
    print(f"  P50={s[n//2]:.3f}ms  P95={s[int(n*0.95)]:.3f}ms  P99={s[int(n*0.99)]:.3f}ms")
    b500 = sum(1 for j in s if j < 0.5)
    w = sum(1 for j in s if 0.5 <= j < 2.0)
    c = sum(1 for j in s if j >= 2.0)
    print(f"  <500us: {b500} ({100*b500/n:.1f}%)  WARN: {w} ({100*w/n:.1f}%)  CRIT: {c} ({100*c/n:.1f}%)")
    gs = sorted(all_gaps)
    print(f"Gap: mean={statistics.mean(all_gaps):.1f}ms  std={statistics.stdev(all_gaps):.1f}ms  min={min(all_gaps)}ms  max={max(all_gaps)}ms")
    print(f"\n>>> PAPER: Real robot Python-side control-loop jitter (n={n})")
    print(f"    mean={statistics.mean(s):.3f}ms  P95={s[int(n*0.95)]:.3f}ms  P99={s[int(n*0.99)]:.3f}ms")
    r500 = 100*b500/n
    rW = 100*w/n
    rC = 100*c/n
    print(f"    {r500:.1f}% <500us (below eBPF WARNING), {rW:.1f}% in WARNING band, {rC:.1f}% in CRITICAL band")
    if rC > 0:
        print(f"    >> {rC:.1f}% would trigger eBPF CRITICAL -> ESTOP on real robot")
    else:
        print(f"    >> Real robot control loop is clean: no CRITICAL events expected")
else:
    print("NO DATA FOUND")
