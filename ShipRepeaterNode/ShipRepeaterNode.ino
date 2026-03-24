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
  // The sampling window depends on how the chip was woken/reset:
  //   • Cold boot / hardware-reset: two entry paths (see below).
  //   • Scheduled (timer / BLE / WiFi) wakeup → 50 ms window so normal
  //     operational duties resume as quickly as possible.
  //   • The RTC flag (set by a 2-second BOOT hold during operational mode)
  //     forces config mode without requiring the button at all.
  //
  // Cold-boot paths:
  //   Path 1 — hold from boot start (BOOT_HOLD_COLD_MS = 5 s):
  //     On some boards pressing the BOOT button triggers a hardware reset.
  //     The user holds BOOT, the reset fires, and the button remains LOW
  //     when setup() begins.  We count from t=0; if the button is held
  //     continuously for BOOT_HOLD_COLD_MS (5 s) → config mode.
  //     Releasing before 5 s does NOT enter config mode (avoids accidental
  //     entry from brief button presses that happened to cause a reset).
  //   Path 2 — any press within BOOT_WINDOW_COLD_MS (2 s) from t=0:
  //     Preserves the original behaviour: a brief press at any point in
  //     the first 2 s after a cold boot triggers config mode immediately.
  //     This fires for whatever time remains after path 1 finishes.
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  bool forceConfigMode = false;

  if (rtc_force_config_mode) {
    // Consumed immediately so a subsequent crash-restart doesn't loop in
    // config mode unexpectedly.
    rtc_force_config_mode = false;
    forceConfigMode = true;
  } else {
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    unsigned long t0 = millis();

    if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED) {
      // ── Cold boot / hardware reset ────────────────────────────────────
      // Path 1: button already held at boot start → require 5 s continuous
      // hold from t=0 to confirm intent and avoid accidental config entry.
      if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        bool heldContinuously = true;
        while (millis() - t0 < BOOT_HOLD_COLD_MS) {
          if (digitalRead(BOOT_BUTTON_PIN) == HIGH) { heldContinuously = false; break; }
          delay(10);
        }
        if (heldContinuously) forceConfigMode = true;
      }
      // Path 2: any press within BOOT_WINDOW_COLD_MS from t0 (runs for
      // whatever time remains; exits immediately if already elapsed).
      if (!forceConfigMode) {
        while (millis() - t0 < BOOT_WINDOW_COLD_MS) {
          if (digitalRead(BOOT_BUTTON_PIN) == LOW) { forceConfigMode = true; break; }
          delay(10);
        }
      }
    } else {
      // Scheduled (timer / BLE / WiFi) wakeup → 50 ms window.
      // NOTE: ESP_SLEEP_WAKEUP_GPIO cannot be triggered by GPIO9 on ESP32-C6
      // because GPIO9 is not an LP GPIO.  All non-undefined wakeup causes
      // therefore map to the short 50 ms window.
      while (millis() - t0 < BOOT_WINDOW_SCHED_MS) {
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) { forceConfigMode = true; break; }
        delay(10);
      }
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
    // LED feedback: STATUS_ERROR (red rapid blink) appears at 500 ms to
    // signal that config mode will trigger soon — keep holding.
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