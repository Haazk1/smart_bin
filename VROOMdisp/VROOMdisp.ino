#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- WiFi ----------------
const char* ssid     = "Haazzzz";
const char* password = "haa12345";

// ---------------- URLs ----------------
const char* cam_url    = "http://172.20.10.2/capture";
const char* server_url = "http://172.20.10.7:8000/upload";

// ---------------- Ultrasonic ----------------
#define TRIG_PIN 5
#define ECHO_PIN 18
bool objectDetected = false;

long getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2;
}

// ================== UART TO MOTOR ESP32 ==================
#define RX2_PIN 16
#define TX2_PIN 17

// ================== JOYSTICK ==================
#define VRX_PIN  39
#define VRY_PIN  36
#define SW_PIN   25

#define LEFT_THRESHOLD   1000
#define RIGHT_THRESHOLD  3000
#define UP_THRESHOLD     1000
#define DOWN_THRESHOLD   3000

#define COMMAND_NO       0x00
#define COMMAND_LEFT     0x01
#define COMMAND_RIGHT    0x02
#define COMMAND_UP       0x04
#define COMMAND_DOWN     0x08
#define COMMAND_PRESS    0x10

int valueX = 0;
int valueY = 0;
int command = COMMAND_NO;

// ================== MENUS ==================
enum MenuState {
  MENU_MAIN,
  MENU_SCAN,
  MENU_RESULT,
  MENU_LEARN,
  MENU_LEARN_CATEGORY,
  MENU_STATUS,
  MENU_SETTINGS
};

MenuState currentMenu = MENU_MAIN;
int mainCursor  = 0;
int learnCursor = 0;

// ================== AI RESULT STATE ==================
String lastResult = "No scan yet";

// ================== LEARN RECYCLING DATA ==================
String categories[4] = {"Plastic", "Paper", "Metal", "Trash"};

String plasticInfo[3] = {"Bottles OK","Containers OK","Straws NO"};
String paperInfo[3]   = {"Cardboard OK","Newspapers OK","Dirty Paper NO"};
String metalInfo[3]   = {"Cans OK","Foil OK","Paint Cans NO"};
String trashInfo[3]   = {"Food Waste NO","Tissue NO","Recycle Correctly!"};

// ---------------------------------------------------------------------
// READ JOYSTICK → COMMAND BITMASK
// ---------------------------------------------------------------------
int readJoystickCommand() {
  valueX = analogRead(VRX_PIN);
  valueY = analogRead(VRY_PIN);
  int sw = digitalRead(SW_PIN);

  command = COMMAND_NO;

  if (valueX < LEFT_THRESHOLD)      command |= COMMAND_LEFT;
  else if (valueX > RIGHT_THRESHOLD) command |= COMMAND_RIGHT;

  // swapped UP/DOWN (as in your file)
  if (valueY < UP_THRESHOLD)        command |= COMMAND_DOWN;
  else if (valueY > DOWN_THRESHOLD) command |= COMMAND_UP;

  if (sw == LOW) command |= COMMAND_PRESS;

  return command;
}

// ---------------------------------------------------------------------
// MAP AI TEXT → CMD NUMBER (plastic=1 paper=2 metal=3 trash=4)
// ---------------------------------------------------------------------
int categoryToCmd(String s) {
  s.trim();
  s.toLowerCase();
  s.replace("\r", " ");
  s.replace("\n", " ");

  if (s.indexOf("plastic") != -1) return 1;
  if (s.indexOf("paper")   != -1) return 2;
  if (s.indexOf("metal")   != -1) return 3;
  if (s.indexOf("trash")   != -1) return 4;

  return 0;
}

void sendCmdToMotor(int cmd) {
  if (cmd < 1 || cmd > 4) return;
  Serial2.printf("%d\n", cmd);      // send to motor ESP32
  Serial.printf("➡ Sent to motor: %d\n", cmd);
}

