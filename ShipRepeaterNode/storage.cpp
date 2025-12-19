#include "config.h"

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
    // Adaptive sensor tracking
    preferences.putInt("expSensors", config.expectedSensorCount);
    preferences.putBool("adaptiveAP", config.adaptiveApWindow);
    preferences.putInt("adaptMinSec", config.adaptiveWindowMinSec);
    preferences.putInt("adaptMaxSec", config.adaptiveWindowMaxSec);
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
        // Adaptive sensor tracking
        config.expectedSensorCount = preferences.getInt("expSensors", 0);
        config.adaptiveApWindow = preferences.getBool("adaptiveAP", true);
        config.adaptiveWindowMinSec = preferences.getInt("adaptMinSec", 60);
        config.adaptiveWindowMaxSec = preferences.getInt("adaptMaxSec", 1800);
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
  if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(500)) == pdFALSE)
    return false;

  static bool sdInitialized = false;
  SdSpiConfig spiCfg(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(10));

  // Αν είναι ήδη initialized, μην το ξαναδοκιμάζεις
  if (sdInitialized) {
    xSemaphoreGive(sdCardMutex);
    return true;
  }

  Serial.println("[SD] (Re)Initializing SD card...");
  SPI.end();
  delay(50);
  SPI.begin(SCK, MISO, MOSI, SD_CS_PIN);
  delay(20);

  bool success = sd.begin(spiCfg);
  if (!success) {
    Serial.println("[SD] Card Mount Failed – retrying...");
    delay(100);
    SPI.end();
    delay(20);
    SPI.begin(SCK, MISO, MOSI, SD_CS_PIN);
    SdSpiConfig retryCfg(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(8));
    success = sd.begin(retryCfg);
  }

  if (success) {
    Serial.println("[SD] Card initialized successfully.");
    sdInitialized = true;
  } else {
    Serial.println("[SD] Card Mount Failed (final).");
    sdInitialized = false;
  }

  xSemaphoreGive(sdCardMutex);
  return success;
}

