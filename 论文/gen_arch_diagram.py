#!/usr/bin/env python3
"""Generate system architecture diagram for the eBPF robot safety paper."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import numpy as np

fig, ax = plt.subplots(1, 1, figsize=(8, 6))
ax.set_xlim(0, 10)
ax.set_ylim(0, 10)
ax.axis('off')

# Color scheme
c_stm32 = '#E8F5E9'     # light green
c_stm32_border = '#2E7D32'
c_linux = '#E3F2FD'      # light blue
c_linux_border = '#1565C0'
c_ebpf = '#FFF3E0'       # light orange
c_ebpf_border = '#E65100'
c_danger = '#FFCDD2'
c_warn = '#FFF9C4'
c_normal = '#C8E6C9'

def draw_box(ax, x, y, w, h, text, color, border, fontsize=8, bold=False):
    """Draw a rounded box with text."""
    box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.3",
                         facecolor=color, edgecolor=border, linewidth=1.5)
    ax.add_patch(box)
    weight = 'bold' if bold else 'normal'
    ax.text(x + w/2, y + h/2, text, ha='center', va='center',
            fontsize=fontsize, fontweight=weight, fontfamily='sans-serif')

def draw_arrow(ax, x1, y1, x2, y2, color='gray', style='->', lw=1.2):
    """Draw an arrow."""
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle=style, color=color, lw=lw,
                                connectionstyle="arc3,rad=0"))

def draw_label(ax, x, y, text, fontsize=7, color='black', rotation=0):
    ax.text(x, y, text, ha='center', va='center', fontsize=fontsize,
            color=color, rotation=rotation, fontfamily='sans-serif')

# ============================================================
# Layer labels
# ============================================================
ax.text(0.2, 8.5, 'Layer 3: Kernel Observation (eBPF)', fontsize=10, fontweight='bold',
        color=c_ebpf_border, fontfamily='sans-serif')
ax.text(0.2, 5.2, 'Layer 2: Soft Real-Time (Linux SBC)', fontsize=10, fontweight='bold',
        color=c_linux_border, fontfamily='sans-serif')
ax.text(0.2, 1.8, 'Layer 1: Hard Real-Time (STM32)', fontsize=10, fontweight='bold',
        color=c_stm32_border, fontfamily='sans-serif')

# Separator lines
for y in [7.2, 4.2]:
    ax.plot([0.3, 9.7], [y, y], '--', color='gray', alpha=0.4, lw=0.8)

# ============================================================
# Layer 3: eBPF probes
# ============================================================
draw_box(ax, 0.5, 7.5, 2.6, 1.0, 'loop_monitor\n(tracepoint/nanosleep\n& clock_nanosleep)',
         c_ebpf, c_ebpf_border, 7)
draw_box(ax, 3.5, 7.5, 2.6, 1.0, 'serial_monitor\n(kprobe/tty_write\n& tty_read)',
         c_ebpf, c_ebpf_border, 7)
draw_box(ax, 6.5, 7.5, 2.6, 1.0, 'sched_monitor\n(tracepoint/sched_switch\n& sched_wakeup)',
         c_ebpf, c_ebpf_border, 7)

# Ring buffers (small boxes)
for i, name in enumerate(['loop\nevents', 'serial\nevents', 'sched\nevents']):
    draw_box(ax, 1.0 + i*3, 6.8, 1.6, 0.6, name, '#FFECB3', '#FF8F00', 6)

# Arrows: probes -> ring buffers
for i in range(3):
    draw_arrow(ax, 1.8 + i*3, 7.5, 1.8 + i*3, 7.1, c_ebpf_border, '->', 1.0)

# ============================================================
# Layer 2: Linux
# ============================================================
draw_box(ax, 1.0, 4.5, 2.8, 1.4, 'Go Collector\nBPF Loader + Ring Buffer Consumer\nREST API (9 endpoints, :8090)\nSafety Monitor (500ms poll)',
         c_linux, c_linux_border, 6.5)
draw_box(ax, 4.5, 4.5, 2.8, 1.4, 'Python Control Node\nrobot_control.py (100Hz)\npyserial (460800bps)\nSafety Polling Thread (200ms)',
         c_linux, c_linux_border, 6.5)
draw_box(ax, 8.0, 4.5, 1.5, 1.4, 'Dashboard\n(index.html)\nHTTP :8090',
         '#E8EAF6', '#283593', 6.5)

# Arrows: ring buffers -> Go collector
draw_arrow(ax, 1.8, 6.8, 2.4, 5.6, '#FF8F00', '->', 1.0)

# Arrows: Go collector -> Python (HTTP API)
draw_arrow(ax, 3.8, 5.2, 4.5, 5.2, '#1565C0', '<->', 1.2)

# Arrows: Python -> Dashboard
draw_arrow(ax, 7.3, 5.2, 8.0, 5.2, '#283593', '->', 1.0)

# ============================================================
# Layer 1: STM32
# ============================================================
draw_box(ax, 0.8, 0.5, 2.0, 1.5, 'MPU6050\nI2C 100Hz\nAcquire Task\n(Prio 4)',
         c_stm32, c_stm32_border, 6.5)
draw_box(ax, 3.2, 0.5, 2.0, 1.5, 'Madgwick Fusion\n+ PID Heading\nControl Task\n(Prio 4)',
         c_stm32, c_stm32_border, 6.5)
draw_box(ax, 5.6, 0.5, 2.0, 1.5, 'UART 460800bps\n0xBADD Telemetry\nComm Task\n(Prio 3)',
         c_stm32, c_stm32_border, 6.5)
draw_box(ax, 8.0, 0.5, 1.5, 1.5, 'Motor PWM\nTIM3 10kHz\n+ IWDG\n+ TaskWDT',
         '#FFCCBC', '#BF360C', 6.5)

# Arrows: Acquire -> Control
draw_arrow(ax, 2.8, 1.0, 3.2, 1.0, '#2E7D32', '->', 1.0)
# Arrows: Control -> Comm
draw_arrow(ax, 5.2, 1.0, 5.6, 1.0, '#2E7D32', '->', 1.0)
# Arrows: Control -> Motor
draw_arrow(ax, 4.2, 0.5, 8.0, 0.5, '#BF360C', '->', 1.0)

# ============================================================
# Cross-layer arrows: ESTOP Paths
# ============================================================

# Path A: Go -> Python (ESTOP via REST)
ax.annotate('', xy=(5.9, 4.5), xytext=(3.8, 6.2),
            arrowprops=dict(arrowstyle='->', color='red', lw=2.0, ls='dashed',
                           connectionstyle="arc3,rad=-0.3"))
draw_label(ax, 4.5, 5.8, 'PATH A: ESTOP\n(HTTP POST)', 6, 'red')

# Path B: Python -> STM32 (ESTOP via Serial)
ax.annotate('', xy=(5.9, 2.0), xytext=(5.9, 4.5),
            arrowprops=dict(arrowstyle='->', color='red', lw=2.0, ls='dashed'))
draw_label(ax, 6.2, 3.0, 'PATH B:\nESTOP\n(UART)', 6, 'red')

# Serial link: Python <-> STM32
ax.annotate('', xy=(5.9, 2.0), xytext=(5.9, 4.5),
            arrowprops=dict(arrowstyle='<->', color='#1565C0', lw=1.5))
draw_label(ax, 6.8, 3.0, 'Telemetry\n0xBADD\n100fps', 6, '#1565C0')

# ============================================================
# ESTOP Status Indicators
# ============================================================
draw_box(ax, 0.5, 9.5, 1.8, 0.4, 'NOMINAL', c_normal, '#2E7D32', 7)
draw_box(ax, 2.6, 9.5, 1.8, 0.4, 'WARNING (>500μs)', c_warn, '#F9A825', 7)
draw_box(ax, 4.7, 9.5, 1.8, 0.4, 'CRITICAL (>2000μs)', c_danger, '#C62828', 7)

# Annotation: Allan-variance calibrated thresholds
draw_label(ax, 6.0, 10.3, '* All thresholds derived from MPU6050 Allan variance + Monte Carlo validation',
           6.5, 'gray')

# ============================================================
# Legend / Key metrics
# ============================================================
metrics_text = (
    "Key Metrics:\n"
    "  Control: 100Hz (10ms period), jitter WARNING 500μs, CRITICAL 2000μs\n"
    "  Serial:  460800bps, 32B/frame, CRC16-CCITT, 25.6kbps (5.6% util)\n"
    "  ESTOP:   Dual-path, mean 3.20ms (P95=4.29ms), 100% success (50/50)\n"
    "  Overhead: CPU <0.02%, kernel memory <1.1MB, Go RSS ~14MB"
)
ax.text(0.5, -0.8, metrics_text, fontsize=7, fontfamily='monospace',
        color='#424242', verticalalignment='top')

plt.tight_layout()

# Save
import os
out_dir = "C:/Users/xing2/Desktop/ebpf-robot-safety/论文/paper/figures"
os.makedirs(out_dir, exist_ok=True)
fig.savefig(os.path.join(out_dir, "system_architecture.pdf"), dpi=300, bbox_inches='tight')
fig.savefig(os.path.join(out_dir, "system_architecture.png"), dpi=300, bbox_inches='tight')
print("Architecture diagram saved.")
