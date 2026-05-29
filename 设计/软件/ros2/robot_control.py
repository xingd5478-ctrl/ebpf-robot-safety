#!/usr/bin/env python3
"""
robot_control.py — Real Robot Control Node with eBPF Safety Monitor

Replaces demo_control.py. Connects to STM32 via UART, receives telemetry
frames at 100Hz, and sends motor commands. The eBPF loop_monitor captures
nanosleep() calls in this process to detect control loop jitter.

Architecture:
  Linux (this node)  ←→  UART (460800 bps)  ←→  STM32F103
    │                                              │
    │  TX: "FWD 400\r\n"                            │
    │  TX: "HEAD 90\r\n"                            │
    │  RX: 32-byte telemetry frame (0xBADD)         │
    │                                              │  MPU6050 @ 100Hz
    │  eBPF probes monitor:                        │  Madgwick fusion
    │    - nanosleep() period jitter               │  PID heading control
    │    - tty_write/read UART latency             │  Motor PWM output
    │    - sched_switch scheduling delay           │

Usage:
  python3 robot_control.py
  python3 robot_control.py --serial COM16
  python3 robot_control.py --serial /dev/ttyUSB0 --freq 100 --test
"""

import time
import os
import struct
import signal
import sys
import argparse
import threading
import json
import urllib.request

try:
    import serial
except ImportError:
    serial = None  # lazy check in RobotController.__init__


# ============================================================
#  CRC16 (same algorithm as STM32 firmware)
# ============================================================
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
    return crc & 0xFFFF


# ============================================================
#  Telemetry Frame Parser
# ============================================================
class TelemetryFrame:
    """Parsed 32-byte telemetry frame from STM32 (type 0xBADD)."""

    # Command ID mapping for confirmation
    CMD_NAMES = {0: 'NONE', 1: 'STOP', 2: 'FWD', 3: 'BACK',
                 4: 'LEFT', 5: 'RIGHT', 6: 'VEL', 7: 'ESTOP', 8: 'HEAD'}

    __slots__ = (
        'seq', 'yaw_deg', 'target_yaw_deg', 'motor_left', 'motor_right',
        'ax', 'ay', 'az', 'gx', 'gy', 'gz',
        'emergency_stop', 'heading_mode', 'last_cmd_id', 'last_cmd_name',
        'jitter_us', 'missed_cycles', 'timestamp',
    )

    @classmethod
    def parse(cls, data: bytes) -> 'TelemetryFrame | None':
        if len(data) < 32:
            return None
        if data[0] != 0xBA or data[1] != 0xDD:
            return None

        # Verify CRC over bytes 0-29
        calc_crc = crc16(data[:30])
        rx_crc = struct.unpack_from('<H', data, 30)[0]
        if calc_crc != rx_crc:
            return None

        frame = cls()
        frame.seq      = data[2]
        frame.yaw_deg  = struct.unpack_from('<h', data, 3)[0]  / 10.0
        frame.target_yaw_deg = struct.unpack_from('<h', data, 5)[0] / 10.0
        frame.motor_left     = struct.unpack_from('<h', data, 7)[0]
        frame.motor_right    = struct.unpack_from('<h', data, 9)[0]
        frame.ax = struct.unpack_from('<h', data, 11)[0]
        frame.ay = struct.unpack_from('<h', data, 13)[0]
        frame.az = struct.unpack_from('<h', data, 15)[0]
        frame.gx = struct.unpack_from('<h', data, 17)[0]
        frame.gy = struct.unpack_from('<h', data, 19)[0]
        frame.gz = struct.unpack_from('<h', data, 21)[0]
        frame.emergency_stop = bool(data[23])
        frame.heading_mode   = bool(data[24])
        frame.last_cmd_id    = data[25]
        frame.last_cmd_name  = cls.CMD_NAMES.get(data[25], 'UNKNOWN')
        frame.jitter_us      = struct.unpack_from('<H', data, 26)[0] / 10.0
        frame.missed_cycles  = data[28]  # uint8
        frame.timestamp      = time.time()
        return frame


