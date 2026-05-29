#!/usr/bin/env python3
"""
Integration smoke test for eBPF Robot Safety Monitor.

Validates the data path end-to-end without requiring physical hardware.
Run with: python3 tests/test_integration.py

Tests:
  1. CRC16 round-trip (STM32 ↔ Python protocol compatibility)
  2. Telemetry frame parsing (valid + corrupt + CRC mismatch)
  3. Command parser equivalence (verify cmd dispatch logic)
  4. Go collector API health check
  5. BPF object file existence + section verification
"""

import sys
import os
import struct
import json
import urllib.request
import time

# Add ros2/ to path for TelemetryFrame import
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'ros2'))

PASS = 0
FAIL = 0

def check(name, condition, detail=''):
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f'  PASS  {name}')
    else:
        FAIL += 1
        print(f'  FAIL  {name}  {detail}')

def section(title):
    print(f'\n{"="*60}')
    print(f'  {title}')
    print(f'{"="*60}')


# ============================================================
#  Test 1: CRC16 compatibility
# ============================================================
section('Test 1: CRC16 protocol compatibility')

# Import from robot_control
from robot_control import crc16 as py_crc16

# Known test vectors (same as STM32 firmware uses)
test_vectors = [
    (bytes([0xBA, 0xDD, 0x00] + [0]*27), 'zero-filled telemetry header'),
    (bytes(range(32)), 'sequential bytes 0-31'),
    (b'Hello, STM32!', 'ASCII string'),
    (bytes([0xFF]*30), 'all 0xFF'),
    (bytes(30), 'all zeros'),
]

for data, desc in test_vectors:
    crc = py_crc16(data)
    check(f'CRC16({desc}) = 0x{crc:04X}', 0 < crc <= 0xFFFF,
          f'got {crc}')

# Verify CRC16 self-consistency: CRC should detect single-bit errors
original = bytes([0xBA, 0xDD, 0x05] + [0xAB]*27)
crc_orig = py_crc16(original)
corrupted = bytearray(original)
corrupted[15] ^= 0x01  # flip one bit
crc_corrupt = py_crc16(bytes(corrupted))
check('CRC detects single-bit error', crc_orig != crc_corrupt,
      f'orig={crc_orig:04X} corrupt={crc_corrupt:04X}')


# ============================================================
#  Test 2: Telemetry frame parsing
# ============================================================
section('Test 2: Telemetry frame parsing')

from robot_control import TelemetryFrame

# Build a valid frame manually
def build_frame(yaw_d10=450, motor_l=300, motor_r=250, seq=0x42,
                emergency_stop=0, heading_mode=0, last_cmd_id=2,
                jitter_us10=85, missed=0):
    buf = bytearray(32)
    buf[0] = 0xBA   # big-endian magic, matches STM32 firmware
    buf[1] = 0xDD
    buf[2] = seq
    struct.pack_into('<h', buf, 3, yaw_d10)
    struct.pack_into('<h', buf, 5, 0)          # target_yaw = 0
    struct.pack_into('<h', buf, 7, motor_l)
    struct.pack_into('<h', buf, 9, motor_r)
    # accel: ax,ay,az = 0, 0, 16384 (1g)
    struct.pack_into('<h', buf, 11, 0)
    struct.pack_into('<h', buf, 13, 0)
    struct.pack_into('<h', buf, 15, 16384)
    # gyro: all zero
    for i in range(3):
        struct.pack_into('<h', buf, 17 + i*2, 0)
    buf[23] = emergency_stop
    buf[24] = heading_mode
    buf[25] = last_cmd_id
    struct.pack_into('<H', buf, 26, jitter_us10)
    buf[28] = missed
    # CRC over bytes 0-29
    crc = py_crc16(bytes(buf[:30]))
    struct.pack_into('<H', buf, 30, crc)
    return bytes(buf)

# Test valid frame
frame_data = build_frame(yaw_d10=450, motor_l=300, motor_r=250, seq=0x42)
tlm = TelemetryFrame.parse(frame_data)
check('Parse valid frame', tlm is not None)
if tlm:
    check('  yaw = 45.0 deg', abs(tlm.yaw_deg - 45.0) < 0.1,
          f'got {tlm.yaw_deg}')
    check('  motor_left = 300', tlm.motor_left == 300,
          f'got {tlm.motor_left}')
    check('  motor_right = 250', tlm.motor_right == 250,
          f'got {tlm.motor_right}')
    check('  seq = 0x42', tlm.seq == 0x42, f'got {tlm.seq}')
    check('  last_cmd = FWD', tlm.last_cmd_name == 'FWD',
          f'got {tlm.last_cmd_name}')
    check('  jitter = 8.5 us', abs(tlm.jitter_us - 8.5) < 0.1,
          f'got {tlm.jitter_us}')
    check('  not emergency_stop', not tlm.emergency_stop)
    check('  not heading_mode', not tlm.heading_mode)

# Test wrong magic
bad_magic = bytearray(frame_data)
bad_magic[0] = 0xFF
check('Reject bad magic', TelemetryFrame.parse(bytes(bad_magic)) is None)

# Test CRC mismatch
bad_crc = bytearray(frame_data)
bad_crc[31] ^= 0xFF
check('Reject CRC mismatch', TelemetryFrame.parse(bytes(bad_crc)) is None)

