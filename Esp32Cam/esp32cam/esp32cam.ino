// Explicit pin definitions for AI-Thinker ESP32-CAM.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define LED_GPIO_NUM 4       // Flash LED on GPIO 4 (typically active HIGH)
#define RED_LED_GPIO_NUM 33  // Red LED on GPIO 33 (typically active LOW)

#include "esp_camera.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WebServer.h>
#include "img_converters.h"
#include <HTTPClient.h>
#include "esp_timer.h"
#include "driver/rtc_io.h"

const char* ssid = "RASPNET";
const char* password = "123456789";

const char* PING_SERVER_URL = "http://10.42.0.1/cgi-bin/ping.cgi";

WebServer server(80);

// Forward declarations
void handleStillCapture();
void sendPing();
void pingTask(void* pvParameters);
void blinkLed(int pin, int count, int delay_ms, int active_state);

// Handler for serving a single JPEG image
void handleStillCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed for still image");
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }
  server.setContentLength(fb->len);
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendContent((const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// Function to send the PING POST request
void sendPing() {
  // Blink RED LED when sending ping (active LOW)
  digitalWrite(RED_LED_GPIO_NUM, LOW);   // Turn RED LED on
  delay(50);                             // Brief on-time
  digitalWrite(RED_LED_GPIO_NUM, HIGH);  // Turn RED_LED off

  HTTPClient http;
  http.begin(PING_SERVER_URL);
  http.addHeader("Content-Type", "text/plain");

  // Get MAC address as string (e.g., "A0:B1:C2:D3:E4:F5")
  String macAddressStr = WiFi.macAddress();
  
  // Convert MAC address string to a clean hexadecimal string (e.g., "A0B1C2D3E4F5")
  String cleanMacAddress = "";
  for (int i = 0; i < macAddressStr.length(); i++) {
    if (macAddressStr.charAt(i) != ':') {
      cleanMacAddress += macAddressStr.charAt(i);
    }
  }
  
  // Convert the hexadecimal MAC string to a uint64_t numerical value
  uint64_t macAsUint64 = strtoull(cleanMacAddress.c_str(), NULL, 16);
  
  // Convert the uint64_t numerical value to a decimal string for the POST body
  String postBody = String(macAsUint64);

  // Add MAC address as a custom header for additional context/debugging
  http.addHeader("X-ESP32-MAC", macAddressStr);

  // Send the numerical MAC address as the body content
  int httpResponseCode = http.POST(postBody);
  String payload = "N/A";
  if (httpResponseCode > 0) {
    payload = http.getString();
    Serial.printf("Ping POST successful. URL: %s, Code: %d, Response: %s\n", PING_SERVER_URL, httpResponseCode, payload.c_str());
  } else {
    Serial.printf("Ping POST failed. URL: %s, Code: %d, Error: %s\n", PING_SERVER_URL, httpResponseCode, http.errorToString(httpResponseCode).c_str());
    blinkLed(RED_LED_GPIO_NUM, 3, 100, LOW);  // Blink RED LED on ping error (active LOW)
  }
  http.end();
}

// FreeRTOS task for periodic PING requests
void pingTask(void* pvParameters) {
  for (;;) {
    sendPing();
    vTaskDelay(pdMS_TO_TICKS(10000));  // Delay for 10 seconds
  }
}

// Helper function to blink an LED
// active_state: LOW if LED is active LOW (turns on with LOW), HIGH if LED is active HIGH (turns on with HIGH)
void blinkLed(int pin, int count, int delay_ms, int active_state) {
  pinMode(pin, OUTPUT);
  int inactive_state = (active_state == LOW) ? HIGH : LOW; // Opposite of active state
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, active_state);   // Turn LED on
    delay(delay_ms);
    digitalWrite(pin, inactive_state);  // Turn LED off
    delay(delay_ms);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Starting minimal ESP32-CAM setup...");

  // Initialize LED pins
  pinMode(LED_GPIO_NUM, OUTPUT);     // Flash LED
  digitalWrite(LED_GPIO_NUM, LOW);   // Ensure Flash LED is off initially (active HIGH, so LOW is off)

  pinMode(RED_LED_GPIO_NUM, OUTPUT);     // Red LED
  digitalWrite(RED_LED_GPIO_NUM, HIGH);  // Ensure Red LED is off initially (active LOW, so HIGH is off)

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x! Halting...\n", err);
    blinkLed(LED_GPIO_NUM, 5, 200, HIGH);  // Blink FLASH LED on camera init error (active HIGH)
    delay(5000);
    return;
  }
  Serial.println("Camera initialized successfully.");

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("Connecting to WiFi");
  unsigned long wifi_start_time = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - wifi_start_time > 30000) {
      Serial.println("\nWiFi connection timed out!");
      blinkLed(LED_GPIO_NUM, 10, 100, HIGH);  // Blink FLASH LED on WiFi timeout error (active HIGH)
      return;
    }
  }
  Serial.printf("\nWiFi connected\nCamera IP: %s\n", WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, handleStillCapture);
  server.begin();
  Serial.println("HTTP server started.");

  Serial.println("Creating ping task...");
  xTaskCreatePinnedToCore(
    pingTask,
    "pingTask",
    4096,
    NULL,
    1,
    NULL,
    0);
  Serial.println("Ping task created.");

  digitalWrite(LED_GPIO_NUM, HIGH);  // Turn Flash LED on after successful boot (active HIGH)
  Serial.println("Flash LED turned on.");

  Serial.println("Minimal ESP32-CAM setup complete.");
}

void loop() {
  server.handleClient();
  delay(10);
}
