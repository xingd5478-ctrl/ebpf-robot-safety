// loop_monitor.bpf.c — Control Loop Period Jitter Monitor (v4)
//
// v4: Added monitored_pids map — robot control PIDs skip 64:1 subsampling.
//     This gives 100% sampling for the robot process while keeping low overhead
//     for other system processes.
//
// Build: clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include -I./bpf -c $< -o $@

#include "common.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char __license[] SEC("license") = "GPL";

// --- Safety event ---
struct loop_event {
    __u64 ts_ns;
    __u64 actual_interval;
    __u64 expected_ns;
    __u64 jitter_ns;
    __s32 pid;
    __u8  severity;
    __u8  pad[3];
};

// --- Ring buffer ---
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} loop_events SEC(".maps");

// --- Per-PID last check-in map ---
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __s32);      // pid
    __type(value, __u64);    // last timestamp
} pid_last_seen SEC(".maps");

// --- Safety config ---
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u64);
} safety_config SEC(".maps");

// --- Monitored PIDs (registered by Go collector) ---
// PIDs in this map skip the 64:1 subsampling, giving 100% detection
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __s32);      // pid
    __type(value, __u8);     // flag (1 = monitored)
} monitored_pids SEC(".maps");

// Shared counter for subsampling system-wide nanosleep events
static __u64 call_count = 0;

// Common logic: check interval jitter, emit event if threshold exceeded
static __always_inline int check_jitter(__s32 pid, __u64 now)
{
    __u64 *last = bpf_map_lookup_elem(&pid_last_seen, &pid);
    if (!last) {
        bpf_map_update_elem(&pid_last_seen, &pid, &now, BPF_ANY);
        return 0;
    }

    __u64 interval = now - *last;
    *last = now;

    // Read thresholds from safety_config map
    __u32 k0 = 0, k1 = 1, k2 = 2;
    __u64 *warn_thresh = bpf_map_lookup_elem(&safety_config, &k0);
    __u64 *crit_thresh = bpf_map_lookup_elem(&safety_config, &k1);
    __u64 *expected    = bpf_map_lookup_elem(&safety_config, &k2);

    if (!warn_thresh || !crit_thresh || !expected || *expected == 0)
        return 0;

    // Filter: only intervals roughly matching the control-loop period
    if (interval < (*expected) / 4 || interval > (*expected) * 10)
        return 0;

    __s64 diff = (__s64)interval - (__s64)(*expected);
    __u64 jitter = diff >= 0 ? (__u64)diff : (__u64)(-diff);

    __u8 severity = 0;
    if (jitter > *crit_thresh) severity = 2;
    else if (jitter > *warn_thresh) severity = 1;
    if (severity == 0) return 0;

    struct loop_event *evt = bpf_ringbuf_reserve(&loop_events, sizeof(*evt), 0);
    if (!evt) return 0;
    evt->ts_ns = now;
    evt->actual_interval = interval;
    evt->expected_ns = *expected;
    evt->jitter_ns = jitter;
    evt->pid = pid;
    evt->severity = severity;
    bpf_ringbuf_submit(evt, 0);
    return 0;
}

// Helper: check if PID is monitored, return 1 if monitored
static __always_inline int is_monitored(__s32 pid)
{
    __u8 *m = bpf_map_lookup_elem(&monitored_pids, &pid);
    return m && *m;
}

// tracepoint/syscalls/sys_enter_nanosleep
SEC("tracepoint/syscalls/sys_enter_nanosleep")
int tp_nanosleep(void *ctx)
{
    __s32 pid = (__s32)(bpf_get_current_pid_tgid() & 0xFFFFFFFF);

    // Monitored PIDs skip subsampling entirely
    if (!is_monitored(pid)) {
        call_count++;
        if ((call_count & 0x3F) != 0)
            return 0;
    }

    __u64 now = bpf_ktime_get_ns();
    return check_jitter(pid, now);
}

// tracepoint/syscalls/sys_enter_clock_nanosleep
SEC("tracepoint/syscalls/sys_enter_clock_nanosleep")
int tp_clock_nanosleep(void *ctx)
{
    __s32 pid = (__s32)(bpf_get_current_pid_tgid() & 0xFFFFFFFF);

    if (!is_monitored(pid)) {
        call_count++;
        if ((call_count & 0x3F) != 0)
            return 0;
    }

    __u64 now = bpf_ktime_get_ns();
    return check_jitter(pid, now);
}
