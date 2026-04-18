# record_audio.py
# Thu âm thanh từ ESP32-S3 + SPH0645 qua Serial và xuất file .wav
#
# Yêu cầu:
#   pip install pyserial
#
# Cách dùng:
#   python record_audio.py --port COM3 --duration 5 --output output.wav

import serial
import wave
import struct
import argparse
import time
import sys
from datetime import datetime

# ─── Cấu hình mặc định ───────────────────────────────────────
DEFAULT_PORT      = "COM3"
DEFAULT_BAUDRATE  = 115200
DEFAULT_DURATION  = 5        # giây
DEFAULT_OUTPUT    = "output.wav"
SAMPLE_RATE       = 16000    # phải khớp với SAMPLE_RATE trong main.cpp
CHANNELS          = 1        # mono (SPH0645 SEL = GND → LEFT)
SAMPLE_WIDTH      = 2        # bytes (16-bit PCM cho WAV)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Thu âm thanh từ ESP32-S3 + SPH0645 → file .wav"
    )
    parser.add_argument("--port",     default=DEFAULT_PORT,
                        help=f"Cổng Serial (mặc định: {DEFAULT_PORT})")
    parser.add_argument("--baud",     default=DEFAULT_BAUDRATE, type=int,
                        help=f"Baudrate (mặc định: {DEFAULT_BAUDRATE})")
    parser.add_argument("--duration", default=DEFAULT_DURATION, type=float,
                        help=f"Thời gian thu âm (giây, mặc định: {DEFAULT_DURATION})")
    parser.add_argument("--output",   default=DEFAULT_OUTPUT,
                        help=f"Tên file output (mặc định: {DEFAULT_OUTPUT})")
    parser.add_argument("--auto-name", action="store_true",
                        help="Tự đặt tên file theo timestamp (vd: rec_20240101_120000.wav)")
    return parser.parse_args()


def make_output_name(base: str, auto: bool) -> str:
    if auto:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        return f"rec_{ts}.wav"
    return base


def record(port: str, baud: int, duration: float, output: str):
    total_samples  = int(SAMPLE_RATE * duration)
    # ESP32 gửi raw int32 (4 bytes/sample) → ta đọc 4 bytes rồi chuyển sang int16
    bytes_per_raw  = 4
    total_bytes    = total_samples * bytes_per_raw

    print(f"[CONFIG] Port={port} | Baud={baud} | "
          f"Duration={duration}s | Output={output}")
    print(f"[CONFIG] SampleRate={SAMPLE_RATE}Hz | Channels={CHANNELS} | "
          f"Expect {total_samples} samples ({total_bytes} bytes raw)")

    # ── Mở Serial ────────────────────────────────────────────
    try:
        ser = serial.Serial(port, baud, timeout=2)
    except serial.SerialException as e:
        print(f"[ERROR] Không mở được cổng {port}: {e}")
        sys.exit(1)

    # Chờ ESP32 boot xong
    time.sleep(1.5)
    ser.reset_input_buffer()
    print(f"[INFO]  Đã kết nối {port}. Bắt đầu thu âm {duration}s...")

    # ── Thu dữ liệu thô ──────────────────────────────────────
    raw_data   = bytearray()
    start_time = time.time()
    last_print = start_time

    while len(raw_data) < total_bytes:
        elapsed = time.time() - start_time

        # Timeout cứng
        if elapsed > duration + 3:
            print("\n[WARN]  Timeout — dừng thu âm sớm.")
            break

        waiting = ser.in_waiting
        if waiting > 0:
            chunk = ser.read(min(waiting, total_bytes - len(raw_data)))
            raw_data.extend(chunk)

        # In tiến độ mỗi 0.5s
        if time.time() - last_print >= 0.5:
            pct = len(raw_data) / total_bytes * 100
            bar = "█" * int(pct / 5) + "░" * (20 - int(pct / 5))
            print(f"\r[REC]   [{bar}] {pct:5.1f}%  "
                  f"{len(raw_data)}/{total_bytes} bytes", end="", flush=True)
            last_print = time.time()

        time.sleep(0.001)

    print(f"\n[INFO]  Thu được {len(raw_data)} bytes raw.")
    ser.close()

    if len(raw_data) < bytes_per_raw:
        print("[ERROR] Không đủ dữ liệu để tạo file WAV.")
        sys.exit(1)

    # ── Chuyển đổi int32 → int16 ─────────────────────────────
    # SPH0645 xuất 24-bit data trong frame 32-bit
    # Bit có nghĩa nằm ở 18 bit trên → dịch phải 14 bit (giống trong main.cpp)
    # Sau đó clamp về int16 để ghi WAV chuẩn
    pcm_samples = []
    num_raw     = len(raw_data) // bytes_per_raw

    for i in range(num_raw):
        raw_int32 = struct.unpack_from("<i", raw_data, i * bytes_per_raw)[0]
        sample18  = raw_int32 >> 14          # lấy 18 bit có nghĩa
        sample16  = max(-32768, min(32767, sample18))   # clamp int16
        pcm_samples.append(sample16)

    print(f"[INFO]  Chuyển đổi xong: {len(pcm_samples)} PCM samples.")

    # ── Ghi file WAV ─────────────────────────────────────────
    with wave.open(output, "wb") as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)       # 2 bytes = 16-bit PCM
        wf.setframerate(SAMPLE_RATE)
        pcm_bytes = struct.pack(f"<{len(pcm_samples)}h", *pcm_samples)
        wf.writeframes(pcm_bytes)

    size_kb = len(pcm_bytes) / 1024
    print(f"[DONE]  Đã lưu: {output}  ({size_kb:.1f} KB, "
          f"{len(pcm_samples)} samples @ {SAMPLE_RATE}Hz)")


# ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    args   = parse_args()
    output = make_output_name(args.output, args.auto_name)
    record(
        port     = args.port,
        baud     = args.baud,
        duration = args.duration,
        output   = output
    )