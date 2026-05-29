#!/usr/bin/env python3
"""Flash STM32 firmware via custom bootloader protocol - v2."""
import serial, time, sys

PORT = '/dev/ttyUSB1'
BAUD = 460800
FIRMWARE = '/home/xingdong/桌面/ebpf-robot-safety/stm32mpu6050/build/stm32mpu6050.bin'
DATA_SIZE = 256

def crc16_ccitt(data):
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
    return crc & 0xFFFF

def reset_mcu(s):
    """Reset STM32 by toggling DTR."""
    s.dtr = True
    s.rts = False
    time.sleep(0.2)
    s.dtr = False
    time.sleep(0.3)  # wait for STM32 to boot

def enter_bootloader(s):
    """Try to enter bootloader by resetting and sending 'C'."""
    # Try up to 5 times
    for attempt in range(5):
        print(f"  Attempt {attempt+1}...")

        # Flush buffers
        s.reset_input_buffer()
        s.reset_output_buffer()

        # Reset the MCU
        reset_mcu(s)

        # Now send 'C' rapidly - bootloader has ~1s window
        for i in range(80):  # 80 * 15ms = 1.2s
            s.write(b'\x43')
            s.flush()
            time.sleep(0.015)

            # Check for response
            if s.in_waiting:
                resp = s.read(s.in_waiting)
                # Look for 'R' (0x52) - bootloader ready
                if b'\x52' in resp:
                    # Verify it's really bootloader by checking context
                    # Bootloader sends just 'R', app data would have surrounding bytes
                    idx = resp.index(b'\x52')
                    context = resp[max(0,idx-2):idx+3]
                    print(f"    Got 0x52 at idx={idx}, context={context.hex()}")
                    # If 'R' appears soon after reset with little surrounding noise
                    if len(resp) < 20 or idx < 10:
                        print(f"    Bootloader ready!")
                        return True

        # If we didn't get in, read remaining data
        time.sleep(0.1)
        if s.in_waiting:
            remaining = s.read(s.in_waiting)
            # Check if app started (indicates bootloader passed)
            if b'MPU6050' in remaining or b'CLI' in remaining or b'BOOT' in remaining:
                print(f"    App started (bootloader passed)")

    return False

def flash_firmware():
    with open(FIRMWARE, 'rb') as f:
        fw_data = f.read()
    fw_size = len(fw_data)
    fw_crc = crc16_ccitt(fw_data)
    print(f"Firmware: {fw_size} bytes, CRC16=0x{fw_crc:04X}")

    s = serial.Serial(PORT, BAUD, timeout=0.2)

    # === Phase 1: Enter bootloader ===
    print("Entering bootloader...")
    if not enter_bootloader(s):
        print("ERROR: Could not enter bootloader after 5 attempts")
        s.close()
        sys.exit(1)

    # Flush before START
    time.sleep(0.05)
    s.reset_input_buffer()

    # === Phase 2: Send START ===
    print("Sending START command...")
    start_pkt = b'\x53' + fw_size.to_bytes(4, 'little') + fw_crc.to_bytes(2, 'little')
    s.write(start_pkt)
    s.flush()

    # Wait for START ACK (within 3 seconds)
    for _ in range(60):
        b = s.read(1)
        if b == b'\x41':
            print("  START ACK")
            break
        elif b == b'\x4e':
            print("ERROR: Bootloader sent NAK")
            s.close()
            sys.exit(1)
    else:
        print("ERROR: No ACK for START")
        s.close()
        sys.exit(1)

    # Wait for ERASE ACK (up to 10 seconds - erase is slow)
    print("Waiting for ERASE to complete...")
    for i in range(200):
        b = s.read(1)
        if b == b'\x41':
            print(f"  ERASE ACK (took ~{i*50}ms)")
            break
        elif b == b'\x46':
            print("ERROR: ERASE FAILED")
            s.close()
            sys.exit(1)
        elif b:
            print(f"  debug: got 0x{b.hex()} during erase wait")
    else:
        print("ERROR: No ACK for ERASE")
        s.close()
        sys.exit(1)

    # === Phase 3: Send data ===
    print(f"Sending {fw_size} bytes...")
    total = 0
    pkt = 0
    while total < fw_size:
        chunk = fw_data[total:total+DATA_SIZE]
        s.write(chunk)
        total += len(chunk)
        pkt += 1

        if pkt % 16 == 0:  # Every 4KB
            s.flush()
            time.sleep(0.05)
            # Read ACK
            ack_found = False
            for _ in range(10):
                if s.in_waiting:
                    b = s.read(1)
                    if b == b'\x41':
                        ack_found = True
                        break
            pct = total * 100 // fw_size
            print(f"  {pct}% ({total}/{fw_size}) {'ACK' if ack_found else '?'}")
        else:
            # Small delay between packets
            time.sleep(0.001)

    print(f"All data sent ({total} bytes, {pkt} packets)")

    # === Phase 4: Verify ===
    s.flush()
    time.sleep(0.2)

    # Read final ACK
    buf = b''
    for _ in range(40):
        if s.in_waiting:
            buf += s.read(s.in_waiting)
        time.sleep(0.05)

    print(f"Final response: {buf.hex() if buf else 'none'}")

    if b'\x4F' in buf:  # 'O' = success
        print("\n*** FLASH SUCCESS! ***")
    elif b'\x46' in buf:  # 'F' = fail
        print("\n*** FLASH FAILED ***")
        sys.exit(1)
    else:
        # Check for text message
        text = buf.decode('latin-1', errors='replace')
        if 'UPDATE OK' in text:
            print("\n*** FLASH SUCCESS! ***")
        elif 'FAIL' in text:
            print(f"\n*** FLASH FAILED: {text} ***")
            sys.exit(1)
        else:
            print(f"\nStatus unclear: {text[:200]}")

    s.close()

if __name__ == '__main__':
    flash_firmware()
