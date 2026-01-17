#include "WiFi.h"
#include "esp_camera.h"
#include "WebServer.h"

// ================= WIFI =================
const char* ssid     = "Haazzzz";
const char* password = "haa12345";

// ================= FLASH LED =================
#define FLASH_LED_PIN 4   // Onboard white flash LED

// ================= WEB SERVER =================
WebServer server(80);

// ================= AI THINKER CAMERA PINS =================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ================= LED BLINK STATE =================
unsigned long lastBlink = 0;
bool ledState = false;

void setup() {
  Serial.begin(115200);
  Serial.println();

  // -------- Flash LED setup --------
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW); // start OFF (some boards invert)

  // -------- WiFi connect (blink LED) --------
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastBlink > 200) {
      lastBlink = millis();
      ledState = !ledState;
      digitalWrite(FLASH_LED_PIN, ledState);
    }
    delay(10);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // -------- Solid LED = powered & ready --------
  digitalWrite(FLASH_LED_PIN, HIGH);

  // ================= CAMERA CONFIG =================
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 10;            // high quality (big file)
  config.fb_count     = 1;

  // -------- Init camera --------
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("❌ Camera init failed!");
    while (true) {
      digitalWrite(FLASH_LED_PIN, !digitalRead(FLASH_LED_PIN));
      delay(150); // fast blink = error
    }
  }

  Serial.println("✅ Camera initialized");

  // ================= /capture ENDPOINT =================
  server.on("/capture", HTTP_GET, []() {

    // Flush old frames
    for (int i = 0; i < 2; i++) {
      camera_fb_t *old = esp_camera_fb_get();
      if (old) esp_camera_fb_return(old);
    }

    // Capture new frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      server.send(500, "text/plain", "Camera capture failed");
      return;
    }

    // Send JPEG
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });

  server.begin();
  Serial.println("📷 Camera server started");
}

void loop() {
  server.handleClient();
}
