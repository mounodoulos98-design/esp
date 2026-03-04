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
  // Sample BOOT button as the very first thing – before any blocking code.
  // GPIO9 is the physical BOOT button on the Waveshare ESP32-C6.
  // Holding it LOW during reset / deep-sleep wake forces Configuration Mode.
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  delay(20);  // Debounce
  bool forceConfigMode = (digitalRead(BOOT_BUTTON_PIN) == LOW);

  Serial.begin(115200);
  Serial.println("\n\n===================================");
  Serial.println("ShipRepeaterNode Booting...");
  Serial.println("[SERIAL] Baud rate: 115200 -- make sure your monitor is set to 115200!");

  setupStatusLed();
  sdCardMutex = xSemaphoreCreateMutex();
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