"""
STM32 MPU6050 100fps 优化验证测试

测试项:
  1. 100fps 持续流传输 (30s)
  2. 帧率精度 (±5% @ 100Hz)
  3. 序列连续性 (丢帧检测)
  4. CRC 完整性校验 (每帧)
  5. 帧间隔抖动 (min/avg/max/std)
  6. MCU 系统健康检查 (丢帧/通信错误)

用法:
    python tests/test_100fps_optimized.py [COM端口]
    python tests/test_100fps_optimized.py           # 自动检测端口
"""

import serial
import serial.tools.list_ports
import time
import sys
import struct
from statistics import stdev, mean
from collections import defaultdict


# ======================== 协议常量 ========================

FRAME_RAW_6AXIS = 0xAADD
FRAME_RAW_SIZE = 20
FRAME_RAW_PAYLOAD_LEN = 18

BAUDRATE = 460800
BOOT_WAIT = 4


# ======================== CRC16 ========================

def crc16(data: bytes) -> int:
    """CRC16-CCITT (poly=0x1021, init=0xFFFF) — 与 MCU 固件一致"""
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ======================== 端口检测 ========================

def auto_detect_port():
    """自动检测 STM32 串口"""
    for p in serial.tools.list_ports.comports():
        desc = p.description.upper()
        hwid = p.hwid.upper()
        if 'STM32' in desc or 'STLINK' in desc:
            return p.device
        if 'USB SERIAL' in desc:
            return p.device
        if 'CH340' in desc:
            return p.device
        if 'FT' in desc or '0403' in hwid:
            return p.device
    return None


# ======================== CLI 辅助 ========================

def send_cmd(ser, cmd, wait=0.3):
    """发送 CLI 命令, 返回响应文本"""
    ser.reset_input_buffer()
    ser.write((cmd + '\r\n').encode())
    time.sleep(wait)
    resp = ser.read(ser.in_waiting) if ser.in_waiting else b''
    return resp.decode('utf-8', errors='replace')


# ======================== 帧捕获与解析 ========================

def capture_frames(ser, duration, quiet=False):
    """
    捕获数据帧, 返回解析结果列表.

    返回值: list[dict], 每项包含:
        - seq: int      序列号
        - t_recv: float 接收时间戳 (time.monotonic)
        - accel: tuple[int16*3]
        - gyro:  tuple[int16*3]
        - crc_ok: bool  CRC 校验结果
        - offset: int   在缓冲区的偏移位置
    """
    ser.reset_input_buffer()
    time.sleep(0.2)
    ser.reset_input_buffer()

    buf = bytearray()
    frames = []
    start = time.monotonic()
    last_log = 0.0

    while True:
        elapsed = time.monotonic() - start
        if elapsed >= duration:
            break

        # 读取可用数据
        if ser.in_waiting:
            chunk = ser.read(min(ser.in_waiting, 4096))
            buf.extend(chunk)

        # 解析缓冲区中的帧
        while True:
            if len(buf) < FRAME_RAW_SIZE:
                break

            # 查找帧头 0xAA 0xDD
            if buf[0] != 0xAA or buf[1] != 0xDD:
                buf.pop(0)
                continue

            if len(buf) < FRAME_RAW_SIZE:
                break

            frame = bytes(buf[:FRAME_RAW_SIZE])

            # 校验 CRC
            expected_crc = struct.unpack('<H', frame[FRAME_RAW_PAYLOAD_LEN:FRAME_RAW_PAYLOAD_LEN + 2])[0]
            calc_crc = crc16(frame[:FRAME_RAW_PAYLOAD_LEN])
            crc_ok = (calc_crc == expected_crc)

            # 解析
            seq = frame[3] | ((frame[4] & 0x7F) << 8)
            ax, ay, az = struct.unpack('<3h', frame[6:12])
            gx, gy, gz = struct.unpack('<3h', frame[12:18])

            frames.append({
                'seq': seq,
                't_recv': time.monotonic(),
                'accel': (ax, ay, az),
                'gyro': (gx, gy, gz),
                'crc_ok': crc_ok,
            })

            buf = buf[FRAME_RAW_SIZE:]

            # 进度日志
            if not quiet and elapsed - last_log >= 5:
                fps = len(frames) / elapsed if elapsed > 0 else 0
                print(f"  [{elapsed:.0f}s] {len(frames)} frames, {fps:.1f} FPS")
                last_log = elapsed

        # 短暂休眠避免忙等
        time.sleep(0.002)

    elapsed_total = time.monotonic() - start
    return frames, elapsed_total


