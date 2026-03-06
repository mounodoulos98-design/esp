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

// RTC flag: set by a 2-second BOOT hold in loop() to request Config Mode on
// next restart WITHOUT performing a factory reset (settings are preserved).
// Persists across software resets and deep sleep; cleared in setup() once used.
RTC_DATA_ATTR bool rtc_force_config_mode = false;


void setup() {
  // ── BOOT button → Config Mode detection ────────────────────────────────
  // Sample BOOT button (GPIO9, active LOW) as the very first action.
  // The sampling window depends on how the chip was woken/reset so that:
  //   • A cold boot / hardware-reset gives a comfortable 2-second window —
  //     the user has time to press BOOT after powering the board.
  //   • A GPIO wakeup (BOOT pressed while COLLECTOR was in deep sleep) is
  //     treated as an immediate config-mode request.
  //   • A scheduled timer / BLE / WiFi wakeup uses only a 50 ms window so
  //     normal operational duties resume as quickly as possible.
  //   • The RTC flag (set by a 2-second BOOT hold during operational mode)
  //     forces config mode without requiring the button at all.
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  bool forceConfigMode = false;

  if (rtc_force_config_mode) {
    // Consumed immediately so a subsequent crash-restart doesn't loop in
    // config mode unexpectedly.
    rtc_force_config_mode = false;
    forceConfigMode = true;
  } else {
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    // Cold boot / hardware reset → 2 000 ms window (plenty of time).
    // Scheduled (timer / BLE / WiFi) wakeup → 50 ms (fast operational resume).
    // NOTE: ESP_SLEEP_WAKEUP_GPIO cannot be triggered by GPIO9 on ESP32-C6
    // because GPIO9 is not an LP GPIO.  All non-undefined wakeup causes
    // therefore map to the short 50 ms window.
    unsigned long windowMs = (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED)
                               ? BOOT_WINDOW_COLD_MS : BOOT_WINDOW_SCHED_MS;
    unsigned long t0 = millis();
    while (millis() - t0 < windowMs) {
      if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        forceConfigMode = true;
        break;
      }
      delay(10);
    }
  }

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

    // ── BOOT button hold behaviour ──────────────────────────────────────
    // 2 s hold → restart into Config Mode (all saved settings preserved).
    // 5 s hold → factory reset all settings, then restart into Config Mode.
    // The LED feedback pattern naturally communicates the threshold:
    //   the STATUS_SLEEPING blink changes to STATUS_ERROR (red rapid) at 2 s.
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      if (bootButtonPressTime == 0) bootButtonPressTime = millis();
      unsigned long held = millis() - bootButtonPressTime;
      if (held > BOOT_HOLD_RESET_MS) {
        Serial.println("[BOOT] 5-second hold: factory reset triggered!");
        factoryReset();
        ESP.restart();
      } else if (held > BOOT_HOLD_CONFIG_MS) {
        Serial.println("[BOOT] 2-second hold: entering Config Mode (settings preserved).");
        rtc_force_config_mode = true;
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