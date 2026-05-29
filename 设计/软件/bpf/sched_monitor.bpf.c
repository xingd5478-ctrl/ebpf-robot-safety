// sched_monitor.bpf.c — ROS2 Node Scheduling Latency Monitor (v2)
//
// Uses tracepoint/sched/sched_wakeup to detect when a monitored
// ROS2 process is woken up, and sched_switch to detect when it
// actually gets CPU time. The difference = scheduling latency.
//
// This version uses proper BPF_PROG macros to safely access
// tracepoint fields without raw pointer arithmetic.

#include "common.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char __license[] SEC("license") = "GPL";

// --- Event pushed to userspace ---
struct sched_event {
    __u64 ts_ns;
    __s32 pid;
    __u64 wait_ns;          // time between wakeup and actual execution
    __u64 runtime_ns;       // how long this task ran
    __u32 prio;
    char  comm[16];
};

// --- Ring buffer ---
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} sched_events SEC(".maps");

// --- Per-PID: when was this task last woken up ---
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __s32);       // pid
    __type(value, __u64);     // wakeup timestamp ns
} wakeup_map SEC(".maps");

// --- Per-PID: when was this task last scheduled ON ---
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __s32);       // pid
    __type(value, __u64);     // last_switched_in_ns
} switch_map SEC(".maps");

// --- Monitored PIDs (populated by userspace) ---
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, __s32);      // pid
    __type(value, __u8);     // 1 = monitor
} monitored_pids SEC(".maps");

// tracepoint/sched/sched_wakeup — fires when a task is woken up
SEC("tracepoint/sched/sched_wakeup")
int tp_wakeup(void *ctx)
{
    __s32 pid;
    // sched_wakeup tracepoint format:
    // field:comm;  offset:8;  size:16
    // field:pid;   offset:24; size:4

    // Use bpf_probe_read_kernel with proper kernel pointee
    if (bpf_probe_read_kernel(&pid, sizeof(pid), (void *)((long)ctx + 24)) < 0)
        return 0;

    __u8 *mon = bpf_map_lookup_elem(&monitored_pids, &pid);
    if (!mon)
        return 0;

    __u64 now = bpf_ktime_get_ns();
    bpf_map_update_elem(&wakeup_map, &pid, &now, BPF_ANY);
    return 0;
}

// tracepoint/sched/sched_switch — record runtime + compute wait time
SEC("tracepoint/sched/sched_switch")
int tp_sched_switch(void *ctx)
{
    __s32 prev_pid, next_pid;
    char next_comm[16];

    // sched_switch tracepoint format:
    // field:prev_comm;   offset:8;  size:16
    // field:prev_pid;    offset:24; size:4
    // field:prev_prio;   offset:28; size:4
    // field:prev_state;  offset:32; size:8
    // field:next_comm;   offset:40; size:16
    // field:next_pid;    offset:56; size:4
    // field:next_prio;   offset:60; size:4

    if (bpf_probe_read_kernel(&next_pid, sizeof(next_pid), (void *)((long)ctx + 56)) < 0)
        return 0;
    if (bpf_probe_read_kernel_str(&next_comm, sizeof(next_comm), (void *)((long)ctx + 40)) < 0)
        return 0;
    if (bpf_probe_read_kernel(&prev_pid, sizeof(prev_pid), (void *)((long)ctx + 24)) < 0)
        return 0;

    __u64 now = bpf_ktime_get_ns();

    // Check if NEXT task is monitored — compute its wait time
    __u8 *next_mon = bpf_map_lookup_elem(&monitored_pids, &next_pid);
    if (next_mon) {
        __u64 *wakeup_ns = bpf_map_lookup_elem(&wakeup_map, &next_pid);
        __u64 wait_ns = 0;
        if (wakeup_ns) {
            wait_ns = now - *wakeup_ns;
            *wakeup_ns = now;
        }

        // Update switch time
        bpf_map_update_elem(&switch_map, &next_pid, &now, BPF_ANY);

        if (wait_ns > 5000000) { // Only report wait > 5ms
            struct sched_event *evt = bpf_ringbuf_reserve(&sched_events, sizeof(*evt), 0);
            if (evt) {
                evt->ts_ns = now;
                evt->pid = next_pid;
                evt->wait_ns = wait_ns;
                evt->runtime_ns = 0;
                __builtin_memcpy(evt->comm, next_comm, 16);
                bpf_ringbuf_submit(evt, 0);
            }
        }
    }

    // Check if PREV task is monitored — compute its runtime
    __u8 *prev_mon = bpf_map_lookup_elem(&monitored_pids, &prev_pid);
    if (prev_mon) {
        __u64 *last_switched = bpf_map_lookup_elem(&switch_map, &prev_pid);
        if (last_switched) {
            __u64 runtime = now - *last_switched;
            if (runtime > 100000000) { // runtime > 100ms
                struct sched_event *evt = bpf_ringbuf_reserve(&sched_events, sizeof(*evt), 0);
                if (evt) {
                    evt->ts_ns = now;
                    evt->pid = prev_pid;
                    evt->wait_ns = 0;
                    evt->runtime_ns = runtime;
                    __builtin_memcpy(evt->comm, next_comm, 16);
                    bpf_ringbuf_submit(evt, 0);
                }
            }
        }
    }

    return 0;
}