// ---------------------------------------------------------------------
// CAMERA + GEMINI SCAN
// ---------------------------------------------------------------------
void performScan() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Scanning...");
  display.println("Waiting for AI...");
  display.display();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected!");
    lastResult = "WiFi not connected!";
    return;
  }

  HTTPClient httpCam;
  httpCam.begin(cam_url);
  int code = httpCam.GET();

  if (code == HTTP_CODE_OK) {

    WiFiClient *stream = httpCam.getStreamPtr();
    int len = httpCam.getSize();

    if (len <= 0) {
      Serial.println("❌ Invalid JPEG length!");
      httpCam.end();
      lastResult = "Invalid JPEG length!";
      return;
    }

    Serial.printf("CAM declares: %d bytes\n", len);

    uint8_t *buffer = new uint8_t[len];

    int readLen = stream->readBytes(buffer, len);
    Serial.printf("Actually read: %d bytes\n", readLen);

    httpCam.end();

    if (readLen != len) {
      Serial.println("❌ Incomplete JPEG detected — skipping frame");
      lastResult = "Incomplete JPEG!";
      delete[] buffer;
      return;
    }

    Serial.println("✔ FULL JPEG RECEIVED");

    HTTPClient httpPost;
    httpPost.begin(server_url);
    httpPost.addHeader("Content-Type", "image/jpeg");
    httpPost.setTimeout(15000);

    int postCode = httpPost.POST(buffer, readLen);

    if (postCode == HTTP_CODE_OK) {
      String response = httpPost.getString();
      Serial.println("✅ Image sent!");
      Serial.println("💬 Gemini says: " + response);

      lastResult = response;

      // ✅ AUTO SEND COMMAND
      int cmd = categoryToCmd(lastResult);
      if (cmd != 0) sendCmdToMotor(cmd);
      else Serial.println("⚠ Could not detect category in AI text.");

      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Scan complete!");
      display.println("Result saved.");
      display.display();

    } else {
      Serial.printf("❌ Upload failed: %d\n", postCode);
      lastResult = "Upload failed: " + String(postCode);

      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Upload failed!");
      display.display();
    }

    httpPost.end();
    delete[] buffer;

  } else {
    Serial.printf("⚠️ Camera GET failed: %d\n", code);
    lastResult = "Camera GET failed: " + String(code);
  }
}

// ================== DRAW FUNCTIONS ==================

void drawMainMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SMART RECYCLE BIN");
  display.println("------------------");

  String items[5] = {
    "Start Scan",
    "View Result",
    "Learn Recycling",
    "System Status",
    "Settings"
  };

  for (int i = 0; i < 5; i++) {
    display.print((i == mainCursor) ? "> " : "  ");
    display.println(items[i]);
  }

  display.display();
}

void drawScanScreen(long distance) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("START SCAN");
  display.println("----------");
  display.println("Place an item");
  display.println("< 15cm from bin");
  display.println("");
  display.print("Dist: ");
  display.print(distance);
  display.println(" cm");
  display.println("LEFT/OK: back");
  display.display();
}

void drawResultScreen() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("AI RESULT");
  display.println("---------");

  int16_t x1, y1;
  uint16_t w, h;

  int x = 0;
  int y = 16;
  int lineH = 8;
  int maxW = 128;

  // Clean text so "Category: trash" stays on ONE LINE
  String text = lastResult;
  text.replace("\r", " ");
  text.replace("\n", " ");
  text.replace("category:", "Category:");
  text.replace("Category:  ", "Category: ");
  text.replace("Category:\t", "Category: ");
  while (text.indexOf("  ") != -1) text.replace("  ", " ");

  // Word wrap
  String line = "";
  int i = 0;

  while (i < text.length()) {
    while (i < text.length() && text[i] == ' ') i++;
    if (i >= text.length()) break;

    int start = i;
    while (i < text.length() && text[i] != ' ') i++;
    String word = text.substring(start, i);

    String test = (line.length() == 0) ? word : (line + " " + word);
    display.getTextBounds(test, x, y, &x1, &y1, &w, &h);

    if (w <= maxW) {
      line = test;
    } else {
      if (line.length() > 0) {
        display.setCursor(x, y);
        display.println(line);
        y += lineH;
        if (y > 56) break;
      }
      line = word;
    }
  }

  if (y <= 56 && line.length() > 0) {
    display.setCursor(x, y);
    display.println(line);
  }

  display.display();
}

void drawLearnMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LEARN RECYCLING");
  display.println("----------------");

  for (int i = 0; i < 4; i++) {
    display.print((i == learnCursor) ? "> " : "  ");
    display.println(categories[i]);
  }
  display.println("");
  display.println("RIGHT/OK: select");
  display.println("LEFT/OK: back");
  display.display();
}

