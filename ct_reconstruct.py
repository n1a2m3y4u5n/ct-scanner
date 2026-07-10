"""
광학 CT 스캐너 - 이미지 복원
==============================
Arduino CSV → Beer-Lambert → 행렬 A(Siddon) → SVD/Tikhonov → 단면 이미지
"""

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import pandas as pd
from scipy.linalg import lstsq
from pathlib import Path
import time

# ═══════════════════════════════════════════════════════════════
# ① 설정  ← 여기만 수정
# ═══════════════════════════════════════════════════════════════
CSV_PATH  = "scan_data.csv"   # Arduino 시리얼 출력 저장 파일
REF_PATH  = None              # 물체 없을 때 reference CSV (없으면 None)
SAVE_DIR  = "ct_output"       # 결과 저장 폴더

# 기하 파라미터 (mm)
D       = 200    # LED ↔ PD 전체 거리
R_LED   = 130    # LED → 회전중심 거리
PD_GAP  = 12     # PD 간격 (중심 간격)
N_PD    = 6      # A0~A2 불량 → A3~A8만 사용
                 # LED/회전중심을 A5-A6 중앙에 맞춤 → 6개 대칭 배치

# 복원 설정
N_GRID  = 21     # 격자 크기 N×N  (클수록 고해상도, 느림)
LAMBDA  = 0.01   # Tikhonov 정규화 (0=순수 SVD, 크면 부드러워짐)

# 이미지가 거울상이면 -1로 변경
ROT_SIGN = 1

# ═══════════════════════════════════════════════════════════════
# ② 기하 계산
# ═══════════════════════════════════════════════════════════════
R_PD   = D - R_LED                    # 회전중심 → PD = 70mm
SCAN_R = 2.5 * PD_GAP * (R_LED / D)  # 스캔 필드 반경 = 19.5mm (⌀39mm)
CELL   = 2 * SCAN_R / N_GRID          # 셀 크기 mm

# θ=0 기준 좌표  (새 회전중심 = 원점 = A5/A6 중앙)
# A3~A8 상대 y좌표: +30, +18, +6, -6, -18, -30 mm (완전 대칭)
LED0 = np.array([-R_LED, 0.0])
PD0  = np.column_stack([
    np.full(N_PD, R_PD),
    (np.array([2.5, 1.5, 0.5, -0.5, -1.5, -2.5])) * PD_GAP  # A3→+30, A8→-30
])

print("=" * 50)
print("광학 CT 스캐너 복원")
print("=" * 50)
print(f"스캔 필드:  ⌀{SCAN_R*2:.1f}mm")
print(f"격자 셀:    {CELL:.2f}mm")
print(f"격자 크기:  {N_GRID}×{N_GRID} = {N_GRID**2}개 미지수")

# ═══════════════════════════════════════════════════════════════
# ③ CSV 로드
# ═══════════════════════════════════════════════════════════════
def load_csv(path):
    df  = pd.read_csv(path, comment='#')
    ang = df['angle_deg'].values
    adc = df[['A3','A4','A5','A6','A7','A8']].values.astype(float)
    return ang, adc

angles_deg, adc = load_csv(CSV_PATH)
M_ANG   = len(angles_deg)
M_TOTAL = M_ANG * N_PD

print(f"\n측정 각도:  {M_ANG}개  ({360/M_ANG:.2f}°마다)")
print(f"총 측정수:  {M_TOTAL}  vs  미지수 {N_GRID**2}  →  {M_TOTAL/N_GRID**2:.1f}× 과결정")

# ═══════════════════════════════════════════════════════════════
# ④ Beer-Lambert 변환  →  b 벡터
# ═══════════════════════════════════════════════════════════════
if REF_PATH:
    _, adc_ref = load_csv(REF_PATH)
    I0 = adc_ref.mean(axis=0)
    print("\nI0: reference 파일에서 로드")
else:
    # 물체 없을 때 값을 따로 측정 안 했으면 각 PD 최대값으로 근사
    I0 = adc.max(axis=0)
    print("\nI0: 각 PD 최대값으로 근사 (reference 파일 없음)")

print(f"I0 (A0~A8): {I0.astype(int)}")