# ============================================================
#  Robot Controller
# ============================================================
class RobotController:
    """100Hz control loop with serial telemetry + command output."""

    def __init__(self, serial_port: str, freq_hz: int = 100,
                 autonomy: str = 'idle', api_url: str = 'http://localhost:8090'):
        if serial is None:
            raise ImportError("pyserial not installed. Run: pip install pyserial")

        self.period_ns = int(1e9 / freq_hz)
        self.freq_hz = freq_hz
        self.autonomy_mode = autonomy
        self.api_url = api_url

        if serial_port.startswith('socket://'):
            self.ser = serial.serial_for_url(serial_port, timeout=0.005)
        else:
            self.ser = serial.Serial(
                port=serial_port,
                baudrate=460800,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.005,
            )
        print(f"[control] Serial opened: {serial_port}")

        # State
        self.running = True
        self.cycle_count = 0
        self.last_telemetry: TelemetryFrame | None = None
        self.last_cmd_sent: str = ''
        self.jitter_samples: list[float] = []  # last 100 jitter values

        # Autonomy test sequence generator
        self._auto_gen = self._autonomy_sequence()

        # Safety polling thread (200ms to minimize response latency)
        self._safety_thread = threading.Thread(target=self._safety_poll, daemon=True)

        # API reporting thread (runs in background to avoid control loop jitter)
        self._api_report_stop = threading.Event()
        self._api_thread = threading.Thread(target=self._api_report_loop, daemon=True)

        signal.signal(signal.SIGINT, self._on_signal)
        signal.signal(signal.SIGTERM, self._on_signal)

    def _on_signal(self, signum, frame):
        print("\n[control] Shutdown signal received")
        self.running = False
        self._api_report_stop.set()

    def send_command(self, cmd: str):
        """Send ASCII command to STM32 over serial."""
        self.last_cmd_sent = cmd.split()[0]  # first token, e.g. "FWD"
        raw = (cmd + '\r\n').encode('ascii')
        self.ser.write(raw)
        self.ser.flush()

    def _read_telemetry(self) -> TelemetryFrame | None:
        """Non-blocking frame sync + read from serial stream."""
        try:
            # Sync to frame header (0xBA 0xDD)
            sync_attempts = 0
            while self.ser.in_waiting > 0 and sync_attempts < 300:
                b = self.ser.read(1)
                sync_attempts += 1
                if b[0] == 0xBA:
                    b2 = self.ser.read(1)
                    if len(b2) == 1 and b2[0] == 0xDD:
                        # Read remaining 30 bytes
                        remaining = self.ser.read(30)
                        if len(remaining) == 30:
                            frame_data = b'\xBA\xDD' + remaining
                            return TelemetryFrame.parse(frame_data)
                        break  # partial read, give up this cycle
            return None
        except (serial.SerialException, OSError):
            return None

    def _autonomy_sequence(self):
        """Generator yielding commands for simple autonomy demo."""
        if self.autonomy_mode == 'idle':
            while True:
                yield None

        # Test sequence: forward → turn → forward → stop → repeat
        sequence = [
            ('FWD 350',  3.0),     # forward 3s
            ('STOP',     1.0),     # pause
            ('LEFT 250', 2.0),     # turn left 2s
            ('FWD 350',  3.0),     # forward 3s
            ('STOP',     1.0),
            ('RIGHT 250',2.0),     # turn right 2s
            ('FWD 350',  2.0),
            ('STOP',     2.0),
        ]

        seq_idx = 0
        cmd_start = 0.0
        current_cmd = None

        while True:
            now = time.time()
            if current_cmd is None or (now - cmd_start) >= sequence[seq_idx][1]:
                seq_idx = (seq_idx + 1) % len(sequence)
                current_cmd = sequence[seq_idx][0]
                cmd_start = now
                yield current_cmd
            else:
                yield None  # maintain current command

    def _api_report_loop(self):
        """Background thread: report telemetry to API every 2s."""
        while not self._api_report_stop.is_set():
            self._api_report_stop.wait(2.0)
            self._report_to_api()

    def _report_to_api(self):
        """Post telemetry to Go collector API."""
        try:
            if self.last_telemetry is not None:
                tlm = self.last_telemetry
                data = json.dumps({
                    'pid': os.getpid(),
                    'yaw_deg': tlm.yaw_deg,
                    'motor_left': tlm.motor_left,
                    'motor_right': tlm.motor_right,
                    'emergency_stop': tlm.emergency_stop,
                    'jitter_us': tlm.jitter_us,
                    'missed_cycles': tlm.missed_cycles,
                    'cycle_count': self.cycle_count,
                }).encode()
                req = urllib.request.Request(
                    f'{self.api_url}/api/robot_telemetry',
                    data=data,
                    headers={'Content-Type': 'application/json'},
                )
                urllib.request.urlopen(req, timeout=1)
        except Exception:
            pass

    def _safety_poll(self):
        """Thread: poll /api/safety_command every 200ms for fast ESTOP response."""
        while self.running:
            time.sleep(0.2)
            try:
                resp = urllib.request.urlopen(f'{self.api_url}/api/safety_command', timeout=0.5)
                data = json.loads(resp.read())
                cmd = data.get('cmd', '')
                if cmd == 'ESTOP' and self.last_telemetry is not None and not self.last_telemetry.emergency_stop:
                    print("[SAFETY] eBPF detected CRITICAL — sending EMERGENCY STOP!")
                    self.send_command('ESTOP')
                elif cmd.startswith('STOP'):
                    print(f"[SAFETY] eBPF safety command: {cmd}")
                    self.send_command(cmd)
            except Exception:
                pass

    def run(self):
        print(f"[control] Starting at {self.freq_hz}Hz (period={self.period_ns/1e6:.1f}ms)")
        print(f"[control] Autonomy mode: {self.autonomy_mode}")
        print("[control] Press Ctrl+C to stop")

        self._safety_thread.start()
        self._api_thread.start()

        last_time = time.monotonic_ns()
        seq_last = -1

        while self.running:
            self.cycle_count += 1

            # --- Read telemetry ---
            tlm = self._read_telemetry()
            if tlm is not None:
                self.last_telemetry = tlm
                self.jitter_samples.append(tlm.jitter_us)
                if len(self.jitter_samples) > 100:
                    self.jitter_samples = self.jitter_samples[-100:]

                # Command confirmation: verify STM32 received our command
                if self.last_cmd_sent and tlm.last_cmd_name != 'NONE':
                    if tlm.last_cmd_name != self.last_cmd_sent:
                        if self.cycle_count % 100 == 0:
                            print(f"[WARN] Cmd mismatch: sent={self.last_cmd_sent}, "
                                  f"echo={tlm.last_cmd_name} (id={tlm.last_cmd_id})")

                # Detect dropped telemetry frames
                expected_seq = (seq_last + 1) & 0xFF
                if seq_last >= 0 and tlm.seq != expected_seq:
                    dropped = (tlm.seq - expected_seq) & 0xFF
                    if dropped > 3:
                        print(f"[WARN] Telemetry gap: {dropped} frames dropped")

                seq_last = tlm.seq

            # --- Autonomy: get next command ---
            cmd = next(self._auto_gen)
            if cmd is not None:
                self.send_command(cmd)

            # --- Maintain fixed period ---
            elapsed = time.monotonic_ns() - last_time
            sleep_ns = self.period_ns - elapsed
            if sleep_ns > 0:
                time.sleep(sleep_ns / 1e9)
            elif sleep_ns < -self.period_ns:
                overrun_ms = -sleep_ns / 1e6
                if self.cycle_count % 500 == 0:
                    print(f"[WARN] Cycle {self.cycle_count}: overrun by {overrun_ms:.1f}ms")

            last_time = time.monotonic_ns()

            # Periodic status
            if self.cycle_count % 1000 == 0 and self.last_telemetry:
                tlm = self.last_telemetry
                avg_jitter = (sum(self.jitter_samples) / len(self.jitter_samples)
                              if self.jitter_samples else 0)
                print(f"[control] cycle={self.cycle_count:6d}  "
                      f"yaw={tlm.yaw_deg:+6.1f}  "
                      f"motor=({tlm.motor_left:+4d},{tlm.motor_right:+4d})  "
                      f"jitter_avg={avg_jitter:.0f}us  "
                      f"missed={tlm.missed_cycles}")


