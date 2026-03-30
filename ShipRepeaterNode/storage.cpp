#include "config.h"
#include <LittleFS.h>
#include <driver/gpio.h>

RTC_DATA_ATTR static time_t rtc_persisted_epoch = 0;
RTC_DATA_ATTR static uint32_t rtc_persisted_sleep_s = 0;

const char* PREF_NAMESPACE = "node_config";
const char* RTC_NAMESPACE  = "rtc_store";

// LittleFS backup file — written every time config is saved to NVS.
// Acts as a resilient fallback if the NVS partition is auto-erased by the
// Arduino framework after a power cut (ESP_ERR_NVS_NO_FREE_PAGES handling).
#define CONFIG_BACKUP_PATH "/cfgbak.json"

// ── LittleFS helpers ─────────────────────────────────────────────────────────

static bool saveConfigToFile() {
  if (!LittleFS.begin(true)) {
    Serial.println("[STORAGE] WARNING: LittleFS mount failed; config backup unavailable (NVS-only mode).");
    return false;
  }
  File f = LittleFS.open(CONFIG_BACKUP_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("[STORAGE] Cannot open config backup for writing.");
    LittleFS.end();
    return false;
  }
  // Serialise all config fields as JSON.
  JsonDocument doc;
  doc["configured"]  = config.isConfigured;
  doc["apSSID"]      = config.apSSID;
  doc["apPASS"]      = config.apPASS;
  doc["apIP"]        = config.apIP;
  doc["nodeName"]    = config.nodeName;
  doc["role"]        = config.role;
  doc["meshInt"]     = config.meshIntervalMin;
  doc["meshWin"]     = config.meshWindowSec;
  doc["uplinkSSID"]  = config.uplinkSSID;
  doc["uplinkPASS"]  = config.uplinkPASS;
  doc["uplinkHost"]  = config.uplinkHost;
  doc["uplinkPort"]  = config.uplinkPort;
  doc["uplinkRoute"] = config.uplinkRoute;
  doc["sensorAP"]    = config.sensorAP_SSID;
  doc["collCyc"]     = config.collectorApCycleSec;
  doc["collWin"]     = config.collectorApWindowSec;
  doc["collTout"]    = config.collectorDataTimeoutSec;
  doc["bleBeacon"]   = config.bleBeaconEnabled;
  doc["bleScanSec"]  = config.bleScanDurationSec;
  serializeJson(doc, f);
  f.close();
  LittleFS.end();
  Serial.println("[STORAGE] Config backup saved to LittleFS.");
  return true;
}

