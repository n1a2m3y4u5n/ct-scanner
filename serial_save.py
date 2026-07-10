"""
시리얼 데이터 저장
==================
Arduino 측정 중 시리얼 출력을 CSV로 저장
"""

import serial
import time

PORT    = 'COM3'          # ← Windows: 'COM3', Mac/Linux: '/dev/ttyUSB0'
BAUD    = 115200
OUTFILE = 'scan_data.csv'

print(f"포트 {PORT} 연결 중...")
ser = serial.Serial(PORT, BAUD, timeout=3)
time.sleep(2)             # Arduino 리셋 대기

print(f"저장 시작 → {OUTFILE}")
print("-" * 40)

with open(OUTFILE, 'w', encoding='utf-8') as f:
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            continue
        print(line)
        f.write(line + '\n')
        f.flush()
        if '측정 완료' in line:
            print("-" * 40)
            print(f"완료! → {OUTFILE}")
            break

ser.close()
