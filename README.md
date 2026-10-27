# ct-scanner

적외선 LED와 포토다이오드로 만든 광학 CT 스캐너입니다. 아두이노로 투과광을 측정하고 파이썬으로 단면 이미지를 복원합니다.

## 소개

IR LED와 포토다이오드 배열 사이에서 물체를 스텝 모터로 회전시키며 각도별 투과광을 측정합니다. 측정값에 Beer-Lambert 법칙을 적용하고, Siddon 방식으로 만든 행렬을 SVD와 Tikhonov 정규화로 풀어 단면을 복원합니다. 측정 각도 수를 바꿔 가며 행렬 조건수와 복원 오차의 관계를 보는 탐구에 쓰였습니다.

## 주요 기능

- 아두이노 펌웨어(`ct_scanner.ino`): 모터를 돌려 각도마다 포토다이오드 6개 값을 읽고 CSV 형식으로 시리얼에 출력합니다. 기본 측정 횟수는 200회(1.8도 간격)이고 `MEAS_COUNT`로 바꿉니다.
- `serial_save.py`: 시리얼 출력을 `scan_data.csv`로 저장합니다.
- `ct_reconstruct.py`: CSV를 읽어 사이노그램, 복원 이미지, 특이값 분포를 그려 `ct_output/`에 저장합니다.
- `arduino_versions/`: 이전 버전 펌웨어 (`ct_scanner_1.ino`, `_3`, `_4`).

## 기술

- 하드웨어: IR LED 940nm, BPW34 포토다이오드 6개(A3~A8), DRV8825 드라이버와 스텝 모터 (1/16 마이크로스텝)
- 배치: LED, 130mm, 회전 중심, 70mm, 포토다이오드 배열
- Python: pyserial, numpy, matplotlib, pandas, scipy

## 실행 방법

```bash
pip install pyserial numpy matplotlib pandas scipy
```

1. Arduino IDE로 `ct_scanner.ino`를 업로드합니다. 시리얼 속도는 115200 baud입니다.
2. `serial_save.py`의 `PORT`를 본인 포트로 고치고(예: Windows `COM3`, Mac/Linux `/dev/ttyUSB0`) 실행합니다.
   ```bash
   python serial_save.py
   ```
   물체 없이 먼저 한 번 측정해 `ref.csv`로 이름을 바꿉니다. 이어 물체를 올려 측정하면 `scan_data.csv`가 저장됩니다.
3. `ct_reconstruct.py` 상단의 `CSV_PATH`, `REF_PATH`를 맞추고 실행합니다.
   ```bash
   python ct_reconstruct.py
   ```
   결과는 `ct_output/reconstruction.png`에 저장됩니다.

조정 항목: 거울상이면 `ROT_SIGN = -1`, 노이즈가 많으면 `LAMBDA`를 높이고 흐리면 낮춥니다. 모터 관성으로 물체가 흔들리면 `SETTLE_MS`를 늘립니다. 자세한 설명은 `사용가이드.md`에 있습니다.
