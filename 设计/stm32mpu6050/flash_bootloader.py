#!/usr/bin/env python3
"""Flash STM32 firmware via custom bootloader protocol."""
import serial, time, sys, os

PORT = '/dev/ttyUSB1'
BAUD = 460800
FIRMWARE = '/home/xingdong/桌面/ebpf-robot-safety/stm32mpu6050/build/stm32mpu6050.bin'
APP_ADDR = 0x08002000
DATA_SIZE = 256

def crc16_ccitt(data):
    """CRC-16-CCITT with initial value 0xFFFF."""
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
    return crc & 0xFFFF

def flash_firmware():
    # Read firmware binary
    with open(FIRMWARE, 'rb') as f:
        fw_data = f.read()
    fw_size = len(fw_data)
    fw_crc = crc16_ccitt(fw_data)
    print(f"Firmware: {fw_size} bytes, CRC16=0x{fw_crc:04X}")

    if fw_size > 56 * 1024:
        print("ERROR: Firmware too large for 56KB app area!")
        sys.exit(1)

    s = serial.Serial(PORT, BAUD, timeout=0.5)

    # === Phase 1: Enter bootloader ===
    print("Entering bootloader...")
    s.read(4096)  # flush

    # Toggle DTR to reset MCU
    s.dtr = False
    s.rts = True
    time.sleep(0.05)
    s.dtr = True
    time.sleep(0.05)
    s.dtr = False

    # Send 'C' rapidly for 2 seconds
    for i in range(200):
        s.write(b'\x43')
        time.sleep(0.01)
        resp = s.read(64)
        if resp and b'\x52' in resp:
            print(f"  Got READY ('R') at attempt {i}")
            break
    else:
        print("ERROR: No bootloader response")
        s.close()
        sys.exit(1)

    # Flush aggressively
    time.sleep(0.05)
    s.read(4096)
    s.reset_input_buffer()

    # === Phase 2: Send START command ===
    print("Sending START...")
    # 'S' + 4B size (little-endian) + 2B CRC16
    start_pkt = b'\x53' + fw_size.to_bytes(4, 'little') + fw_crc.to_bytes(2, 'little')
    s.write(start_pkt)
    s.flush()

    # Expect ACK for START (3 second timeout like bootloader)
    start_ack = None
    for _ in range(60):
        b = s.read(1)
        if b == b'\x41':
            start_ack = True
            break
        elif b == b'\x4E':  # NAK
            print("ERROR: Bootloader NAK'd START command")
            s.close()
            sys.exit(1)
        elif b:
            pass  # ignore residual data
    if not start_ack:
        print(f"ERROR: No ACK for START within 3s")
        s.close()
        sys.exit(1)
    print("  START ACK")

    # Expect ACK for ERASE complete (erase takes time on STM32F103)
    erase_ack = None
    for _ in range(120):  # up to 6 seconds for erase
        b = s.read(1)
        if b == b'\x41':
            erase_ack = True
            break
        elif b == b'\x46':  # FAIL
            print("ERROR: Bootloader reported ERASE FAIL")
            s.close()
            sys.exit(1)
    if not erase_ack:
        print(f"ERROR: No ACK for ERASE within 6s")
        s.close()
        sys.exit(1)
    print("  ERASE ACK")

    # === Phase 3: Send firmware data ===
    print(f"Sending {fw_size} bytes in {DATA_SIZE}-byte chunks...")
    total_sent = 0
    pkt_count = 0

    while total_sent < fw_size:
        remaining = fw_size - total_sent
        chunk_size = min(DATA_SIZE, remaining)
        chunk = fw_data[total_sent:total_sent + chunk_size]
        s.write(chunk)
        total_sent += chunk_size
        pkt_count += 1

        # Every 16 packets (4KB), expect ACK
        if pkt_count % 16 == 0:
            time.sleep(0.1)
            ack = s.read(32)
            if b'\x41' in ack:
                pct = total_sent * 100 // fw_size
                print(f"  {pct}% ({total_sent}/{fw_size}) ACK")

        # Small delay between packets to avoid overwhelming UART
        if total_sent < fw_size:
            time.sleep(0.002)

    print(f"  All data sent ({total_sent} bytes, {pkt_count} packets)")

    # === Phase 4: Wait for final result ===
    time.sleep(0.5)
    # Expect final ACK for data complete
    resp = s.read(64)
    print(f"  Final response: {resp.hex() if resp else 'nothing'}")

    # Read more for success message
    time.sleep(0.5)
    resp2 = s.read(256)
    text = resp2.decode('latin-1', errors='replace')
    if 'UPDATE OK' in text:
        print("\n*** FLASH SUCCESS! ***")
    elif b'\x4F' in resp or b'\x4F' in resp2:  # 'O' success byte
        print("\n*** FLASH SUCCESS! (O received) ***")
    elif b'\x46' in resp or b'\x46' in resp2:  # 'F' fail byte
        print(f"\n*** FLASH FAILED! ***")
        print(f"Response: {text}")
    else:
        print(f"\nStatus unclear. Response text: {text[:200]}")

    s.close()

if __name__ == '__main__':
    flash_firmware()