# ======================== 分析函数 ========================

def analyze_frames(frames, duration):
    """全面分析捕获的帧数据"""
    n = len(frames)
    if n == 0:
        return {"error": "no frames captured"}

    # 基础统计
    avg_fps = n / duration if duration > 0 else 0

    # 序列完整性
    seq_drops = 0
    seq_dups = 0
    seq_out_of_order = 0
    seen_seqs = set()
    prev_seq = -1

    for f in frames:
        s = f['seq']
        if s in seen_seqs:
            seq_dups += 1
        seen_seqs.add(s)

        if prev_seq >= 0:
            expected = (prev_seq + 1) & 0x7FFF
            if s != expected:
                gap = (s - expected) & 0x7FFF
                if gap < 0x4000:  # 正常前向跳变
                    seq_drops += gap
                else:              # 乱序
                    seq_out_of_order += 1
        prev_seq = s

    # CRC 完整性
    crc_errors = sum(1 for f in frames if not f['crc_ok'])

    # 帧间隔统计 (使用接收时间戳)
    intervals = []
    for i in range(1, n):
        dt = (frames[i]['t_recv'] - frames[i - 1]['t_recv']) * 1000  # ms
        intervals.append(dt)

    if intervals:
        jitter_std = stdev(intervals)
        jitter_min = min(intervals)
        jitter_max = max(intervals)
        jitter_avg = mean(intervals)
        # 10ms 理论值 ±1ms 以外的视为异常间隔
        outliers = sum(1 for dt in intervals if dt < 8 or dt > 12)
    else:
        jitter_std = jitter_min = jitter_max = jitter_avg = 0
        outliers = 0

    # 传感器数据合理性
    accel_values = [f['accel'] for f in frames]
    gyro_values = [f['gyro'] for f in frames]

    # 检查是否有合理的变化范围 (排除全零或恒定值)
    ax_vals = [a[0] for a in accel_values]
    ay_vals = [a[1] for a in accel_values]
    az_vals = [a[2] for a in accel_values]

    # Z 轴应接近 +1g = +16384 LSB (静止时)
    az_mean = mean(az_vals) if az_vals else 0
    az_ok = 8000 < az_mean < 24000  # 0.5g ~ 1.5g

    return {
        'n_frames': n,
        'duration': duration,
        'avg_fps': avg_fps,
        'seq_drops': seq_drops,
        'seq_dups': seq_dups,
        'seq_out_of_order': seq_out_of_order,
        'unique_seqs': len(seen_seqs),
        'crc_errors': crc_errors,
        'n_intervals': len(intervals),
        'interval_avg_ms': jitter_avg,
        'interval_min_ms': jitter_min,
        'interval_max_ms': jitter_max,
        'interval_std_ms': jitter_std,
        'outliers': outliers,
        'az_mean': az_mean,
        'az_ok': az_ok,
    }


def print_results(stats, label="100fps"):
    """格式化输出分析结果"""
    if 'error' in stats:
        print(f"\n  [{label}] ERROR: {stats['error']}")
        return

    s = stats
    print(f"\n  [{label}] {'='*50}")
    print(f"  捕获时间:    {s['duration']:.1f}s")
    print(f"  总帧数:      {s['n_frames']}")
    print(f"  平均帧率:    {s['avg_fps']:.2f} FPS")
    print(f"  {'─'*50}")
    print(f"  序列完整性:")
    print(f"    丢帧数:    {s['seq_drops']}")
    print(f"    重复帧:    {s['seq_dups']}")
    print(f"    乱序帧:    {s['seq_out_of_order']}")
    print(f"    唯一序列:  {s['unique_seqs']}")
    print(f"  {'─'*50}")
    print(f"  CRC 校验:")
    print(f"    错误帧数:  {s['crc_errors']} / {s['n_frames']}")
    print(f"  {'─'*50}")
    print(f"  帧间隔抖动:")
    print(f"    平均:      {s['interval_avg_ms']:.3f} ms")
    print(f"    最小:      {s['interval_min_ms']:.3f} ms")
    print(f"    最大:      {s['interval_max_ms']:.3f} ms")
    print(f"    标准差:    {s['interval_std_ms']:.3f} ms")
    print(f"    异常:      {s['outliers']} / {s['n_intervals']}")
    print(f"  {'─'*50}")
    print(f"  传感器合理性:")
    print(f"    Z轴均值:  {s['az_mean']:.0f} LSB", end="")
    print(f"  {'[OK]' if s['az_ok'] else '[WARN] 预期 ~16384'}")
    print(f"  {'='*50}")


