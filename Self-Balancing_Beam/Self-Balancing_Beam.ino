#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <math.h>

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

Adafruit_MPU6050 mpu;

static inline float rad2deg(float r) { return r * 57.2957795f; }
static inline int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// --- USTAWIENIA ---
const float LEVEL_TOL_DEG   = 9.0f;
const int   PRINT_PERIOD_MS = 150;

// Offset "zera"
float rollOffsetDeg = 0.0f;

// Filtr
float rollFiltDeg = 0.0f;
const float LPF_ALPHA = 0.85f;

// =========================
//      TARGET
// =========================
float targetDeg = 0.0f;
bool  targetReachedLatch = false;

// =========================
//      REGULATOR PID
// =========================
// Kp: [us/deg]
// Ki: [us/(deg*s)]
// Kd: [us/(deg/s)]   (D z żyroskopu: deg/s)
float Kp = 5.0f;
float Ki = 0.0f;
float Kd = 0.0f;

// Integrator (już "w us", bo dodajemy go bezpośrednio do u)
float iTerm = 0.0f;
const float ITERM_LIMIT_US = 200.0f;

// dt
uint32_t lastCtrlMs = 0;

// =========================
//      ESC / SILNIKI
// =========================
#define ESC_PIN_1 26
#define ESC_PIN_2 25
const int FREQUENCY = 50;

const int MIN_THROTTLE  = 1000;
const int MAX_THROTTLE  = 1700;

const int IDLE_THROTTLE = 1350;

const int ESC1_MIN_RUN = 1230;
const int ESC2_MIN_RUN = 1230;

const int   U_LIMIT_US = 300;
const float MOTOR_DEADBAND_DEG = 1.0f;

// TRIMY
int TRIM_ESC1_US = -105;
int TRIM_ESC2_US = 125;
const int TRIM_STEP = 5;

const int TRIM_MIN_US = -200;
const int TRIM_MAX_US = +200;

Servo esc1, esc2;
bool motorsEnabled = false;

// =========================
//      IMU
// =========================
float computeRollDegFromAccel(const sensors_event_t &a) {
  float ay = a.acceleration.y;
  float az = a.acceleration.z;
  return rad2deg(atan2f(ay, az));
}

float captureAverageRoll(int samples, int delayMs) {
  float sum = 0.0f;
  sensors_event_t a, g, t;

  for (int i = 0; i < samples; i++) {
    mpu.getEvent(&a, &g, &t);
    sum += computeRollDegFromAccel(a);
    delay(delayMs);
  }
  return sum / (float)samples;
}

// =========================
//      ESC helper (ZWROCI REALNE PWM)
// =========================
void writeESC(int &us1, int &us2) {
  if (motorsEnabled) {
    if (us1 < ESC1_MIN_RUN) us1 = ESC1_MIN_RUN;
    if (us2 < ESC2_MIN_RUN) us2 = ESC2_MIN_RUN;
  }

  us1 = clampi(us1, MIN_THROTTLE, MAX_THROTTLE);
  us2 = clampi(us2, MIN_THROTTLE, MAX_THROTTLE);

  esc1.writeMicroseconds(us1);
  esc2.writeMicroseconds(us2);
}

// =========================
//      Serial helper
// =========================
void printHelp() {
  Serial.println();
  Serial.println("=== KOMENDY ===");
  Serial.println("s         -> START (EN=1)");
  Serial.println("x         -> STOP  (EN=0, 1000us)");
  Serial.println("t<deg>    -> target, np: t30, t-15, t0");
  Serial.println("kp<val>   -> ustaw Kp, np: kp10, kp6.5");
  Serial.println("ki<val>   -> ustaw Ki, np: ki0, ki0.8");
  Serial.println("kd<val>   -> ustaw Kd, np: kd0, kd0.8");
  Serial.println("a+ / a-   -> trim ESC1 +/-5us");
  Serial.println("b+ / b-   -> trim ESC2 +/-5us");
  Serial.println("ta<us>    -> ustaw TRIM_ESC1_US, np: ta-15, ta20");
  Serial.println("tb<us>    -> ustaw TRIM_ESC2_US, np: tb10, tb-25");
  Serial.println("p         -> pokaz target/trim/Kp/Ki/Kd");
  Serial.println("h         -> help");
  Serial.println("================");
  Serial.println();
}