# ============================================================
#  CLI
# ============================================================
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Robot Control Node')
    parser.add_argument('--serial', type=str, default=None,
                        help='Serial port (auto-detect if not specified)')
    parser.add_argument('--freq', type=int, default=100,
                        help='Control loop frequency (Hz)')
    parser.add_argument('--autonomy', type=str, default='idle',
                        choices=['idle', 'test', 'patrol'],
                        help='Autonomy mode')
    parser.add_argument('--api', type=str, default='http://localhost:8090',
                        help='Go collector API URL')
    parser.add_argument('--list-ports', action='store_true',
                        help='List available serial ports and exit')
    args = parser.parse_args()

    if args.list_ports:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        if ports:
            print("Available serial ports:")
            for p in ports:
                desc = p.description or '(no description)'
                print(f"  {p.device} — {desc}")
        else:
            print("No serial ports found.")
        sys.exit(0)

    # Auto-detect serial port
    serial_port = args.serial
    if serial_port is None:
        import serial.tools.list_ports
        ports = list(serial.tools.list_ports.comports())
        if ports:
            serial_port = ports[0].device
            print(f"[control] Auto-detected serial port: {serial_port}")
        else:
            print("[ERROR] No serial port found. Use --serial PORT or --list-ports")
            sys.exit(1)

    ctrl = RobotController(
        serial_port=serial_port,
        freq_hz=args.freq,
        autonomy=args.autonomy,
        api_url=args.api,
    )
    ctrl.run()
    print(f"[control] Stopped after {ctrl.cycle_count} cycles")
    ctrl.ser.close()