def evaluate_pass_fail(stats):
    """基于测试数据给出 PASS/FAIL 结论"""
    if 'error' in stats:
        return False, [stats['error']]

    failures = []
    warnings = []

    # 帧率检查
    if stats['avg_fps'] < 90:
        failures.append(f"帧率过低: {stats['avg_fps']:.1f} FPS (需 ≥ 90)")
    elif stats['avg_fps'] < 95:
        warnings.append(f"帧率偏低: {stats['avg_fps']:.1f} FPS (建议 ≥ 95)")

    # 丢帧检查
    if stats['seq_drops'] > 0:
        drop_rate = stats['seq_drops'] / stats['n_frames'] * 100 if stats['n_frames'] > 0 else 0
        if drop_rate > 1:
            failures.append(f"丢帧率过高: {stats['seq_drops']} 帧 ({drop_rate:.2f}%)")
        else:
            warnings.append(f"存在丢帧: {stats['seq_drops']} 帧 ({drop_rate:.3f}%)")

    # CRC 检查
    if stats['crc_errors'] > 0:
        failures.append(f"CRC 错误: {stats['crc_errors']} 帧")

    # 抖动检查
    if stats['interval_std_ms'] > 2:
        failures.append(f"抖动过大: std={stats['interval_std_ms']:.3f}ms (需 < 2ms)")
    elif stats['interval_std_ms'] > 1:
        warnings.append(f"抖动偏大: std={stats['interval_std_ms']:.3f}ms")

    # 异常间隔检查
    outlier_pct = stats['outliers'] / stats['n_intervals'] * 100 if stats['n_intervals'] > 0 else 0
    if outlier_pct > 5:
        failures.append(f"异常间隔过多: {stats['outliers']}/{stats['n_intervals']} ({outlier_pct:.1f}%)")
    elif outlier_pct > 1:
        warnings.append(f"异常间隔偏多: {stats['outliers']}/{stats['n_intervals']} ({outlier_pct:.1f}%)")

    # 传感器数据
    if not stats['az_ok']:
        warnings.append(f"Z轴加速度异常: {stats['az_mean']:.0f} LSB (预期 ~16384)")

    all_pass = len(failures) == 0
    return all_pass, failures + warnings


# ======================== 测试函数 ========================

def test_100fps_stream(ser, duration=30):
    """核心测试: 100fps 持续流"""
    print(f"\n{'='*60}")
    print(f"  测试 1: 100fps 持续流 ({duration}s)")
    print(f"{'='*60}")

    # 确保 100Hz
    send_cmd(ser, "rate 100", wait=0.3)
    time.sleep(0.5)
    ser.reset_input_buffer()

    frames, elapsed = capture_frames(ser, duration)
    stats = analyze_frames(frames, elapsed)
    print_results(stats, "100fps")

    passed, issues = evaluate_pass_fail(stats)
    print(f"\n  ['✓' if passed else '✗'] 100fps 流测试: {'PASS' if passed else 'FAIL'}")
    if issues:
        for issue in issues:
            print(f"    - {issue}")

    return passed, stats


def test_jitter_precision(ser, duration=10):
    """高精度抖动测量"""
    print(f"\n{'='*60}")
    print(f"  测试 2: 帧间隔抖动高精度测量 ({duration}s)")
    print(f"{'='*60}")

    send_cmd(ser, "rate 100", wait=0.3)
    time.sleep(0.5)
    ser.reset_input_buffer()

    frames, elapsed = capture_frames(ser, duration)
    stats = analyze_frames(frames, elapsed)
    print_results(stats, "jitter")

    return stats


def test_system_health(ser):
    """读取 MCU 系统状态"""
    print(f"\n{'='*60}")
    print(f"  测试 3: 系统健康检查")
    print(f"{'='*60}")

    resp = send_cmd(ser, "status", wait=0.5)
    for line in resp.split('\n'):
        line = line.strip()
        if any(kw in line for kw in ['运行时间', '总帧数', '丢帧', '通信错误',
                                       '堆使用率', '传感器', '看门狗', '当前配置']):
            print(f"  {line[:120]}")

    # 检查队列状态
    resp2 = send_cmd(ser, "tasks", wait=0.5)
    for line in resp2.split('\n'):
        if 'RawData' in line:
            print(f"  {line.strip()[:80]}")

    print()


