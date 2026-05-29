import serial, time
s = serial.Serial('COM16', 460800, timeout=1)
time.sleep(0.5)
d = s.read(200)
s.close()
print(f"{len(d)} bytes, {d.count(b'\xAA\xDD')} AADD frames")
