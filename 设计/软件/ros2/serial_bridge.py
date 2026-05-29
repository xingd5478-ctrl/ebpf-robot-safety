#!/usr/bin/env python3
"""Bridge: STM32 AADD frames → Go collector API for Dashboard."""
import serial, struct, time, urllib.request, json

API = "http://localhost:8090"
PORT = "COM16"
BAUD = 460800
GYRO_LSB = 131.0  # ±250dps

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
    return crc & 0xFFFF

print(f"Opening {PORT} @ {BAUD}...")
ser = serial.Serial(PORT, BAUD, timeout=0.1)
buf = b''
count = 0
last_post = 0
yaw = 0.0
last_t = time.time()

while True:
    try:
        if ser.in_waiting: buf += ser.read(ser.in_waiting)

        while len(buf) >= 20:
            if buf[0] != 0xAA or buf[1] != 0xDD: buf = buf[1:]; continue
            f = buf[:20]
            if crc16(f[:18]) != struct.unpack_from('<H', f, 18)[0]: buf = buf[1:]; continue

            count += 1
            ax = struct.unpack_from('<h', f, 6)[0]
            ay = struct.unpack_from('<h', f, 8)[0]
            az = struct.unpack_from('<h', f, 10)[0]
            gz = struct.unpack_from('<h', f, 16)[0]

            # Integrate gyro Z for yaw estimate
            now = time.time()
            dt = now - last_t
            last_t = now
            if 0.005 < dt < 0.05:  # valid dt range
                yaw += (gz / GYRO_LSB) * dt

            # Keep yaw in [0, 360)
            yaw %= 360

            buf = buf[20:]

            if count % 200 == 0:
                pitch = round(ay / 16384.0 * 90, 1)
                roll = round(ax / 16384.0 * 90, 1)
                print(f"  [{count:5d}] pitch={pitch:+6.1f} roll={roll:+6.1f} yaw={yaw:+6.1f}")

            # Post every 2s
            now2 = time.time()
            if now2 - last_post > 2:
                try:
                    tlm = {"yaw_deg": round(yaw, 1), "motor_left": 0, "motor_right": 0,
                           "emergency_stop": False, "jitter_us": 0,
                           "missed_cycles": 0, "cycle_count": count}
                    d = json.dumps(tlm).encode()
                    urllib.request.urlopen(
                        urllib.request.Request(API + "/api/robot_telemetry",
                            data=d, headers={"Content-Type": "application/json"}), timeout=2)
                    print(f"  [POST] yaw={yaw:.1f}")
                except Exception as e: print(f"  [POST ERR] {e}")
                last_post = now2

        time.sleep(0.001)
    except KeyboardInterrupt: break
    except Exception as e: print(f"Err: {e}"); time.sleep(1)

ser.close()