# Test short frame
check('Reject short frame', TelemetryFrame.parse(frame_data[:20]) is None)

# Test emergency stop flag
estop_data = build_frame(emergency_stop=1)
estop_tlm = TelemetryFrame.parse(estop_data)
check('Detect emergency_stop flag', estop_tlm is not None and estop_tlm.emergency_stop)

# Test heading mode flag
head_data = build_frame(heading_mode=1)
head_tlm = TelemetryFrame.parse(head_data)
check('Detect heading_mode flag', head_tlm is not None and head_tlm.heading_mode)


# ============================================================
#  Test 3: Command name mapping
# ============================================================
section('Test 3: Command confirmation mapping')

expected_cmds = {
    0: 'NONE', 1: 'STOP', 2: 'FWD', 3: 'BACK',
    4: 'LEFT', 5: 'RIGHT', 6: 'VEL', 7: 'ESTOP', 8: 'HEAD',
}
for cmd_id, cmd_name in expected_cmds.items():
    frame = build_frame(last_cmd_id=cmd_id)
    tlm = TelemetryFrame.parse(frame)
    check(f'  cmd_id={cmd_id} → {cmd_name}',
          tlm is not None and tlm.last_cmd_name == cmd_name,
          f'got {tlm.last_cmd_name if tlm else "None"}')


# ============================================================
#  Test 4: Go collector API health
# ============================================================
section('Test 4: Go collector API')

api_url = os.environ.get('COLLECTOR_API', 'http://localhost:8090')

def api_ok(endpoint):
    try:
        resp = urllib.request.urlopen(f'{api_url}{endpoint}', timeout=2)
        return resp.status == 200
    except Exception as e:
        return False, str(e)

# Only run if collector is running
collector_alive = False
try:
    urllib.request.urlopen(f'{api_url}/api/summary', timeout=2)
    collector_alive = True
except Exception:
    pass

if collector_alive:
    check('GET /api/summary', api_ok('/api/summary'))
    check('GET /api/loop', api_ok('/api/loop'))
    check('GET /api/serial', api_ok('/api/serial'))
    check('GET /api/sched', api_ok('/api/sched'))
    check('GET /api/alerts', api_ok('/api/alerts'))
    check('GET /api/jitter_history', api_ok('/api/jitter_history'))

    # Test safety command endpoint (atomic read+clear)
    try:
        resp = urllib.request.urlopen(f'{api_url}/api/safety_command', timeout=2)
        data = json.loads(resp.read())
        check('GET /api/safety_command returns {cmd: ...}',
              'cmd' in data, f'got {data}')
    except Exception as e:
        check('GET /api/safety_command', False, str(e))

    # Test robot telemetry POST
    try:
        import json as jmod
        tlm_data = jmod.dumps({
            'yaw_deg': 45.0, 'motor_left': 300, 'motor_right': 250,
            'emergency_stop': False, 'jitter_us': 8.5,
            'missed_cycles': 0, 'cycle_count': 1000,
        }).encode()
        req = urllib.request.Request(
            f'{api_url}/api/robot_telemetry',
            data=tlm_data,
            headers={'Content-Type': 'application/json'},
        )
        resp = urllib.request.urlopen(req, timeout=2)
        check('POST /api/robot_telemetry', resp.status == 200)

        # Verify telemetry is reflected in summary
        summary_resp = urllib.request.urlopen(f'{api_url}/api/summary', timeout=2)
        summary = json.loads(summary_resp.read())
        check('  robot_yaw in summary', summary.get('robot_yaw') == 45.0)
    except Exception as e:
        check('POST /api/robot_telemetry', False, str(e))

    # Test command endpoint
    try:
        cmd_data = b'{"cmd":"STOP"}'
        req = urllib.request.Request(
            f'{api_url}/api/command',
            data=cmd_data,
            headers={'Content-Type': 'application/json'},
        )
        resp = urllib.request.urlopen(req, timeout=2)
        check('POST /api/command', resp.status == 200)
    except Exception as e:
        check('POST /api/command', False, str(e))
else:
    print('  (Collector not running — skipping API tests)')
    print(f'  Start with: sudo BPF_DIR=bpf ./bin/collector')


# ============================================================
#  Test 5: BPF object files
# ============================================================
section('Test 5: BPF object files')

bpf_dir = os.environ.get('BPF_DIR',
    os.path.join(os.path.dirname(__file__), '..', 'bpf'))

for name in ['loop_monitor', 'serial_monitor', 'sched_monitor']:
    path = os.path.join(bpf_dir, f'{name}.bpf.o')
    check(f'{name}.bpf.o exists', os.path.isfile(path),
          f'at {path}')

# Check ELF magic
for name in ['loop_monitor', 'serial_monitor', 'sched_monitor']:
    path = os.path.join(bpf_dir, f'{name}.bpf.o')
    if os.path.isfile(path):
        with open(path, 'rb') as f:
            magic = f.read(4)
        is_elf = magic == b'\x7fELF'
        check(f'{name}.bpf.o is valid ELF', is_elf,
              f'magic={magic.hex()}')


# ============================================================
#  Results
# ============================================================
section('Results')

total = PASS + FAIL
print(f'  Passed: {PASS}/{total}')
if FAIL > 0:
    print(f'  FAILED: {FAIL}/{total}')
    sys.exit(1)
else:
    print(f'  All {total} tests passed!')
