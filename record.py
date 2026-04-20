# record_audio.py
# ESP32 gửi int16 PCM trực tiếp → ghi thẳng vào WAV, không cần convert
#
# Yêu cầu: pip install pyserial
# Cách dùng: python record_audio.py --port COM3 --duration 5

import serial
import wave
import argparse
import time
import sys
from datetime import datetime

DEFAULT_PORT     = "COM3"
DEFAULT_BAUDRATE = 115200
DEFAULT_DURATION = 5
DEFAULT_OUTPUT   = "output.wav"
SAMPLE_RATE      = 16000
CHANNELS         = 1
SAMPLE_WIDTH     = 2   # int16 = 2 bytes/sample


def record(port, baud, duration, output):
    total_samples = int(SAMPLE_RATE * duration)
    total_bytes   = total_samples * SAMPLE_WIDTH

    print(f"[CONFIG] {port} @ {baud}baud | {duration}s | → {output}")
    print(f"[CONFIG] Cần {total_bytes} bytes ({total_samples} samples @ {SAMPLE_RATE}Hz)")
    print(f"[WARN]   Đảm bảo ESP32 đang ở RECORDING_MODE = true")

    try:
        ser = serial.Serial(port, baud, timeout=2)
    except serial.SerialException as e:
        print(f"[ERROR] Không mở được {port}: {e}")
        sys.exit(1)

    time.sleep(1.5)
    ser.reset_input_buffer()
    print("[INFO]  Bắt đầu thu âm...\n")

    pcm_data   = bytearray()
    start_time = time.time()
    last_print = start_time

    while len(pcm_data) < total_bytes:
        if time.time() - start_time > duration + 3:
            print("\n[WARN]  Timeout.")
            break

        waiting = ser.in_waiting
        if waiting > 0:
            need  = total_bytes - len(pcm_data)
            chunk = ser.read(min(waiting, need))
            pcm_data.extend(chunk)

        if time.time() - last_print >= 0.5:
            pct = len(pcm_data) / total_bytes * 100
            bar = "█" * int(pct / 5) + "░" * (20 - int(pct / 5))
            elapsed = time.time() - start_time
            print(f"\r[REC]   [{bar}] {pct:5.1f}%  {elapsed:.1f}s",
                  end="", flush=True)
            last_print = time.time()

        time.sleep(0.001)

    ser.close()

    # Đảm bảo chẵn bytes (int16)
    if len(pcm_data) % 2 != 0:
        pcm_data = pcm_data[:-1]

    print(f"\n[INFO]  Nhận được {len(pcm_data)} bytes "
          f"({len(pcm_data)//2} samples).")

    # Ghi WAV trực tiếp — data đã là int16 PCM chuẩn
    with wave.open(output, "wb") as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(bytes(pcm_data))

    kb = len(pcm_data) / 1024
    print(f"[DONE]  Đã lưu: {output}  ({kb:.1f} KB)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port",      default=DEFAULT_PORT)
    parser.add_argument("--baud",      default=DEFAULT_BAUDRATE, type=int)
    parser.add_argument("--duration",  default=DEFAULT_DURATION,  type=float)
    parser.add_argument("--output",    default=DEFAULT_OUTPUT)
    parser.add_argument("--auto-name", action="store_true")
    args = parser.parse_args()

    if args.auto_name:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output = f"rec_{ts}.wav"

    record(args.port, args.baud, args.duration, args.output)