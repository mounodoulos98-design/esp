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
  // Three entry paths, checked in priority order:
  //
  //   Path 1 (ALL wakeup causes) — hold from boot start (BOOT_HOLD_COLD_MS = 5 s):
  //     On this board pressing the BOOT button causes a hardware reset.
  //     The user holds BOOT, the reset fires, and the button remains LOW
  //     when setup() begins.  We count from t=0; if the button is held
  //     continuously for BOOT_HOLD_COLD_MS (5 s) → config mode.
  //     Releasing before 5 s does NOT enter config mode (avoids accidental
  //     entry from a brief button press that happened to cause a reset).
  //     Also fires when waking from deep sleep (timer/BLE/WiFi) while the
  //     button is held — user holds BOOT, device wakes from sleep, and
  //     sees the button still held → same 5 s count → config mode.
  //     Zero latency when button is NOT held at t=0.
  //
  //   Path 2 (cold boot only) — any press within BOOT_WINDOW_COLD_MS (2 s):
  //     A brief press at any point in the first 2 s after a cold boot /
  //     hardware reset triggers config mode immediately.
  //     Runs for whatever time remains after Path 1 finishes.
  //
  //   Scheduled (timer / BLE / WiFi) wakeup → 50 ms window so normal
  //     operational duties resume as quickly as possible.
  //
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
    unsigned long t0 = millis();

    // ── Path 1 (ALL wakeup causes): button held at boot start ────────────
    // Pressing BOOT on this board causes a hardware reset.  The user holds
    // BOOT, the reset fires, and the button remains LOW when setup() begins.
    // We count from t=0; if the button is held continuously for
    // BOOT_HOLD_COLD_MS (5 s) → config mode.
    // Releasing before 5 s cancels (avoids accidental config entry from a
    // brief press that happened to cause a reset).
    // This path is checked for EVERY wakeup cause so it also fires when the
    // device wakes from deep sleep (timer/BLE/WiFi) and the user is still
    // holding the button.  When the button is NOT held at t=0 this block
    // exits immediately, adding zero latency to scheduled wakeups.
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      bool heldContinuously = true;
      while (millis() - t0 < BOOT_HOLD_COLD_MS) {
        if (digitalRead(BOOT_BUTTON_PIN) == HIGH) { heldContinuously = false; break; }
        delay(10);
      }
      if (heldContinuously) forceConfigMode = true;
    }

    if (!forceConfigMode) {
      if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // ── Cold boot / hardware reset ──────────────────────────────────
        // Path 2: any brief press within BOOT_WINDOW_COLD_MS from t=0.
        // Runs for whatever time remains after Path 1; exits immediately
        // if the window has already elapsed.
        while (millis() - t0 < BOOT_WINDOW_COLD_MS) {
          if (digitalRead(BOOT_BUTTON_PIN) == LOW) { forceConfigMode = true; break; }
          delay(10);
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