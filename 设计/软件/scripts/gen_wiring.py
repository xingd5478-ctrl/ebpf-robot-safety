#!/usr/bin/env python3
"""Generate STM32 wiring diagram for the eBPF robot safety project."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Circle
import numpy as np

fig, ax = plt.subplots(1, 1, figsize=(12, 8))
ax.set_xlim(0, 14)
ax.set_ylim(0, 12)
ax.axis('off')

# Colors
c_stm32 = '#E3F2FD'
c_stm32_b = '#1565C0'
c_sensor = '#E8F5E9'
c_sensor_b = '#2E7D32'
c_motor = '#FFF3E0'
c_motor_b = '#E65100'
c_serial = '#F3E5F5'
c_serial_b = '#7B1FA2'
c_power = '#FFCDD2'
c_power_b = '#C62828'

def draw_box(ax, x, y, w, h, text, color, border, fontsize=8, bold=False):
    box = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.3",
                         facecolor=color, edgecolor=border, linewidth=2)
    ax.add_patch(box)
    weight = 'bold' if bold else 'normal'
    ax.text(x + w/2, y + h/2, text, ha='center', va='center',
            fontsize=fontsize, fontweight=weight, fontfamily='monospace')

def draw_label(ax, x, y, text, fontsize=8, color='black', ha='center', weight='normal'):
    ax.text(x, y, text, ha=ha, va='center', fontsize=fontsize,
            color=color, fontweight=weight, fontfamily='sans-serif')

def draw_wire(ax, x1, y1, x2, y2, color='#424242', lw=1.5, ls='-'):
    ax.plot([x1, x2], [y1, y2], color=color, lw=lw, linestyle=ls)

def draw_pin(ax, x, y, label, side='left', color='#333'):
    """Draw a pin marker"""
    ax.plot(x, y, 'o', color=color, markersize=5, zorder=5)
    ha = 'right' if side == 'left' else 'left'
    dx = -0.15 if side == 'left' else 0.15
    draw_label(ax, x+dx, y, label, 7, color, ha)

# ============================================================
# Title
# ============================================================
ax.text(7, 11.5, 'STM32F103C8T6 机器人控制系统接线图', ha='center', fontsize=14,
        fontweight='bold', fontfamily='sans-serif')
ax.text(7, 11.0, 'eBPF Robot Safety Monitor — Hardware Wiring Reference', ha='center',
        fontsize=9, color='#666', fontfamily='sans-serif')

# ============================================================
# STM32 chip (center)
# ============================================================
draw_box(ax, 5.0, 4.5, 4.0, 4.5,
         'STM32F103C8T6\n\nI2C1: PB6(SCL) PB7(SDA)\n'
         'USART1: PA9(TX) PA10(RX)\n'
         'TIM3_CH1: PA6 PWM\nTIM3_CH2: PA7 PWM\n'
         'DIR: PB12-15\nIWDG: LSI 40kHz',
         c_stm32, c_stm32_b, 8, bold=True)

# ============================================================
# Left side: MPU6050
# ============================================================
draw_box(ax, 0.3, 5.5, 2.8, 2.5,
         'MPU6050 6-Axis IMU\n\nVCC → 3.3V\nGND → GND\n'
         'SCL → PB6\nSDA → PB7\nINT → NC',
         c_sensor, c_sensor_b, 8)

# Wires: MPU6050 → STM32
draw_wire(ax, 3.1, 7.2, 5.0, 7.5, '#2E7D32', 1.5)
draw_wire(ax, 3.1, 6.8, 5.0, 7.0, '#2E7D32', 1.5)
draw_label(ax, 4.0, 7.6, 'SCL', 7, '#2E7D32')
draw_label(ax, 4.0, 6.5, 'SDA', 7, '#2E7D32')

# I2C pullup note
draw_label(ax, 1.7, 5.2, '* 通常模块自带 4.7kΩ 上拉电阻', 6.5, '#888')

# ============================================================
# Right side: Motor Driver
# ============================================================
draw_box(ax, 10.5, 4.5, 3.2, 4.5,
         '电机驱动板\n(TB6612 / L298N)\n\n'
         'PWMA  ← PA6\nPWMB  ← PA7\n'
         'AIN1  ← PB12\nAIN2  ← PB13\n'
         'BIN1  ← PB14\nBIN2  ← PB15\n'
         'VM → 电池 7.4V\nVCC → 5V',
         c_motor, c_motor_b, 8)

# Motor wires
draw_wire(ax, 9.0, 7.8, 10.5, 7.8, '#E65100', 1.2)
draw_wire(ax, 9.0, 7.3, 10.5, 7.3, '#E65100', 1.2)
draw_wire(ax, 9.0, 6.8, 10.5, 6.8, '#E65100', 1.2)
draw_wire(ax, 9.0, 6.3, 10.5, 6.3, '#E65100', 1.2)
draw_wire(ax, 9.0, 5.8, 10.5, 5.8, '#E65100', 1.2)
draw_wire(ax, 9.0, 5.3, 10.5, 5.3, '#E65100', 1.2)

# Motors (below driver)
draw_box(ax, 10.5, 1.8, 1.4, 1.2, 'M1\n(左前)', '#FFCCBC', '#BF360C', 7)  # corrected: LeftFront
draw_box(ax, 12.2, 1.8, 1.4, 1.2, 'M2\n(右前)', '#FFCCBC', '#BF360C', 7)  # corrected: RightFront

draw_wire(ax, 11.2, 4.5, 11.2, 3.0, '#BF360C', 1.0)
draw_wire(ax, 12.9, 4.5, 12.9, 3.0, '#BF360C', 1.0)

# ============================================================
# Top: USB-UART
# ============================================================
draw_box(ax, 1.5, 9.0, 3.5, 2.0,
         'USB-UART (FT232RL)\n\nTXD → PA10 (RX)\nRXD ← PA9  (TX)\nGND ↔ GND',
         c_serial, c_serial_b, 8)

# Wires: UART → STM32
draw_wire(ax, 3.5, 10.2, 5.0, 8.5, '#7B1FA2', 1.5)
draw_wire(ax, 3.5, 9.7, 5.0, 8.0, '#7B1FA2', 1.5)
draw_label(ax, 4.2, 10.5, 'PA10 RX', 7, '#7B1FA2')
draw_label(ax, 4.2, 7.7, 'PA9 TX', 7, '#7B1FA2')

# USB to PC label
draw_label(ax, 1.0, 10.2, 'USB → PC/Linux SBC', 8, '#7B1FA2', 'center', 'bold')

# ============================================================
# Power
# ============================================================
draw_box(ax, 0.3, 1.5, 3.0, 2.5,
         '电源\n\n电池 7.4V Li-Po\n↓ 降压模块\n5V → STM32 / 驱动板\n3.3V → MPU6050',
         c_power, c_power_b, 8)

# ============================================================
# Pin mapping table (right side, below driver)
# ============================================================
draw_label(ax, 11.2, 0.8, 'STM32 引脚速查', 8, '#333', 'center', 'bold')

pin_data = [
    ('PA6', 'TIM3_CH1', '左轮 PWM'),
    ('PA7', 'TIM3_CH2', '右轮 PWM'),
    ('PA9', 'USART1_TX', '串口发送'),
    ('PA10', 'USART1_RX', '串口接收'),
    ('PB6', 'I2C1_SCL', 'MPU6050 时钟'),
    ('PB7', 'I2C1_SDA', 'MPU6050 数据'),
    ('PB12', 'GPIO', '左轮 IN1'),
    ('PB13', 'GPIO', '左轮 IN2'),
    ('PB14', 'GPIO', '右轮 IN1'),
    ('PB15', 'GPIO', '右轮 IN2'),
]

for i, (pin, func, desc) in enumerate(pin_data):
    y = 0.5 - i * 0.35
    draw_label(ax, 8.0, y, f'{pin}', 6.5, '#1565C0', 'right', 'bold')
    draw_label(ax, 9.0, y, func, 6.5, '#333', 'center')
    draw_label(ax, 10.3, y, desc, 6.5, '#555', 'left')

# ============================================================
# Notes
# ============================================================
notes = [
    '连接前务必断电！检查 VCC-GND 无短路后再上电',
    'MPU6050 模块通常已集成 4.7kΩ I2C 上拉电阻，无需外加',
    '电机驱动板 VM 接电池 7.4V，VCC 接 STM32 5V（或独立 5V）',
    'USB-UART 的 3.3V 不要接！STM32 由电池供电，避免电压冲突',
    '波特率 460800bps，CRC16-CCITT 校验',
    '独立看门狗 IWDG 约 16s 超时，固件已使能',
]
for i, note in enumerate(notes):
    draw_label(ax, 0.5, 0.5 - i*0.35, f'[{i+1}] {note}', 6.5, '#666', 'left')

# ============================================================
# ESTOP path highlight
# ============================================================
ax.annotate('ESTOP CMD\n(ASCII "ESTOP\\r\\n")',
            xy=(5.0, 8.5), xytext=(7.5, 11.5),
            arrowprops=dict(arrowstyle='->', color='red', lw=2, ls='--',
                           connectionstyle="arc3,rad=-0.3"),
            fontsize=7, color='red', fontweight='bold', ha='center')

plt.tight_layout()

# Save
out = "C:/Users/xing2/Desktop/ebpf-robot-safety/ebpf-robot-safety/设计说明/docs/wiring_diagram.pdf"
import os
os.makedirs(os.path.dirname(out), exist_ok=True)
fig.savefig(out, dpi=200, bbox_inches='tight')
# Also PNG
fig.savefig(out.replace('.pdf', '.png'), dpi=200, bbox_inches='tight')
print(f"Wiring diagram saved to: {out}")
print(f"PNG: {out.replace('.pdf', '.png')}")
