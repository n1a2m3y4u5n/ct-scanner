/*
 * ============================================================
 *  광학 CT 스캐너 - 측정 펌웨어
 * ============================================================
 *  - IR LED 1개 투과광을 포토다이오드 9개(A0~A8)로 측정
 *  - DRV8825로 스텝 모터를 일정 각도씩 회전
 *  - 각 각도마다 9개 ADC값을 시리얼로 전송 (CSV 형식)
 *
 *  포토다이오드 배치: LED 방향에서 봤을 때 왼쪽부터 A0~A8
 * ============================================================
 */

// ── 핀 정의 ────────────────────────────────
const int PIN_STEP = 2;   // DRV8825 STEP → Arduino D2
const int PIN_DIR  = 3;   // DRV8825 DIR  → Arduino D3
// EN은 GND 직결(항상 활성)이면 코드 제어 불필요
// 만약 EN을 핀에 연결했다면 아래 주석 해제
// const int PIN_EN = 4;

const int NUM_PD = 9;                          // 포토다이오드 개수
const int PD_PINS[NUM_PD] = {A0, A1, A2, A3, A4, A5, A6, A7, A8};

// ── 측정 파라미터 ──────────────────────────
const int STEPS_PER_REV = 200;   // 모터 1회전 풀스텝 수 (1.8°/스텝)
                                 // ※ DRV8825 마이크로스텝 설정 안 했으면 200

const int MEAS_COUNT = 200;      // 한 바퀴를 몇 번 측정할지
                                 // 200 → 1.8°마다 (가장 촘촘)
                                 //  72 → 5°마다
                                 //  36 → 10°마다
                                 //  18 → 20°마다

// 한 번 측정 후 회전할 스텝 수
const int STEPS_PER_MEAS = STEPS_PER_REV / MEAS_COUNT;

// 측정 안정화 관련
const int   SETTLE_MS    = 50;   // 모터 정지 후 대기 (진동 안정화)
const int   ADC_SAMPLES  = 8;    // 노이즈 줄이려 평균낼 횟수
const int   STEP_DELAY_US = 1200; // 스텝 펄스 간격 (작을수록 빠름)

// ── 초기 설정 ──────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, HIGH);   // 회전 방향 (HIGH/LOW로 정/역 바꿈)

  // pinMode(PIN_EN, OUTPUT);
  // digitalWrite(PIN_EN, LOW);  // LOW = 모터 활성화

  // ADC 기준 외부 잡음 줄이기 위해 첫 측정 버림
  for (int i = 0; i < NUM_PD; i++) analogRead(PD_PINS[i]);

  delay(1000);

  // CSV 헤더 출력
  Serial.println(F("# 광학 CT 스캐너 측정 시작"));
  Serial.print(F("# 측정수="));   Serial.print(MEAS_COUNT);
  Serial.print(F(", 각도간격=")); Serial.print(360.0 / MEAS_COUNT, 2);
  Serial.println(F("deg"));
  Serial.println(F("angle_idx,angle_deg,A0,A1,A2,A3,A4,A5,A6,A7,A8"));
}

// ── 모터 한 스텝 ───────────────────────────
void stepOnce() {
  digitalWrite(PIN_STEP, HIGH);
  delayMicroseconds(STEP_DELAY_US);
  digitalWrite(PIN_STEP, LOW);
  delayMicroseconds(STEP_DELAY_US);
}

// ── 포토다이오드 1개 평균 측정 ──────────────
int readPD(int pin) {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin);
  }
  return (int)(sum / ADC_SAMPLES);
}

// ── 메인 루프 ──────────────────────────────
void loop() {
  static int angleIdx = 0;

  // 한 바퀴 다 돌면 정지
  if (angleIdx >= MEAS_COUNT) {
    Serial.println(F("# 측정 완료 (1회전)"));
    while (true) {
      // 정지. 리셋하면 다시 시작.
    }
  }

  // 1) 모터 진동 안정화 대기
  delay(SETTLE_MS);

  // 2) 9개 포토다이오드 측정 + CSV 출력
  float angleDeg = angleIdx * (360.0 / MEAS_COUNT);
  Serial.print(angleIdx);
  Serial.print(',');
  Serial.print(angleDeg, 2);

  for (int i = 0; i < NUM_PD; i++) {
    int val = readPD(PD_PINS[i]);
    Serial.print(',');
    Serial.print(val);
  }
  Serial.println();

  // 3) 다음 측정 위치로 회전
  for (int s = 0; s < STEPS_PER_MEAS; s++) {
    stepOnce();
  }

  angleIdx++;
}
