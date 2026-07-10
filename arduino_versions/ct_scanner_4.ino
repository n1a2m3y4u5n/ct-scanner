/*
 * ============================================================
 *  광학 CT 스캐너 - 측정 펌웨어 v3
 * ============================================================
 *  하드웨어:
 *    - IR LED 940nm (고정)
 *    - BPW34 포토다이오드 × 6개 (A3~A8, A0~A2 불량)
 *    - DRV8825 + 스텝 모터 (M2→5V, 1/16 마이크로스텝)
 *    - LED/회전중심: A5-A6 중앙 높이 정렬
 *
 *  배치:
 *    [LED] ── 130mm ── [모터/물체] ── 70mm ── [PD 배열]
 * ============================================================
 */

// ── 핀 정의 ──────────────────────────────────────
const int PIN_STEP = 2;
const int PIN_DIR  = 3;

const int NUM_PD = 6;
const int PD_PINS[NUM_PD] = {A3, A4, A5, A6, A7, A8};  // A0~A2 불량

// ── 파라미터 ─────────────────────────────────────
const int STEPS_PER_REV = 3200;   // M2→5V (1/16 마이크로스텝)

const int MEAS_COUNT = 200;        // 측정 횟수
                                   //   200 → 1.8°마다
                                   //    72 → 5°마다
                                   //    36 → 10°마다

const int STEP_DELAY_MIN  = 1500;  // 최고속 (μs)
const int STEP_DELAY_MAX  = 6000;  // 가감속 구간 저속 (μs)
const int RAMP_STEPS      = 8;     // 가감속 구간 스텝 수

const int SETTLE_MS   = 80;        // 회전 후 안정화 대기 (ms)
const int ADC_SAMPLES = 16;        // 노이즈 평균 샘플 수

// ── 계산값 ───────────────────────────────────────
const int STEPS_PER_MEAS = STEPS_PER_REV / MEAS_COUNT;  // = 16

// ── 초기 설정 ────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR,  OUTPUT);
  digitalWrite(PIN_DIR, HIGH);

  for (int i = 0; i < NUM_PD; i++) {
    analogRead(PD_PINS[i]);
    analogRead(PD_PINS[i]);
  }
  delay(1500);

  Serial.println(F("# ========================================="));
  Serial.println(F("# 광학 CT 스캐너 v3"));
  Serial.print  (F("# 측정수    = ")); Serial.println(MEAS_COUNT);
  Serial.print  (F("# 각도간격  = ")); Serial.print(360.0 / MEAS_COUNT, 2); Serial.println(F(" deg"));
  Serial.print  (F("# 스텝/측정 = ")); Serial.println(STEPS_PER_MEAS);
  Serial.println(F("# ========================================="));
  Serial.println(F("angle_idx,angle_deg,A3,A4,A5,A6,A7,A8"));
}

// ── 가감속 램프 ──────────────────────────────────
void stepWithRamp(int totalSteps) {
  if (totalSteps <= 0) return;
  int ramp = min(RAMP_STEPS, totalSteps / 2);

  for (int s = 0; s < totalSteps; s++) {
    int d;
    if (ramp > 0 && s < ramp)
      d = STEP_DELAY_MAX - (long)(STEP_DELAY_MAX - STEP_DELAY_MIN) * s / ramp;
    else if (ramp > 0 && s >= totalSteps - ramp)
      d = STEP_DELAY_MAX - (long)(STEP_DELAY_MAX - STEP_DELAY_MIN) * (totalSteps - s - 1) / ramp;
    else
      d = STEP_DELAY_MIN;

    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(d);
    digitalWrite(PIN_STEP, LOW);  delayMicroseconds(d);
  }
}

// ── PD 평균 측정 ─────────────────────────────────
int readPD(int pin) {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) sum += analogRead(pin);
  return (int)(sum / ADC_SAMPLES);
}

// ── 메인 루프 ────────────────────────────────────
void loop() {
  static int angleIdx = 0;

  if (angleIdx >= MEAS_COUNT) {
    Serial.println(F("# 측정 완료 — 리셋하면 재시작"));
    while (true) {}
  }

  delay(SETTLE_MS);

  Serial.print(angleIdx);
  Serial.print(',');
  Serial.print(angleIdx * (360.0f / MEAS_COUNT), 2);
  for (int i = 0; i < NUM_PD; i++) {
    Serial.print(',');
    Serial.print(readPD(PD_PINS[i]));
  }
  Serial.println();

  stepWithRamp(STEPS_PER_MEAS);
  angleIdx++;
}