static bool loadConfigFromFile() {
  if (!LittleFS.begin(true)) {
    Serial.println("[STORAGE] LittleFS mount failed; no file backup available.");
    return false;
  }
  if (!LittleFS.exists(CONFIG_BACKUP_PATH)) {
    Serial.println("[STORAGE] No config backup file found in LittleFS.");
    LittleFS.end();
    return false;
  }
  File f = LittleFS.open(CONFIG_BACKUP_PATH, FILE_READ);
  if (!f) {
    Serial.println("[STORAGE] Cannot open config backup for reading.");
    LittleFS.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  LittleFS.end();
  if (err) {
    Serial.printf("[STORAGE] Config backup JSON parse error: %s\n", err.c_str());
    return false;
  }
  config.isConfigured = doc["configured"] | false;
  if (!config.isConfigured) {
    Serial.println("[STORAGE] Config backup exists but not marked as configured.");
    return false;
  }
  config.apSSID                = doc["apSSID"]      | "";
  config.apPASS                = doc["apPASS"]      | "";
  config.apIP                  = doc["apIP"]        | "";
  config.nodeName              = doc["nodeName"]    | "DefaultNode";
  config.role                  = doc["role"]        | (int)ROLE_REPEATER;
  config.meshIntervalMin       = doc["meshInt"]     | (int)MESH_APPOINTMENT_INTERVAL_M;
  config.meshWindowSec         = doc["meshWin"]     | (int)MESH_APPOINTMENT_WINDOW_S;
  config.uplinkSSID            = doc["uplinkSSID"]  | ROOT_AP_SSID;
  config.uplinkPASS            = doc["uplinkPASS"]  | ROOT_AP_PASSWORD;
  config.uplinkHost            = doc["uplinkHost"]  | UPLINK_HOST_DEFAULT;
  config.uplinkPort            = doc["uplinkPort"]  | (int)UPLINK_PORT_DEFAULT;
  config.uplinkRoute           = doc["uplinkRoute"] | (int)UPLINK_DIRECT;
  config.sensorAP_SSID         = doc["sensorAP"]   | "DefaultSensorAP";
  config.collectorApCycleSec   = doc["collCyc"]    | (int)COLLECTOR_AP_CYCLE_S;
  config.collectorApWindowSec  = doc["collWin"]    | (int)COLLECTOR_AP_WINDOW_S;
  config.collectorDataTimeoutSec = doc["collTout"] | (int)COLLECTOR_DATA_TIMEOUT_S;
  config.bleBeaconEnabled      = doc["bleBeacon"]  | true;
  config.bleScanDurationSec    = doc["bleScanSec"] | 5;
  Serial.println("[STORAGE] Configuration restored from LittleFS backup.");
  return true;
}

// Internal helper: write config to NVS only, without touching LittleFS.
// Used in recovery paths where LittleFS was the source — no need to re-write
// the file we just read from.
static void saveConfigToNVS() {
  preferences.begin(PREF_NAMESPACE, false);
  // Write 'configured' FIRST so a power cut mid-write leaves the flag set.
  preferences.putBool("configured", config.isConfigured);
  preferences.putString("apSSID", config.apSSID);
  preferences.putString("apPASS", config.apPASS);
  preferences.putString("apIP", config.apIP);
  preferences.putString("nodeName", config.nodeName);
  preferences.putInt("role", config.role);
  preferences.putInt("meshInt", config.meshIntervalMin);
  preferences.putInt("meshWin", config.meshWindowSec);
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
  preferences.putBool("bleBeacon", config.bleBeaconEnabled);
  preferences.putInt("bleScanSec", config.bleScanDurationSec);
  preferences.end();
  Serial.println("[STORAGE] Configuration restored to NVS.");
}

void saveConfiguration() {
  preferences.begin(PREF_NAMESPACE, false);
  // Write the 'configured' sentinel FIRST so that a power cut mid-write still
  // leaves the device knowing it was configured (other keys fall back to safe
  // defaults via getBool/getString defaults in loadConfiguration).
  preferences.putBool("configured", config.isConfigured);
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
  
  preferences.end();
  Serial.println("[STORAGE] Configuration saved to flash.");

  // Also persist to LittleFS so that if the NVS partition is auto-erased by
  // the Arduino framework (power-cut corruption recovery), the config can be
  // restored from this file on the next boot.
  saveConfigToFile();
}

void loadConfiguration() {
  if (!preferences.begin(PREF_NAMESPACE, true)) {
    // NVS namespace missing — most commonly caused by the Arduino framework
    // auto-erasing the NVS partition after a power-cut corruption event
    // (ESP_ERR_NVS_NO_FREE_PAGES / ESP_ERR_NVS_NEW_VERSION_FOUND).
    Serial.println("[STORAGE] ERROR: NVS namespace unavailable — trying LittleFS backup.");
    if (loadConfigFromFile()) {
      // Re-write config back to NVS so subsequent boots are fast.
      // Use NVS-only path to avoid redundant LittleFS write.
      saveConfigToNVS();
    }
    return;
  }
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
    Serial.println("[STORAGE] No configuration found in NVS — trying LittleFS backup.");
    preferences.end();
    if (loadConfigFromFile()) {
      // Re-write config back to NVS so the next boot reads directly from NVS.
      // Use NVS-only path to avoid redundant LittleFS write.
      saveConfigToNVS();
    }
    return;
  }
  preferences.end();
}

