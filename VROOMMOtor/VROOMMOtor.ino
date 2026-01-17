// =============================================================
// MOTOR ESP32: ESP32 + L298N (HW-095) Stepper + 4 microswitches + Servo
//
// Now supports commands from:
//   - Serial Monitor (USB)  -> type 1/2/3/4 + Enter
//   - Serial2 (UART)        -> receives "1\n".."4\n" from AI ESP32
//
// Serial2 pins (default):
//   RX2 = GPIO16  (connect from AI ESP32 TX2 GPIO17)
//   TX2 = GPIO17  (not required unless you want feedback)
// IMPORTANT: Common GND between boards.
//
// Switch wiring (NO type):
//   COM -> GND
//   NO  -> GPIO
//   pressed = LOW
// NOTE: GPIO34-39 have NO internal pullups -> use external 10k pull-up to 3.3V
//
// SW1=36, SW2=39, SW3=34, SW4=35
// =============================================================

#include <ESP32Servo.h>

// ----------------- Stepper pins (L298N) -----------------
int IN1 = 14, IN2 = 27, IN3 = 26, IN4 = 25;

// ----------------- Switches -----------------
const int NUM_SW = 4;
const int swPins[NUM_SW]    = {36, 39, 34, 35};
const char* swNames[NUM_SW] = {"SW1","SW2","SW3","SW4"};

// Full-step sequence
int seq[4][4] = {
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1}
};

// ----------------- Stepper timing -----------------
int stepIndex = 0;
unsigned long lastStepMs = 0;
const unsigned long stepIntervalMs = 25; // bigger = slower

// ----------------- Switch debounce -----------------
bool rawLast[NUM_SW] = {0,0,0,0};
bool stablePressed[NUM_SW] = {0,0,0,0}; // true = pressed
unsigned long lastChangeMs[NUM_SW] = {0,0,0,0};
const unsigned long debounceMs = 30;

// ----------------- Targeting + Position -----------------
int currentPos = 0;       // 0..3
bool running = false;
int target = -1;

// direction: +1 = CW, -1 = CCW
int runDir = +1;

// ----------------- Rotation Limitation -----------------
const int MAX_STEPS = 200;
const int MIN_STEPS_CCW = -200;
int stepCounter = 0;

// ----------------- Servo (HOLD at 90) -----------------
Servo myServo;
const int SERVO_PIN = 4;
const int SERVO_MIN_US = 500;
const int SERVO_MAX_US = 2400;

const int SERVO_UPRIGHT = 90;
const int SERVO_DROP    = 0;

const unsigned long SERVO_DROP_MS = 5000;

// Non-blocking servo state
bool servoDropping = false;
unsigned long servoDropStartMs = 0;

// ----------------- UART from AI board -----------------
#define RX2_PIN 16
#define TX2_PIN 17

// =============================================================

