// Common type definitions for eBPF Robot Safety Monitor
// Provides stubs for kernel types without requiring vmlinux.h

#ifndef __COMMON_H__
#define __COMMON_H__

typedef signed char __s8;
typedef signed short __s16;
typedef signed int __s32;
typedef signed long long __s64;
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long __u64;

typedef __u32 __be32;
typedef __u16 __be16;
typedef __u64 __be64;
typedef __u32 __wsum;
typedef __u16 __sum16;

// BPF map types
enum bpf_map_type {
    BPF_MAP_TYPE_HASH    = 1,
    BPF_MAP_TYPE_ARRAY   = 2,
    BPF_MAP_TYPE_RINGBUF = 27,
};

#define BPF_ANY 0

// Forward declarations for kernel types
struct __sk_buff;
struct tcphdr; struct iphdr; struct ipv6hdr;
struct xdp_md; struct bpf_sock; struct bpf_sock_addr; struct bpf_sock_ops;
struct msghdr; struct sk_buff;
struct hrtimer;
struct tty_struct;

// ktime_t is s64
typedef __s64 ktime_t;

// pt_regs for x86_64 kprobe
struct pt_regs {
    unsigned long r15, r14, r13, r12, rbp, rbx;
    unsigned long r11, r10, r9, r8;
    unsigned long rax, rcx, rdx, rsi, rdi;
    unsigned long orig_rax, ip, cs, flags, sp, ss;
};

#endif