# ADC 0 방지 후 log 변환
ratio = np.clip(adc / I0[None, :], 1e-6, 1.0)
b     = -np.log(ratio).ravel()   # shape: (M_TOTAL,)

# ═══════════════════════════════════════════════════════════════
# ⑤ 행렬 A 구성  (Siddon 알고리즘)
# ═══════════════════════════════════════════════════════════════
def rotate_pts(xy, theta):
    """xy: (N,2) 배열, theta 라디안 회전"""
    c, s = np.cos(theta), np.sin(theta)
    R = np.array([[c, -s], [s, c]])
    return (R @ xy.T).T

def siddon(led, pd):
    """
    LED→PD 직선이 N_GRID×N_GRID 격자를 지나는 셀별 길이
    반환: dict {cell_index: length_mm}
    """
    dx = pd[0] - led[0]
    dy = pd[1] - led[1]
    ray_len = np.hypot(dx, dy)
    if ray_len < 1e-10:
        return {}

    t_list = [0.0, 1.0]

    if abs(dx) > 1e-10:
        for k in range(N_GRID + 1):
            t = (-SCAN_R + k * CELL - led[0]) / dx
            if 0.0 < t < 1.0:
                t_list.append(t)

    if abs(dy) > 1e-10:
        for k in range(N_GRID + 1):
            t = (-SCAN_R + k * CELL - led[1]) / dy
            if 0.0 < t < 1.0:
                t_list.append(t)

    t_arr = np.unique(np.round(t_list, 12))
    out   = {}

    for i in range(len(t_arr) - 1):
        t_mid = (t_arr[i] + t_arr[i + 1]) / 2
        seg   = (t_arr[i + 1] - t_arr[i]) * ray_len
        mx    = led[0] + t_mid * dx
        my    = led[1] + t_mid * dy
        cx    = int((mx + SCAN_R) / CELL)
        cy    = int((my + SCAN_R) / CELL)
        if 0 <= cx < N_GRID and 0 <= cy < N_GRID:
            idx     = cy * N_GRID + cx
            out[idx] = out.get(idx, 0.0) + seg

    return out

print(f"\n행렬 A 구성 중...  ({M_TOTAL} × {N_GRID**2})", end='', flush=True)
t0 = time.time()

A   = np.zeros((M_TOTAL, N_GRID ** 2), dtype=np.float32)
row = 0

for angle_deg in angles_deg:
    theta  = np.radians(ROT_SIGN * angle_deg)
    led_r  = rotate_pts(LED0[None, :], -theta)[0]
    pd_r   = rotate_pts(PD0, -theta)

    for i in range(N_PD):
        for cell_idx, length in siddon(led_r, pd_r[i]).items():
            A[row, cell_idx] = length
        row += 1

elapsed = time.time() - t0
nz      = np.count_nonzero(A)
print(f"  완료 ({elapsed:.1f}초)")
print(f"비영 원소: {nz} / {A.size}  ({100*nz/A.size:.2f}%)")

# ═══════════════════════════════════════════════════════════════
# ⑥ 복원  (Tikhonov 정규화)
# ═══════════════════════════════════════════════════════════════
print("\n복원 중...", end='', flush=True)
t0 = time.time()

if LAMBDA == 0:
    x, _, _, _ = lstsq(A, b)
else:
    ATA = A.T @ A
    ATb = A.T @ b
    x   = np.linalg.solve(ATA + LAMBDA * np.eye(N_GRID ** 2), ATb)

img = np.clip(x, 0, None).reshape(N_GRID, N_GRID)
print(f"  완료 ({time.time()-t0:.1f}초)")

# 조건수 (특이값 기반)
sv    = np.linalg.svd(A, compute_uv=False)
kappa = sv[0] / sv[-1]
print(f"κ(A) = {kappa:.3e}   (작을수록 복원 안정)")

# ═══════════════════════════════════════════════════════════════
# ⑦ 시각화
# ═══════════════════════════════════════════════════════════════
Path(SAVE_DIR).mkdir(exist_ok=True)

