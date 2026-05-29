// serial_monitor.bpf.c — Sensor/Actuator Communication Latency Monitor
//
// Hooks tty_write() and tty_read() to measure UART/USB serial latency
// between the Linux SBC and the STM32 microcontroller.
// Detects:
//   - Serial write latency spikes (command not reaching STM32 in time)
//   - Serial read stalls (sensor data not arriving)
//   - Buffer overflow risk (write burst exceeding UART bandwidth)
//
// Analogy to STM32 firmware: your UART CRC + ACK + sequence number monitoring,
// but implemented at the kernel level without touching application code.

#include "common.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

char __license[] SEC("license") = "GPL";

// --- Serial communication event ---
struct serial_event {
    __u64 ts_ns;
    __u32 bytes;          // bytes written/read
    __u16 tty_index;      // which TTY device
    __u8  dir;            // 0=TX (SBC->STM32 cmd), 1=RX (STM32->SBC sensor)
    __u8  severity;       // 0=normal 1=latency_warn 2=stall 3=buffer_risk
    __u32 latency_us;     // time since last write (for TX burst detection)
};

// --- Ring buffer ---
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} serial_events SEC(".maps");

// --- Per-TTY write tracking ---
struct tty_stats {
    __u64 last_write_ns;
    __u64 bytes_written;
    __u64 bytes_read;
    __u64 write_count;
    __u64 read_count;
    __u64 last_read_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16);
    __type(key, __u32);     // tty index
    __type(value, struct tty_stats);
} tty_map SEC(".maps");

// --- Threshold config ---
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u64);
} serial_config SEC(".maps");

// kprobe on tty_write() — monitor SBC → STM32 commands
SEC("kprobe/tty_write")
int BPF_KPROBE(kp_tty_write, struct tty_struct *tty,
               const unsigned char *buf, int count)
{
    if (count <= 0)
        return 0;

    __u64 now = bpf_ktime_get_ns();

    // Use address of tty struct as approximate index
    __u32 idx = (__u32)((unsigned long)tty & 0xFFFF);

    struct tty_stats *st = bpf_map_lookup_elem(&tty_map, &idx);
    if (!st) {
        struct tty_stats new_st = {0};
        new_st.last_write_ns = now;
        new_st.bytes_written = count;
        new_st.write_count   = 1;
        bpf_map_update_elem(&tty_map, &idx, &new_st, BPF_ANY);
        return 0;
    }

    st->bytes_written += count;
    st->write_count++;
    __u64 gap_ns = now - st->last_write_ns;
    st->last_write_ns = now;

    // Detect burst: write gap < 100us suggests buffer overflow risk
    __u8 severity = 0;
    if (gap_ns > 50000000) // >50ms gap → possible stall before this write
        severity = 2;
    else if (gap_ns < 100000 && count > 64) // <100us gap + large write → burst risk
        severity = 3;

    if (severity == 0)
        return 0;

    struct serial_event *evt = bpf_ringbuf_reserve(&serial_events, sizeof(*evt), 0);
    if (!evt)
        return 0;

    evt->ts_ns      = now;
    evt->bytes      = count;
    evt->tty_index  = idx;
    evt->dir        = 0; // TX
    evt->severity   = severity;
    evt->latency_us = (__u32)(gap_ns / 1000);

    bpf_ringbuf_submit(evt, 0);
    return 0;
}

// kprobe on tty_read() — monitor STM32 → SBC sensor data
SEC("kprobe/tty_read")
int BPF_KPROBE(kp_tty_read, struct tty_struct *tty,
               unsigned char *buf, int count)
{
    if (count <= 0)
        return 0;

    __u64 now = bpf_ktime_get_ns();
    __u32 idx = (__u32)((unsigned long)tty & 0xFFFF);

    struct tty_stats *st = bpf_map_lookup_elem(&tty_map, &idx);
    if (!st) {
        struct tty_stats new_st = {0};
        new_st.last_read_ns = now;
        new_st.bytes_read   = count;
        new_st.read_count   = 1;
        bpf_map_update_elem(&tty_map, &idx, &new_st, BPF_ANY);
        return 0;
    }

    st->bytes_read += count;
    st->read_count++;

    // Detect sensor data stall: >100ms since last read
    __u64 gap_ns = now - st->last_read_ns;
    st->last_read_ns = now;

    __u8 severity = 0;
    if (gap_ns > 100000000) // >100ms → sensor stall
        severity = 2;
    else if (gap_ns > 50000000) // >50ms → latency warn
        severity = 1;

    if (severity == 0)
        return 0;

    struct serial_event *evt = bpf_ringbuf_reserve(&serial_events, sizeof(*evt), 0);
    if (!evt)
        return 0;

    evt->ts_ns      = now;
    evt->bytes      = count;
    evt->tty_index  = idx;
    evt->dir        = 1; // RX
    evt->severity   = severity;
    evt->latency_us = (__u32)(gap_ns / 1000);

    bpf_ringbuf_submit(evt, 0);
    return 0;
}