void stepMotorApply(int i) {
  digitalWrite(IN1, seq[i][0]);
  digitalWrite(IN2, seq[i][1]);
  digitalWrite(IN3, seq[i][2]);
  digitalWrite(IN4, seq[i][3]);
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void checkRotationLimit() {
  if (stepCounter >= MAX_STEPS) {
    Serial.println("Rotation limit reached. Reversing direction...");
    runDir = -runDir;
    stepCounter = 0;
  }
}

void printStepStatus() {
  if (runDir >= 0) Serial.printf("Clockwise: Step Count = %d\n", stepCounter);
  else            Serial.printf("Counterclockwise: Step Count = %d\n", stepCounter);
}

// CW: stepIndex++ ; CCW: stepIndex--
void stepOnceDir(int dir) {
  if (dir >= 0) stepIndex = (stepIndex + 1) & 3;
  else          stepIndex = (stepIndex + 3) & 3;
  stepMotorApply(stepIndex);

  stepCounter += (dir >= 0 ? 1 : -1);

  if (stepCounter <= MIN_STEPS_CCW) {
    stepCounter = MIN_STEPS_CCW;
    runDir = +1;
  }

  checkRotationLimit();
  printStepStatus();
}

void updateSwitches() {
  unsigned long now = millis();

  for (int i=0; i<NUM_SW; i++) {
    bool raw = (digitalRead(swPins[i]) == LOW);

    if (raw != rawLast[i]) {
      rawLast[i] = raw;
      lastChangeMs[i] = now;
    }

    if (now - lastChangeMs[i] > debounceMs) {
      stablePressed[i] = raw;
    }
  }
}

void resetSwitchLogic() {
  unsigned long now = millis();
  for (int i = 0; i < NUM_SW; i++) {
    stablePressed[i] = false;
    rawLast[i] = false;
    lastChangeMs[i] = now;
  }
}

int chooseNearestDir(int cur, int tgt) {
  int cwSteps  = (tgt - cur + 4) % 4;
  int ccwSteps = (cur - tgt + 4) % 4;

  if (cwSteps < ccwSteps) return +1;
  if (ccwSteps < cwSteps) return -1;
  return runDir;
}

// ----------------- Servo state machine -----------------
void servoInitHoldUpright() {
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
  myServo.write(SERVO_UPRIGHT);
  delay(300);
}

void servoTriggerDrop() {
  if (servoDropping) return;
  servoDropping = true;
  servoDropStartMs = millis();
  myServo.write(SERVO_DROP);
}

void servoUpdate() {
  if (!servoDropping) return;
  if (millis() - servoDropStartMs >= SERVO_DROP_MS) {
    servoDropping = false;
    myServo.write(SERVO_UPRIGHT);
  }
}

// ----------------- COMMAND HANDLER -----------------
void startMoveToCmd(int cmd) {
  if (cmd < 1 || cmd > 4) {
    Serial.println("Command must be 1..4");
    return;
  }

  int idx = cmd - 1;

  // If already pressed, dispense immediately
  if (stablePressed[idx]) {
    Serial.printf("%s already PRESSED -> Dispense now.\n", swNames[idx]);
    servoTriggerDrop();
    resetSwitchLogic();
    return;
  }

  target = idx;
  runDir = chooseNearestDir(currentPos, target);

  running = true;
  stopMotor();
  lastStepMs = millis();

  Serial.printf("CMD %d -> Target %s (GPIO%d)\n", cmd, swNames[target], swPins[target]);
  Serial.printf("CurrentPos=%s -> Move %s (nearest)\n",
                swNames[currentPos], (runDir >= 0 ? "CLOCKWISE" : "ANTICLOCKWISE"));
  Serial.println("Running now...");
}

// Read a line from a Stream safely (needs newline on sender)
bool readLine(Stream &S, String &out) {
  if (!S.available()) return false;
  out = S.readStringUntil('\n');
  out.trim();
  return out.length() > 0;
}

// ----------------- Main motor runner -----------------
void runMotorToTarget() {
  if (!running || target < 0) return;

  if (stablePressed[target]) {
    stopMotor();
    running = false;

    Serial.printf("%s HIT -> Motor stopped.\n", swNames[target]);
    currentPos = target;

    Serial.println("Dispense -> servo 0 for 5s, then back to 90");
    servoTriggerDrop();

    target = -1;
    resetSwitchLogic();
    Serial.println("Ready for next command.");
    return;
  }

  unsigned long now = millis();
  if (now - lastStepMs >= stepIntervalMs) {
    lastStepMs = now;
    stepOnceDir(runDir);
  }
}

// ----------------- Startup detect -----------------
void detectStartPosition() {
  for (int i = 0; i < NUM_SW; i++) {
    if (stablePressed[i]) {
      currentPos = i;
      Serial.printf("Startup detect: currentPos = %s\n", swNames[currentPos]);
      return;
    }
  }
  Serial.printf("Startup detect: no switch pressed -> default currentPos = %s\n", swNames[currentPos]);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(20);

  Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);
  Serial2.setTimeout(20);

  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  stopMotor();

  for (int i=0; i<NUM_SW; i++) pinMode(swPins[i], INPUT); // external pull-ups required

  servoInitHoldUpright();

  Serial.println("Ready. Commands: 1/2/3/4 from Serial OR Serial2 (AI board).");

  for (int k = 0; k < 8; k++) { updateSwitches(); delay(10); }
  detectStartPosition();
}

void loop() {
  updateSwitches();

  // Accept from Serial Monitor
  String s;
  if (readLine(Serial, s)) {
    int cmd = s.toInt();
    startMoveToCmd(cmd);
  }

  // Accept from AI board via Serial2
  if (readLine(Serial2, s)) {
    int cmd = s.toInt();
    Serial.printf("[AI->UART] got cmd=%d\n", cmd);
    startMoveToCmd(cmd);
  }

  runMotorToTarget();
  servoUpdate();
}
