#include "config.h"
#include "driver/gpio.h"

RTC_DATA_ATTR static time_t rtc_persisted_epoch = 0;
RTC_DATA_ATTR static uint32_t rtc_persisted_sleep_s = 0;

const char* PREF_NAMESPACE = "node_config";
const char* RTC_NAMESPACE = "rtc_store";

void saveConfiguration() {
  preferences.begin(PREF_NAMESPACE, false);
    preferences.putString("apSSID", config.apSSID);
  preferences.putString("apPASS", config.apPASS);
  preferences.putString("apIP", config.apIP);
preferences.putString("nodeName", config.nodeName);
  preferences.putInt("role", config.role);
  preferences.putInt("meshInt", config.meshIntervalMin);
  preferences.putInt("meshWin", config.meshWindowSec);
  // uplink
  preferences.putString("uplinkSSID", config.uplinkSSID);
  preferences.putString("uplinkPASS", config.uplinkPASS);
  preferences.putString("uplinkHost", config.uplinkHost);
  preferences.putInt("uplinkPort", config.uplinkPort);
  preferences.putInt("uplinkRoute", config.uplinkRoute);

  if (config.role == ROLE_COLLECTOR) {
    preferences.putString("sensorAP", config.sensorAP_SSID);
    preferences.putInt("collCyc", config.collectorApCycleSec);
    preferences.putInt("collWin", config.collectorApWindowSec);
    preferences.putInt("collTout", config.collectorDataTimeoutSec);
  }
  
  // Save BLE mesh wake-up configuration
  preferences.putBool("bleBeacon", config.bleBeaconEnabled);
  preferences.putInt("bleScanSec", config.bleScanDurationSec);
  
  preferences.putBool("configured", config.isConfigured);
  preferences.end();
  Serial.println("[STORAGE] Configuration saved to flash.");
}

void loadConfiguration() {
  if (preferences.begin(PREF_NAMESPACE, true)) {
    config.isConfigured = preferences.getBool("configured", false);
    if (config.isConfigured) {
      config.apSSID = preferences.getString("apSSID", "");
      config.apPASS = preferences.getString("apPASS", "");
      config.apIP = preferences.getString("apIP", "");
      config.nodeName = preferences.getString("nodeName", "DefaultNode");
      config.role = preferences.getInt("role", ROLE_REPEATER);
      config.meshIntervalMin = preferences.getInt("meshInt", MESH_APPOINTMENT_INTERVAL_M);
      config.meshWindowSec = preferences.getInt("meshWin", MESH_APPOINTMENT_WINDOW_S);
      // uplink
      config.uplinkSSID = preferences.getString("uplinkSSID", ROOT_AP_SSID);
      config.uplinkPASS = preferences.getString("uplinkPASS", ROOT_AP_PASSWORD);
      config.uplinkHost = preferences.getString("uplinkHost", UPLINK_HOST_DEFAULT);
      config.uplinkPort = preferences.getInt("uplinkPort", UPLINK_PORT_DEFAULT);
      config.uplinkRoute = preferences.getInt("uplinkRoute", UPLINK_DIRECT);

      if (config.role == ROLE_COLLECTOR) {
        config.sensorAP_SSID = preferences.getString("sensorAP", "DefaultSensorAP");
        config.collectorApCycleSec = preferences.getInt("collCyc", COLLECTOR_AP_CYCLE_S);
        config.collectorApWindowSec = preferences.getInt("collWin", COLLECTOR_AP_WINDOW_S);
        config.collectorDataTimeoutSec = preferences.getInt("collTout", COLLECTOR_DATA_TIMEOUT_S);
      }
      
      // Load BLE mesh wake-up configuration
      config.bleBeaconEnabled = preferences.getBool("bleBeacon", true);
      config.bleScanDurationSec = preferences.getInt("bleScanSec", 5);
      
      Serial.println("[STORAGE] Configuration loaded from flash.");
    } else {
      Serial.println("[STORAGE] No configuration found.");
    }
    preferences.end();
  }
}

void factoryReset() {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.clear();
  preferences.end();
  
  preferences.begin(RTC_NAMESPACE, false);
  preferences.clear();
  preferences.end();

  Serial.println("[STORAGE] All settings cleared (Factory Reset).");
}

// **ΝΕΕΣ ΣΥΝΑΡΤΗΣΕΙΣ ΓΙΑ ΜΟΝΙΜΗ ΑΠΟΘΗΚΕΥΣΗ ΩΡΑΣ**
void persistRtcTime(time_t epoch) {
  preferences.begin(RTC_NAMESPACE, false);
  preferences.putULong64("epoch", epoch);
  preferences.end();
  Serial.printf("[STORAGE] Saved epoch %llu to NVS.\n", (uint64_t)epoch);
}

