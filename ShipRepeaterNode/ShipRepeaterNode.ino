#include "config.h"

// =================================================================
// == GLOBAL OBJECT DEFINITIONS
// =================================================================
painlessMesh mesh;
AsyncWebServer server(80);
DNSServer dnsServer;
Preferences preferences;
Scheduler userScheduler;
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SdFat sd;
NodeConfig config;
SemaphoreHandle_t sdCardMutex;

// Global state machine control
bool isOperationalMode = false;
unsigned long bootButtonPressTime = 0;


void setup() {
  Serial.begin(115200);
  // The ESP32-C6 uses native USB-CDC as its serial port.  After a deep-sleep
  // wake the USB device disconnects and re-enumerates.  The host OS needs up
  // to ~1.5 s to reopen the virtual COM port.  Without this wait all boot
  // messages are lost and only USB reconnection noise is visible in the
  // terminal.  The loop exits immediately once the port is open; the 1500 ms
  // cap ensures normal operation when no terminal is connected.
  {
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);
  }
  Serial.println("\n\n===================================");
  Serial.println("ShipRepeaterNode Booting...");

  setupStatusLed();
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  sdCardMutex = xSemaphoreCreateMutex();

  delay(50);  // Debounce for boot button
  bool forceConfigMode = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  WiFi.mode(WIFI_OFF);
  delay(200);

  loadConfiguration();

  if (forceConfigMode || !config.isConfigured) {
    if (forceConfigMode) {
      Serial.println("[MODE] Forced Configuration Mode by user.");
    } else {
      Serial.println("[MODE] No configuration found. Entering Configuration Mode.");
    }
    isOperationalMode = false;
    startConfigurationMode();
  } else {
    Serial.println("[MODE] Configuration found. Entering Operational Mode.");
    isOperationalMode = true;
    startOperationalMode();
  }
}

void loop() {
  if (isOperationalMode) {
    loopOperationalMode();

    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      if (bootButtonPressTime == 0) {
        bootButtonPressTime = millis();
      } else if (millis() - bootButtonPressTime > 3000) {
        Serial.println("[RESET] Factory reset triggered!");
        factoryReset();
        ESP.restart();
      }
    } else {
      bootButtonPressTime = 0;
    }
  } else {
    loopConfigurationMode();
  }

  loopStatusLed();
}