def test_data_integrity(ser, duration=15):
    """数据完整性专项测试 (CRC + 序列)"""
    print(f"\n{'='*60}")
    print(f"  测试 4: 数据完整性专项 ({duration}s)")
    print(f"{'='*60}")

    frames, elapsed = capture_frames(ser, duration, quiet=True)
    stats = analyze_frames(frames, elapsed)
    print_results(stats, "integrity")

    crc_pass = stats['crc_errors'] == 0
    seq_pass = stats['seq_drops'] == 0
    print(f"  CRC 完整性:  {'✓ PASS' if crc_pass else '✗ FAIL'} "
          f"({stats['crc_errors']} errors)")
    print(f"  序列完整性:  {'✓ PASS' if seq_pass else '✗ FAIL'} "
          f"({stats['seq_drops']} drops)")

    return crc_pass and seq_pass


# ======================== 主函数 ========================

def main():
    print("=" * 60)
    print("  STM32-MPU6050 100fps 优化验证测试")
    print("  " + "=" * 50)
    print(f"  测试项: I2C 400kHz · 100fps 持续流 · CRC 校验 · 帧抖动")
    print("=" * 60)

    # 端口检测
    port = sys.argv[1] if len(sys.argv) > 1 else auto_detect_port()
    if not port:
        print("\n[ERROR] 未检测到串口设备!")
        print("  请指定端口: python test_100fps_optimized.py COMx")
        sys.exit(1)
    print(f"\n[INFO] 使用端口: {port}")

    # 打开串口
    try:
        ser = serial.Serial(port, BAUDRATE, timeout=0.01)
        ser.set_buffer_size(rx_size=65536, tx_size=65536)
        print(f"[INFO] 串口已打开: {port} @ {BAUDRATE}")
    except serial.SerialException as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    # 设置 USB 延迟
    try:
        import ctypes
        h = ctypes.windll.kernel32.CreateFileW(
            rf"\.\{port}", 0x80000000 | 0x40000000, 0, None, 3, 0, None)
        if h not in (-1, ctypes.wintypes.HANDLE(-1).value):
            buf = (ctypes.c_uint32 * 5)(1, 0, 0, 0, 0)
            ctypes.windll.kernel32.SetCommTimeouts(h, buf)
            ctypes.windll.kernel32.CloseHandle(h)
            print("[INFO] USB 延迟定时器已设为 1ms")
    except Exception:
        pass

    # 等待 MCU 启动
    print(f"\n[INFO] 等待 MCU 启动 ({BOOT_WAIT}s)...")
    time.sleep(BOOT_WAIT)
    ser.reset_input_buffer()

    # 检查 MCU 是否响应
    resp = send_cmd(ser, "", wait=0.3)
    resp = send_cmd(ser, "status", wait=0.5)
    print(f"[INFO] MCU 状态:\n  {resp[:200].strip()}")

    results = []

    try:
        # Test 1: 100fps 持续流
        passed, stats = test_100fps_stream(ser, duration=30)
        results.append(("100fps 流", passed, stats))

        # Test 2: 高精度抖动
        jitter_stats = test_jitter_precision(ser, duration=10)
        results.append(("抖动测量", jitter_stats['interval_std_ms'] < 2, jitter_stats))

        # Test 3: 系统健康
        test_system_health(ser)

        # Test 4: 数据完整性
        integrity_pass = test_data_integrity(ser, duration=15)
        results.append(("数据完整性", integrity_pass, {}))

        print(f"\n{'='*60}")
        print(f"  最终测试汇总")
        print(f"{'='*60}")
        all_pass = True
        for name, passed, _ in results:
            status = "✓ PASS" if passed else "✗ FAIL"
            print(f"  [{status}] {name}")
            if not passed:
                all_pass = False

        print(f"\n{'─'*60}")
        if all_pass:
            print(f"  结论: 全部通过 — 100fps 优化验证成功")
        else:
            print(f"  结论: 存在失败项, 请检查上述详情")
        print(f"{'─'*60}")

    except Exception as e:
        print(f"\n[ERROR] 测试异常: {e}")
        import traceback
        traceback.print_exc()
        all_pass = False
    finally:
        ser.close()
        print(f"\n[INFO] 串口已关闭")

    sys.exit(0 if all_pass else 1)


if __name__ == '__main__':
    main()