void factoryReset() {
  preferences.begin(PREF_NAMESPACE, false);
  preferences.clear();
  preferences.end();
  
  preferences.begin(RTC_NAMESPACE, false);
  preferences.clear();
  preferences.end();

  // Remove the LittleFS backup so the device also re-enters config mode after
  // a factory reset (otherwise the file would restore config on next boot).
  if (LittleFS.begin(true)) {
    if (LittleFS.exists(CONFIG_BACKUP_PATH)) {
      LittleFS.remove(CONFIG_BACKUP_PATH);
    }
    LittleFS.end();
  }

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


// File-scope flag so sdForceReinit() can reset it.
static bool sdInitialized = false;
// After a complete failure (all speeds tried), back off before retrying so the
// main loop does not hammer the SPI bus and flood the log every few ms.
// Use a dedicated boolean rather than a 0-sentinel on the timestamp so the
// cooldown check is rollover-safe via plain unsigned subtraction.
static bool          sdFailPending    = false;
static unsigned long sdFailCooldownMs = 0;
static const unsigned long SD_FAIL_COOLDOWN_MS = 5000; // 5 s between full retries

// Force the next initSdCard() call to reinitialise the bus and card.
// Call this from the main loop when an SD file operation fails unexpectedly.
void sdForceReinit() {
  if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(200)) == pdFALSE) {
    Serial.println("[SD] sdForceReinit: mutex timeout, reinit skipped");
    return;
  }
  sdInitialized = false;
  sdFailPending  = false; // clear cooldown so the forced reinit is tried immediately
  xSemaphoreGive(sdCardMutex);
}

bool initSdCard() {
  if (xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(500)) == pdFALSE)
    return false;

  // Already initialised – nothing to do.
  if (sdInitialized) {
    xSemaphoreGive(sdCardMutex);
    return true;
  }

  // After a complete failure (all speeds tried), wait before retrying to avoid
  // hammering the SPI bus and flooding the log on every main-loop iteration.
  if (sdFailPending && (millis() - sdFailCooldownMs) < SD_FAIL_COOLDOWN_MS) {
    xSemaphoreGive(sdCardMutex);
    return false;
  }

  Serial.println("[SD] (Re)Initializing SD card...");

  // Release any deep-sleep GPIO hold on CS so we can drive the pin freely.
  gpio_hold_dis((gpio_num_t)SD_CS_PIN);

  // Try progressively slower SPI speeds: 10 → 8 → 4 → 2 → 1 MHz.
  // Lower speeds help when wiring is long, power supply is noisy, or the
  // card is marginal.  1 MHz is well within spec for all SD cards.
  const uint32_t speeds[] = { 10, 8, 4, 2, 1 };
  bool success = false;

  for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
    if (i > 0) {
      // Clean up the previous failed SdFat session before retrying.
      sd.end();
      Serial.printf("[SD] Card Mount Failed at %u MHz – retrying at %u MHz...\n",
                    speeds[i - 1], speeds[i]);
      delay(300);
    }

    // (Re)initialise the SPI bus.
    SPI.end();
    delay(50);

    // Deassert CS HIGH before touching the bus.  SD cards require CS to be
    // idle (HIGH) during the power-up clock pulses; a floating or LOW CS can
    // cause the card to latch garbage and refuse to initialise.
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    delay(10);

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    delay(20);

    // Per the SD Physical Layer Spec, the host must send at least 74 clock
    // cycles with CS HIGH (and DI/MOSI HIGH) to transition the card from
    // SD-bus power-up mode into SPI mode.  SdFat normally issues these, but
    // sending them explicitly here guarantees the card is in a known state
    // before sd.begin() negotiates the protocol — particularly important
    // after a warm reset or deep sleep where the card was not power-cycled.
    SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
    for (int k = 0; k < 10; k++) SPI.transfer(0xFF); // 80 clock cycles
    SPI.endTransaction();
    delay(10);

    SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(speeds[i]));
    success = sd.begin(cfg);
    if (success) break;
  }

  if (success) {
    Serial.println("[SD] Card initialized successfully.");
    sdInitialized = true;
    sdFailPending = false; // clear cooldown on success
  } else {
    Serial.printf("[SD] Card Mount Failed at %u MHz (final).\n", speeds[sizeof(speeds)/sizeof(speeds[0])-1]);
    Serial.println("[SD] Check wiring (CLK=19 MISO=20 MOSI=21 CS=18), VCC=3.3V, card format=FAT32.");
    Serial.println("[SD] Flash SDTest/SDTest.ino for standalone hardware diagnosis.");
    sdInitialized    = false;
    sdFailPending    = true;
    sdFailCooldownMs = millis(); // back off for SD_FAIL_COOLDOWN_MS before next attempt
  }

  xSemaphoreGive(sdCardMutex);
  return success;
}