fig = plt.figure(figsize=(17, 5.5), facecolor='#0f172a')
fig.suptitle(
    f'광학 CT 스캐너 복원   |   {N_GRID}×{N_GRID} 격자   |   λ={LAMBDA}   |   κ(A)={kappa:.2e}',
    color='white', fontsize=13, fontweight='bold', y=1.01
)
gs = gridspec.GridSpec(1, 3, figure=fig, wspace=0.38)

# ─── [1] 사이노그램 ───────────────────────
ax1 = fig.add_subplot(gs[0])
ax1.set_facecolor('#1e293b')
sino = ratio   # (M_ANG, N_PD)  투과율 0~1
im1  = ax1.imshow(
    sino, aspect='auto', cmap='inferno',
    extent=[-0.5, N_PD - 0.5, angles_deg[-1], angles_deg[0]],
    vmin=0, vmax=1
)
plt.colorbar(im1, ax=ax1, label='투과율  I/I₀', fraction=0.046, pad=0.04)
ax1.set_xticks(range(N_PD))
ax1.set_xticklabels([f'A{i}' for i in range(3, 9)], fontsize=8)
ax1.set_xlabel('포토다이오드', color='#94a3b8')
ax1.set_ylabel('각도 (deg)',   color='#94a3b8')
ax1.set_title('사이노그램\n(원시 투과 데이터)', color='white', fontsize=11)

# ─── [2] 복원 이미지 ─────────────────────
ax2 = fig.add_subplot(gs[1])
ax2.set_facecolor('#1e293b')
ext = [-SCAN_R, SCAN_R, -SCAN_R, SCAN_R]
im2 = ax2.imshow(img, cmap='hot', origin='lower', extent=ext)
plt.colorbar(im2, ax=ax2, label='흡수 계수 μ', fraction=0.046, pad=0.04)
ax2.add_patch(plt.Circle(
    (0, 0), SCAN_R, fill=False,
    color='#22c55e', linestyle='--', linewidth=1.2, alpha=0.6
))
ax2.set_xlabel('x (mm)', color='#94a3b8')
ax2.set_ylabel('y (mm)', color='#94a3b8')
ax2.set_title('복원 단면 이미지\n(밝을수록 IR 흡수 큼)', color='white', fontsize=11)

# ─── [3] 특이값 분포 ─────────────────────
ax3 = fig.add_subplot(gs[2])
ax3.set_facecolor('#1e293b')
ax3.semilogy(sv, color='#60a5fa', linewidth=1.5, label='특이값')
ax3.axhline(sv[0],  color='#4ade80', linestyle='--', lw=1.2,
            label=f'σ_max = {sv[0]:.2e}')
ax3.axhline(sv[-1], color='#f87171', linestyle='--', lw=1.2,
            label=f'σ_min = {sv[-1]:.2e}')
ax3.set_xlabel('인덱스',        color='#94a3b8')
ax3.set_ylabel('특이값 크기',   color='#94a3b8')
ax3.set_title(f'특이값 분포\nκ(A) = {kappa:.3e}', color='white', fontsize=11)
ax3.legend(fontsize=8.5, facecolor='#1e293b', labelcolor='white',
           framealpha=0.8, edgecolor='#334155')
ax3.grid(True, color='#0f172a', alpha=0.6)

# 공통 스타일
for ax in [ax1, ax2, ax3]:
    for sp in ax.spines.values():
        sp.set_color('#334155')
    ax.tick_params(colors='#475569', labelsize=8.5)

out = f"{SAVE_DIR}/reconstruction.png"
plt.savefig(out, dpi=150, bbox_inches='tight', facecolor='#0f172a')
plt.show()

print(f"\n결과 저장: {out}")
print("=" * 50)
print("이미지 해석:")
print("  밝은 영역 = IR 흡수 큼  (PLA 벽, 고밀도 재질)")
print("  어두운 영역 = IR 잘 통과 (공기, 공동)")
print()
print("이미지가 거울상이면: ROT_SIGN = -1 로 바꾸고 재실행")
print(f"복원이 너무 노이즈: LAMBDA 높이기  (현재 {LAMBDA})")
print(f"복원이 너무 흐릿:   LAMBDA 낮추기  (현재 {LAMBDA})")
