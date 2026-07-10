/*
 * ============================================================
 *  광학 CT 스캐너 - 측정 펌웨어 v2 (관성 문제 해결)
 * ============================================================
 *  변경사항:
 *    - 가속/감속 램프 추가 (급출발·급정지 제거)
 *    - 마이크로스텝 대응 (STEPS_PER_REV 조정만 하면 됨)
 *    - SETTLE_MS 분리 (회전 후 진동 대기)
 * ============================================================
 *  [DRV8825 마이크로스텝 설정]
 *   풀스텝     M0=GND M1=GND M2=GND  → STEPS_PER_REV = 200
 *   1/2 스텝  M0=5V  M1=GND M2=GND  → STEPS_PER_REV = 400
 *   1/4 스텝  M0=GND M1=5V  M2=GND  → STEPS_PER_REV = 800
 *   1/8 스텝  M0=5V  M1=5V  M2=GND  → STEPS_PER_REV = 1600
 *   1/16 스텝 M0=GND M1=GND M2=5V   → STEPS_PER_REV = 3200  ← 추천
 *   1/32 스텝 M0=5V  M1=GND M2=5V   → STEPS_PER_REV = 6400
 * ============================================================
 */

// ── 핀 정의 ────────────────────────────────────────────────
const int PIN_STEP = 2;
const int PIN_DIR  = 3;

const int NUM_PD = 9;
const int PD_PINS[NUM_PD] = {A0, A1, A2, A3, A4, A5, A6, A7, A8};

// ── 파라미터 ───────────────────────────────────────────────
// ※ M2만 5V 연결 시 1/16 스텝 → 3200으로 변경
const int STEPS_PER_REV = 200;    // 현재 풀스텝 기준

const int MEAS_COUNT = 200;       // 측정 횟수 (360° / MEAS_COUNT = 각도 간격)
                                  //   200 → 1.8°   72 → 5°   36 → 10°

// 스텝 속도 (클수록 느림, 느릴수록 관성 적음)
const int STEP_DELAY_MIN = 1500;  // 최고속 (μs) - 중간 구간
const int STEP_DELAY_MAX = 6000;  // 저속 (μs)   - 가속/감속 구간

// 가속/감속 구간 스텝 수
const int RAMP_STEPS = 8;         // 작으면 급출발, 크면 부드럽지만 느림

// 측정 안정화
const int SETTLE_MS  = 80;        // 회전 완료 후 측정 전 대기 (ms)
const int ADC_SAMPLES = 16;       // 평균 샘플 수 (많을수록 노이즈↓)

// ── 계산값 ─────────────────────────────────────────────────
const int STEPS_PER_MEAS = STEPS_PER_REV / MEAS_COUNT;

// ── 초기 설정 ──────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR,  OUTPUT);
  digitalWrite(PIN_DIR, HIGH);

  // ADC 워밍업 (첫 몇 번 버림)
  for (int i = 0; i < NUM_PD; i++) {
    analogRead(PD_PINS[i]);
    analogRead(PD_PINS[i]);
  }

  delay(1500);  // 시리얼 연결 대기

  Serial.println(F("# ========================================="));
  Serial.println(F("# 광학 CT 스캐너 v2"));
  Serial.print  (F("# 측정수      = ")); Serial.println(MEAS_COUNT);
  Serial.print  (F("# 각도 간격   = ")); Serial.print(360.0 / MEAS_COUNT, 2); Serial.println(F(" deg"));
  Serial.print  (F("# 스텝/측정   = ")); Serial.println(STEPS_PER_MEAS);
  Serial.println(F("# ========================================="));
  Serial.println(F("angle_idx,angle_deg,A0,A1,A2,A3,A4,A5,A6,A7,A8"));
}

// ── 가속/감속 램프 스텝 ────────────────────────────────────
//   totalSteps: 이번에 돌릴 총 스텝 수
void stepWithRamp(int totalSteps) {
  if (totalSteps <= 0) return;

  int ramp = min(RAMP_STEPS, totalSteps / 2);

  for (int s = 0; s < totalSteps; s++) {
    int d;
    if (ramp > 0 && s < ramp) {
      // 가속 구간: MAX → MIN
      d = STEP_DELAY_MAX - (long)(STEP_DELAY_MAX - STEP_DELAY_MIN) * s / ramp;
    } else if (ramp > 0 && s >= totalSteps - ramp) {
      // 감속 구간: MIN → MAX
      int remaining = totalSteps - s - 1;
      d = STEP_DELAY_MAX - (long)(STEP_DELAY_MAX - STEP_DELAY_MIN) * remaining / ramp;
    } else {
      // 정속 구간
      d = STEP_DELAY_MIN;
    }

    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(d);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(d);
  }
}

// ── 포토다이오드 평균 측정 ─────────────────────────────────
int readPD(int pin) {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin);
  }
  return (int)(sum / ADC_SAMPLES);
}

// ── 메인 루프 ──────────────────────────────────────────────
void loop() {
  static int angleIdx = 0;

  if (angleIdx >= MEAS_COUNT) {
    Serial.println(F("# ========================================="));
    Serial.println(F("# 측정 완료 (1회전)"));
    Serial.println(F("# 리셋하면 재시작"));
    Serial.println(F("# ========================================="));
    while (true) {}
  }

  // 1) 회전 후 진동 안정화 대기
  delay(SETTLE_MS);

  // 2) 측정 + CSV 출력
  float angleDeg = angleIdx * (360.0f / MEAS_COUNT);
  Serial.print(angleIdx);
  Serial.print(',');
  Serial.print(angleDeg, 2);
  for (int i = 0; i < NUM_PD; i++) {
    Serial.print(',');
    Serial.print(readPD(PD_PINS[i]));
  }
  Serial.println();

  // 3) 다음 위치로 이동 (가속→정속→감속)
  stepWithRamp(STEPS_PER_MEAS);

  angleIdx++;
}