void drawLearnCategory(int cat) {
  display.clearDisplay();
  display.setCursor(0, 0);

  display.println(categories[cat]);
  display.println("-------------");

  String *info;
  if (cat == 0) info = plasticInfo;
  else if (cat == 1) info = paperInfo;
  else if (cat == 2) info = metalInfo;
  else info = trashInfo;

  for (int i = 0; i < 3; i++) display.println(info[i]);

  display.println("");
  display.println("LEFT/OK: back");
  display.display();
}

void drawStatusScreen() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SYSTEM STATUS");
  display.println("-------------");

  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: OK ");
    display.println(ssid);
    display.print("IP: ");
    display.println(WiFi.localIP());
  } else {
    display.println("WiFi: NOT OK");
  }

  display.println("Ultrasonic: ON");
  display.println("Joystick:   ON");
  display.println("");
  display.println("LEFT/OK: back");
  display.display();
}

void drawSettingsScreen() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SETTINGS");
  display.println("--------");
  display.println("Brightness: 100%");
  display.println("Sound: OFF");
  display.println("Theme: Default");
  display.println("");
  display.println("LEFT/OK: back");
  display.display();
}

// ---------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // UART to motor ESP32
  Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(SW_PIN, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);

  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Smart Bin UI Ready");
  display.display();
  delay(1000);
}

// ---------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------
void loop() {
  int joyCmd = readJoystickCommand();

  switch (currentMenu) {

    case MENU_MAIN:
      drawMainMenu();

      if (joyCmd & COMMAND_UP)   { if (mainCursor > 0) mainCursor--; delay(200); }
      if (joyCmd & COMMAND_DOWN) { if (mainCursor < 4) mainCursor++; delay(200); }

      if ((joyCmd & COMMAND_RIGHT) || (joyCmd & COMMAND_PRESS)) {
        if (mainCursor == 0) { objectDetected = false; currentMenu = MENU_SCAN; }
        else if (mainCursor == 1) currentMenu = MENU_RESULT;
        else if (mainCursor == 2) currentMenu = MENU_LEARN;
        else if (mainCursor == 3) currentMenu = MENU_STATUS;
        else if (mainCursor == 4) currentMenu = MENU_SETTINGS;
        delay(200);
      }
      break;

    case MENU_SCAN: {
      long distance = getDistanceCM();
      drawScanScreen(distance);

      if ((joyCmd & COMMAND_LEFT) || (joyCmd & COMMAND_PRESS)) {
        currentMenu = MENU_MAIN;
        objectDetected = false;
        delay(200);
        break;
      }

      if (distance > 0 && distance < 15 && !objectDetected) {
        objectDetected = true;
        performScan();
        currentMenu = MENU_RESULT;
        delay(200);
      } else if (distance >= 15) {
        objectDetected = false;
      }
      break;
    }

    case MENU_RESULT:
      drawResultScreen();
      if ((joyCmd & COMMAND_LEFT) || (joyCmd & COMMAND_PRESS)) { currentMenu = MENU_MAIN; delay(200); }
      break;

    case MENU_LEARN:
      drawLearnMenu();

      if (joyCmd & COMMAND_UP)   { if (learnCursor > 0) learnCursor--; delay(200); }
      if (joyCmd & COMMAND_DOWN) { if (learnCursor < 3) learnCursor++; delay(200); }

      if ((joyCmd & COMMAND_RIGHT) || (joyCmd & COMMAND_PRESS)) { currentMenu = MENU_LEARN_CATEGORY; delay(200); }
      if (joyCmd & COMMAND_LEFT) { currentMenu = MENU_MAIN; delay(200); }
      break;

    case MENU_LEARN_CATEGORY:
      drawLearnCategory(learnCursor);
      if ((joyCmd & COMMAND_LEFT) || (joyCmd & COMMAND_PRESS)) { currentMenu = MENU_LEARN; delay(200); }
      break;

    case MENU_STATUS:
      drawStatusScreen();
      if ((joyCmd & COMMAND_LEFT) || (joyCmd & COMMAND_PRESS)) { currentMenu = MENU_MAIN; delay(200); }
      break;

    case MENU_SETTINGS:
      drawSettingsScreen();
      if ((joyCmd & COMMAND_LEFT) || (joyCmd & COMMAND_PRESS)) { currentMenu = MENU_MAIN; delay(200); }
      break;
  }

  delay(40);
}
