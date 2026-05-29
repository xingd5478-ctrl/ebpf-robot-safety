#!/usr/bin/env python3
"""
demo_control.py — Mock Robot Control Node for eBPF Safety Monitor Testing

Simulates a real robot control pipeline:
  1. Read IMU sensor data (simulated or real serial)
  2. Run PID control law
  3. Dispatch motor commands

Generates artificial control loop jitter to test the eBPF loop_monitor probe.

Usage:
  python3 demo_control.py                 # 100Hz control, simulated IMU
  python3 demo_control.py --serial COM16  # 100Hz control, real STM32 on COM16
  python3 demo_control.py --fault 5       # Inject fault every 5s (2x period jitter)
"""

import time
import math
import random
import argparse
import signal
import sys

class MockRobotController:
    """Simulates a ROS2-style control node with a periodic timer callback."""

    def __init__(self, freq_hz=100, inject_fault_interval=0):
        self.period_ns = int(1e9 / freq_hz)  # 10ms for 100Hz
        self.inject_fault = inject_fault_interval
        self.cycle_count = 0
        self.running = True

        # Simulated robot state
        self.angle_pitch = 0.0
        self.angle_roll  = 0.0
        self.angle_yaw   = 0.0
        self.motor_left  = 0.0
        self.motor_right = 0.0

        # PID gains (simplified balancing robot)
        self.kp = 2.5
        self.kd = 0.3

        signal.signal(signal.SIGINT, self._on_signal)
        signal.signal(signal.SIGTERM, self._on_signal)

    def _on_signal(self, signum, frame):
        print("\n[control] Shutdown signal received")
        self.running = False

    def read_imu(self):
        """Simulate IMU reading — in real system this reads from STM32 serial."""
        # Generate realistic noise (matching MPU6050 Allan variance characteristics)
        self.angle_pitch += random.gauss(0, 0.02)  # ARW noise
        self.angle_roll  += random.gauss(0, 0.02)
        self.angle_yaw   += random.gauss(0, 0.05)  # Yaw has higher noise (no magnetometer)
        # Drift
        self.angle_yaw   += 0.001  # 0.001 deg/sample = 0.1 deg/s drift
        return self.angle_pitch, self.angle_roll, self.angle_yaw

    def control_law(self, pitch, roll, yaw):
        """Simple PD balancing control."""
        # Target: upright (0 pitch, 0 roll)
        self.motor_left  = -(self.kp * pitch + self.kd * (pitch - getattr(self, '_last_pitch', pitch)))
        self.motor_right = -(self.kp * roll  + self.kd * (roll  - getattr(self, '_last_roll', roll)))
        self._last_pitch = pitch
        self._last_roll  = roll

    def run(self):
        print(f"[control] Starting at {int(1e9/self.period_ns)}Hz (period={self.period_ns/1e6:.1f}ms)")
        print(f"[control] Fault injection: {'every ' + str(self.inject_fault) + 's' if self.inject_fault else 'disabled'}")
        print("[control] Press Ctrl+C to stop")

        last_time = time.monotonic_ns()

        while self.running:
            self.cycle_count += 1

            # --- Fault injection: artificially delay this cycle ---
            if self.inject_fault > 0 and self.cycle_count % (self.inject_fault * 100) == 0:
                delay_s = random.uniform(0.003, 0.008)  # 3-8ms extra delay
                print(f"[FAULT] Injecting {delay_s*1e3:.1f}ms jitter at cycle {self.cycle_count}")
                time.sleep(delay_s)

            # --- Control cycle ---
            pitch, roll, yaw = self.read_imu()
            self.control_law(pitch, roll, yaw)

            # --- Maintain fixed period ---
            elapsed = time.monotonic_ns() - last_time
            sleep_ns = self.period_ns - elapsed
            if sleep_ns > 0:
                time.sleep(sleep_ns / 1e9)
            elif sleep_ns < -self.period_ns:
                # Missed a full cycle
                print(f"[WARN] Cycle {self.cycle_count}: overrun by {-sleep_ns/1e6:.1f}ms")

            last_time = time.monotonic_ns()

            # Periodic status
            if self.cycle_count % 500 == 0:
                print(f"[control] cycle={self.cycle_count:6d}  "
                      f"pitch={pitch:+.3f}  roll={roll:+.3f}  yaw={yaw:+.3f}  "
                      f"motor_L={self.motor_left:+.2f}  motor_R={self.motor_right:+.2f}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Mock Robot Controller')
    parser.add_argument('--freq', type=int, default=100, help='Control loop frequency (Hz)')
    parser.add_argument('--fault', type=int, default=0, help='Inject timing fault every N seconds (0=off)')
    args = parser.parse_args()

    ctrl = MockRobotController(freq_hz=args.freq, inject_fault_interval=args.fault)
    ctrl.run()
    print(f"[control] Stopped after {ctrl.cycle_count} cycles")