time_t restoreRtcTime() {
  time_t epoch = 0;
  if (preferences.begin(RTC_NAMESPACE, true)) {
    epoch = preferences.getULong64("epoch", 0);
    preferences.end();
  }
  if (epoch > 1700000000) {
    Serial.printf("[STORAGE] Restored epoch %llu from NVS.\n", (uint64_t)epoch);
    return epoch;
  }
  return 0;
}


// Send 80 dummy SPI clock cycles with CS HIGH to force SD card into SPI mode
// per SD Physical Layer Spec §6.4.1.  Must be done after each power cycle or
// deep-sleep wake before the first sd.begin().
static void sdSendDummyClocks() {
  digitalWrite(SD_CS_PIN, HIGH);
  for (int i = 0; i < 10; i++) {
    SPI.transfer(0xFF);
  }
}

bool initSdCard() {
  // Bug 2 fix: guard against NULL handle — xSemaphoreCreateMutex() in setup()
  // could have returned NULL if the FreeRTOS heap was exhausted at that point.
  if (sdCardMutex == nullptr) {
    sdCardMutex = xSemaphoreCreateMutex();
    if (sdCardMutex == nullptr) return false;
  }
  if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(500)) == pdFALSE)
    return false;

  static bool sdInitialized = false;
  // Cooldown: after a failed full retry sequence, wait before trying again.
  // Prevents main-loop starvation and WDT timeout when SD is absent/broken.
  static unsigned long lastFailMillis = 0;
  static constexpr unsigned long SD_RETRY_COOLDOWN_MS = 10000; // 10 seconds

  // Αν είναι ήδη initialized, μην το ξαναδοκιμάζεις
  if (sdInitialized) {
    xSemaphoreGive(sdCardMutex);
    return true;
  }

  // Skip retry if still within cooldown after last failure
  if (lastFailMillis != 0 && (millis() - lastFailMillis) < SD_RETRY_COOLDOWN_MS) {
    xSemaphoreGive(sdCardMutex);
    return false;
  }

  Serial.println("[SD] (Re)Initializing SD card...");

  // Release any GPIO hold that was set before deep sleep (prevents CS staying
  // latched LOW after wake, which keeps the SD card in an undefined state).
  gpio_hold_dis((gpio_num_t)SD_CS_PIN);

  // Drive CS HIGH before SPI init — SD spec requires CS=HIGH during
  // the initial 80 clock cycles that switch the card to SPI mode.
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  // Try progressively lower SPI speeds: 10 → 8 → 4 → 2 MHz.
  // Between each attempt: end SdFat + SPI, re-init SPI with explicit pins,
  // send 80 dummy clocks, then re-attempt sd.begin().
  //
  // IMPORTANT: Use SHARED_SPI (not DEDICATED_SPI).  DEDICATED_SPI makes SdFat
  // internally call SPI.end() + SPI.begin() WITHOUT pin arguments.  On Arduino
  // ESP32 core v3.x SPI.end() clears the stored pin config, so the subsequent
  // SPI.begin() reverts to DEFAULT pins — which don't match the SD card wiring.
  // SHARED_SPI avoids this: we own the SPI bus and SdFat just uses it as-is.
  //
  // IMPORTANT: Pass -1 for SS in SPI.begin() — do NOT pass SD_CS_PIN.
  // The SPI peripheral's hardware SS would fight SdFat's software CS toggling,
  // keeping CS in the wrong state during transactions and preventing card init.
  const int speeds[] = { 10, 8, 4, 2 };
  bool success = false;

  for (int s = 0; s < 4 && !success; s++) {
    sd.end();
    SPI.end();
    delay(100);

    // Reset WDT so the retry loop (4 speeds × ~500ms each) doesn't trigger
    // a watchdog panic during normal SD recovery after deep sleep.
    esp_task_wdt_reset();

    // SPI.end() detaches pins, so re-assert CS as GPIO output before SPI.begin().
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    // Re-initialize SPI with our board-specific pins every iteration.
    // Pass -1 for SS: we manage CS ourselves via SdFat's SdSpiConfig.
    // Passing the actual CS pin here makes the SPI hardware drive it,
    // which conflicts with SdFat's software CS toggling.
    SPI.begin(SCK, MISO, MOSI, -1);
    delay(20);

    // 80 dummy clocks with CS HIGH → forces SD into SPI mode
    sdSendDummyClocks();
    delay(10);

    SdSpiConfig cfg(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(speeds[s]));
    success = sd.begin(cfg);
    if (!success) {
      Serial.printf("[SD] Mount failed at %d MHz – %s\n", speeds[s],
                    (s < 3) ? "retrying..." : "(final).");
      delay(300);
    }
  }

  if (success) {
    Serial.println("[SD] Card initialized successfully.");
    sdInitialized = true;
    lastFailMillis = 0; // clear cooldown on success
  } else {
    Serial.println("[SD] Card Mount Failed (final).");
    sdInitialized = false;
    lastFailMillis = millis(); // start cooldown
  }

  xSemaphoreGive(sdCardMutex);
  return success;
}