bool readLine(String &out) {
  if (!Serial.available()) return false;
  out = Serial.readStringUntil('\n');
  out.trim();
  return out.length() > 0;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!mpu.begin()) {
    Serial.println("BLAD: Nie wykryto MPU6050. Sprawdz polaczenia!");
    while (1) delay(100);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  delay(100);

  esc1.setPeriodHertz(FREQUENCY);
  esc2.setPeriodHertz(FREQUENCY);
  esc1.attach(ESC_PIN_1, 1000, 2000);
  esc2.attach(ESC_PIN_2, 1000, 2000);

  Serial.println("Arming ESC: 1000us przez 5s...");
  motorsEnabled = false;
  int u1 = MIN_THROTTLE, u2 = MIN_THROTTLE;
  writeESC(u1, u2);
  delay(5000);
  Serial.println("ESC uzbrojone.");

  Serial.println("Kalibracja zera: poloz urzadzenie poziomo i nie ruszaj (ok. 1s)...");
  rollOffsetDeg = captureAverageRoll(80, 10);
  Serial.print("rollOffsetDeg = ");
  Serial.println(rollOffsetDeg, 3);

  rollFiltDeg = 0.0f;
  targetDeg = 0.0f;
  targetReachedLatch = false;

  iTerm = 0.0f;
  lastCtrlMs = millis();

  Serial.println("START: silniki OFF (bezpiecznie).");
  Serial.print("TARGET="); Serial.println(targetDeg, 2);
  Serial.print("Kp="); Serial.print(Kp, 3);
  Serial.print("  Ki="); Serial.print(Ki, 3);
  Serial.print("  Kd="); Serial.println(Kd, 3);
  printHelp();
}

