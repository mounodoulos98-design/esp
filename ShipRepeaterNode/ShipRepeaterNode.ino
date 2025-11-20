#include "config.h"
#include "tuning.h"

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

// Global tuning parameters
RuntimeTuning g_tuning;

// Global state machine control
bool isOperationalMode = false;
unsigned long bootButtonPressTime = 0;


void setup() {
  Serial.begin(115200);
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
  
  // Load runtime tuning parameters
  g_tuning = loadRuntimeTuning();
  Serial.println("[TUNING] Runtime parameters loaded");

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