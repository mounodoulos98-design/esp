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


bool initSdCard() {
  // Guard against NULL mutex handle (heap exhaustion at boot).
  if (sdCardMutex == nullptr) {
    sdCardMutex = xSemaphoreCreateMutex();
    if (sdCardMutex == nullptr) return false;
  }
  if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(500)) == pdFALSE)
    return false;

  static bool sdInitialized = false;
  // Cooldown: after a failed retry sequence, wait before trying again.
  // Prevents main-loop starvation when SD is absent/broken.
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
  // latched after wake, which keeps the SD card in an undefined state).
  gpio_hold_dis((gpio_num_t)SD_CS_PIN);

  // Use the same init pattern that worked on the original codebase:
  // DEDICATED_SPI + SPI.begin(SCK, MISO, MOSI, SD_CS_PIN).
  // Try progressively lower SPI speeds: 10 → 8 → 4 MHz.
  const int speeds[] = { 10, 8, 4 };
  bool success = false;

  for (int s = 0; s < 3 && !success; s++) {
    SPI.end();
    delay(50);
    SPI.begin(SCK, MISO, MOSI, SD_CS_PIN);
    delay(20);

    esp_task_wdt_reset();

    SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(speeds[s]));
    success = sd.begin(cfg);
    if (!success) {
      Serial.printf("[SD] Mount failed at %d MHz – %s\n", speeds[s],
                    (s < 2) ? "retrying..." : "(final).");
      delay(100);
    }
  }

  if (success) {
    Serial.println("[SD] Card initialized successfully.");
    sdInitialized = true;
    lastFailMillis = 0;
  } else {
    Serial.println("[SD] Card Mount Failed (final).");
    sdInitialized = false;
    lastFailMillis = millis();
  }

  xSemaphoreGive(sdCardMutex);
  return success;
}