void loop() {
  // =========================
  // 1) Komendy z Serial
  // =========================
  String msg;
  if (readLine(msg)) {
    String low = msg;
    low.toLowerCase();

    if (low == "s") {
      motorsEnabled = true;
      targetReachedLatch = false;
      iTerm = 0.0f;
      lastCtrlMs = millis();
      Serial.println("MOTORS: START (EN=1)");

    } else if (low == "x") {
      motorsEnabled = false;
      iTerm = 0.0f;
      int u1 = MIN_THROTTLE, u2 = MIN_THROTTLE;
      writeESC(u1, u2);
      Serial.println("MOTORS: STOP (EN=0)");

    } else if (low.startsWith("t") && !low.startsWith("ta") && !low.startsWith("tb")) {
      String num = msg.substring(1);
      num.trim();
      targetDeg = num.toFloat();
      targetReachedLatch = false;
      iTerm = 0.0f;
      Serial.print("TARGET USTAWIONY: ");
      Serial.print(targetDeg, 2);
      Serial.println(" deg");

    } else if (low.startsWith("kp")) {
      String num = msg.substring(2);
      num.trim();
      Kp = num.toFloat();
      Serial.print("Kp ustawione: ");
      Serial.println(Kp, 4);

    } else if (low.startsWith("ki")) {
      String num = msg.substring(2);
      num.trim();
      Ki = num.toFloat();
      Serial.print("Ki ustawione: ");
      Serial.println(Ki, 4);

    } else if (low.startsWith("kd")) {
      String num = msg.substring(2);
      num.trim();
      Kd = num.toFloat();
      Serial.print("Kd ustawione: ");
      Serial.println(Kd, 4);

    } else if (low == "a+") {
      TRIM_ESC1_US = clampi(TRIM_ESC1_US + TRIM_STEP, TRIM_MIN_US, TRIM_MAX_US);
      Serial.print("TRIM_ESC1_US="); Serial.println(TRIM_ESC1_US);

    } else if (low == "a-") {
      TRIM_ESC1_US = clampi(TRIM_ESC1_US - TRIM_STEP, TRIM_MIN_US, TRIM_MAX_US);
      Serial.print("TRIM_ESC1_US="); Serial.println(TRIM_ESC1_US);

    } else if (low == "b+") {
      TRIM_ESC2_US = clampi(TRIM_ESC2_US + TRIM_STEP, TRIM_MIN_US, TRIM_MAX_US);
      Serial.print("TRIM_ESC2_US="); Serial.println(TRIM_ESC2_US);

    } else if (low == "b-") {
      TRIM_ESC2_US = clampi(TRIM_ESC2_US - TRIM_STEP, TRIM_MIN_US, TRIM_MAX_US);
      Serial.print("TRIM_ESC2_US="); Serial.println(TRIM_ESC2_US);

    } else if (low.startsWith("ta")) {
      String num = msg.substring(2);
      num.trim();
      TRIM_ESC1_US = clampi((int)roundf(num.toFloat()), TRIM_MIN_US, TRIM_MAX_US);
      Serial.print("TRIM_ESC1_US ustawione: ");
      Serial.println(TRIM_ESC1_US);

    } else if (low.startsWith("tb")) {
      String num = msg.substring(2);
      num.trim();
      TRIM_ESC2_US = clampi((int)roundf(num.toFloat()), TRIM_MIN_US, TRIM_MAX_US);
      Serial.print("TRIM_ESC2_US ustawione: ");
      Serial.println(TRIM_ESC2_US);

    } else if (low == "p") {
      Serial.print("TARGET="); Serial.println(targetDeg, 2);
      Serial.print("Kp="); Serial.print(Kp, 4);
      Serial.print("  Ki="); Serial.print(Ki, 4);
      Serial.print("  Kd="); Serial.println(Kd, 4);
      Serial.print("TRIM_ESC1_US (pin26)="); Serial.println(TRIM_ESC1_US);
      Serial.print("TRIM_ESC2_US (pin25)="); Serial.println(TRIM_ESC2_US);

    } else if (low == "h") {
      printHelp();
    } else {
      Serial.println("Nieznana komenda. Wpisz 'h' po pomoc.");
    }
  }

  // =========================
  // 2) IMU
  // =========================
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  float rollDeg = computeRollDegFromAccel(a) - rollOffsetDeg;
  rollFiltDeg = LPF_ALPHA * rollFiltDeg + (1.0f - LPF_ALPHA) * rollDeg;

  float rollRateDegPerSec = rad2deg(g.gyro.x);

  // dt w sekundach
  uint32_t nowMs = millis();
  float dt = (nowMs - lastCtrlMs) * 0.001f;
  if (dt < 0.0005f) dt = 0.0005f;
  lastCtrlMs = nowMs;

  // =========================
  // 3) Sterowanie PID
  // =========================
  int esc1_us = MIN_THROTTLE;
  int esc2_us = MIN_THROTTLE;

  float errorDeg = rollFiltDeg - targetDeg;

  if (motorsEnabled) {
    esc1_us = IDLE_THROTTLE + TRIM_ESC1_US;
    esc2_us = IDLE_THROTTLE + TRIM_ESC2_US;

    // Integrator
    if (fabs(errorDeg) > MOTOR_DEADBAND_DEG) {
      iTerm += (Ki * errorDeg * dt);
      iTerm = clampf(iTerm, -ITERM_LIMIT_US, +ITERM_LIMIT_US);
    }

    float u_f = (Kp * errorDeg) + iTerm + (Kd * rollRateDegPerSec);
    int u = (int)roundf(u_f);
    u = clampi(u, -U_LIMIT_US, +U_LIMIT_US);

    esc1_us += u / 2;
    esc2_us -= u / 2;

    // wysyłamy i jednocześnie urealniamy PWM po ograniczeniach
    writeESC(esc1_us, esc2_us);
  } else {
    iTerm = 0.0f;
    esc1_us = MIN_THROTTLE;
    esc2_us = MIN_THROTTLE;
    writeESC(esc1_us, esc2_us);
  }

  // =========================
  // 3b) LOG do PC (po PWM!)
  // Format: LOG,ms,roll,target,kp,ki,kd,pwm1,pwm2
  // =========================
  Serial.print("LOG,");
  Serial.print(nowMs);
  Serial.print(",");
  Serial.print(rollFiltDeg, 2);   // aktualny kąt
  Serial.print(",");
  Serial.print(targetDeg, 2);     // kąt zadany
  Serial.print(",");
  Serial.print(Kp, 4);
  Serial.print(",");
  Serial.print(Ki, 4);
  Serial.print(",");
  Serial.print(Kd, 4);
  Serial.print(",");
  Serial.print(esc1_us);          // PWM ESC1 (realnie wysłane)
  Serial.print(",");
  Serial.println(esc2_us);        // PWM ESC2 (realnie wysłane)

  // =========================
  // 4) Status
  // =========================
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= (uint32_t)PRINT_PERIOD_MS) {
    lastPrint = millis();

    if (fabs(errorDeg) <= LEVEL_TOL_DEG) {
      Serial.print("POZIOM  | roll=");
      Serial.println(rollFiltDeg, 2);
    } else if (errorDeg > 0) {
      Serial.print("PRAWO   | roll=");
      Serial.println(rollFiltDeg, 2);
    } else {
      Serial.print("LEWO    | roll=");
      Serial.println(rollFiltDeg, 2);
    }

    Serial.print("TARGET=");
    Serial.print(targetDeg, 2);
    Serial.print("  ERR=");
    Serial.print(errorDeg, 2);
    Serial.print("  Kp=");
    Serial.print(Kp, 3);
    Serial.print(" Ki=");
    Serial.print(Ki, 3);
    Serial.print(" Kd=");
    Serial.print(Kd, 3);
    Serial.print("  I=");
    Serial.println(iTerm, 1);

    if (!targetReachedLatch && fabs(errorDeg) <= LEVEL_TOL_DEG) {
      Serial.println(">>> TARGET OSIAGNIETY <<<");
      targetReachedLatch = true;
    }
  }
}
