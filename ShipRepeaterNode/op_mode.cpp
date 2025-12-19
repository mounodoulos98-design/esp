#include "config.h"
#include "firmware_updater.h"
#include "config_updater.h"
#include "station_job_manager.h"
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include <algorithm>
#include <sys/time.h>
#include "sensor_heartbeat_manager.h"
#include "ble_mesh_beacon.h"



extern "C" {
#include "esp_event.h"
}

// === SAFE AP bring-up helper (final stable) ===
static bool safeBringUpAP(const String& ssidIn, const String& passIn, const String& ipStr, const char* tag) {
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(300);
  yield();

  // Δημιουργία default event loop αν δεν υπάρχει (διορθώνει το "Invalid mbox")
  static bool loopCreated = false;
  if (!loopCreated) {
    esp_err_t res = esp_event_loop_create_default();
    if (res == ESP_ERR_INVALID_STATE) {
      Serial.println("[WiFi] Event loop already exists.");
    } else if (res == ESP_OK) {
      Serial.println("[WiFi] Event loop created.");
    } else {
      Serial.printf("[WiFi] Event loop error: %d\n", res);
    }
    loopCreated = true;
  }

  WiFi.mode(WIFI_AP_STA);
  delay(300);
  yield();

  IPAddress ap_ip;
  ap_ip.fromString(ipStr);
  IPAddress subnet(255, 255, 255, 0);
  String ssid = ssidIn.length() ? ssidIn : String(tag);
  String pass = passIn;
  if (pass.length() < 8) pass = "";

  bool ok_cfg = WiFi.softAPConfig(ap_ip, ap_ip, subnet);
  bool ok_ap = WiFi.softAP(ssid.c_str(), pass.c_str());
  delay(200);
  yield();

  Serial.printf("[%s] cfg=%s ap=%s | SSID=%s | IP=%s\n",
                tag, ok_cfg ? "OK" : "FAIL", ok_ap ? "OK" : "FAIL",
                ssid.c_str(), WiFi.softAPIP().toString().c_str());
  return ok_cfg && ok_ap;
}

// External SD init
extern bool initSdCard();

#ifndef INITIAL_SYNC_TIMEOUT_MS
#define INITIAL_SYNC_TIMEOUT_MS 180000
#endif

// =============================
// State & Global Variables
// =============================
std::map<String, std::vector<uint8_t>> rxBufs;  // (kept for possible future use)
bool sdDeferredSave = false;
State currentState;
bool apActive = false;
bool needToSyncTime = false;
// Κάπου δίπλα στα άλλα singletons
static SensorHeartbeatManager heartbeatManager;
// Heartbeat server for sensors on port 3000
static AsyncWebServer sensorServer(3000);

// BLE Mesh Wake-up: Beacon for Root/Repeater, Scanner for Collector/Repeater
static BLEBeaconManager bleBeacon;
static BLEScannerManager bleScanner;

// Repeater dynamic WiFi AP control
static bool repeaterWiFiAPActive = false;
static unsigned long repeaterAPStartTime = 0;

// Repeater WiFi AP timeouts (configurable)
#define REPEATER_NO_CLIENT_TIMEOUT_MS 30000   // 30 seconds
#define REPEATER_MAX_AP_TIME_MS 300000        // 5 minutes
#define REPEATER_AP_STARTUP_DELAY_MS 3000     // 3 seconds

// Uplink window maximum duration (now configurable via web UI)
// Note: config.uplinkMaxWindowSec is loaded from configuration

// Forward declarations for Repeater WiFi control
void startRepeaterWiFiAP();
void ensureRepeaterHttpServer();

// BLE Wake-up callback for Repeater
class RepeaterWakeupCallback : public BLEWakeupCallback {
public:
    void onWakeupRequest() override {
        Serial.println("[REPEATER-WAKEUP] Wake-up request received via BLE");
        // Start WiFi AP when wake-up signal received
        startRepeaterWiFiAP();
        ensureRepeaterHttpServer();
    }
};

static RepeaterWakeupCallback repeaterWakeupCb;

// === Simple Memory-Based Buffer for Callback-to-Loop Communication ===
// FreeRTOS queues cause mutex crashes when used from AsyncWebServer callbacks.
// This uses simple volatile arrays that are safe to write from any context.

// Heartbeat buffer - stores pending heartbeats for SD logging and job execution
struct HeartbeatEntry {
  char sensorSn[32];
  char sensorIp[20];
  bool hasData;
  bool needsJobCheck;
  uint8_t statusData[256];
  size_t statusDataLen;
};

static volatile int hbBufferWriteIdx = 0;
static volatile int hbBufferReadIdx = 0;
static const int HB_BUFFER_SIZE = 10;
static HeartbeatEntry hbBuffer[HB_BUFFER_SIZE];

// Queue a heartbeat from callback (safe - no FreeRTOS calls)
static void bufferHeartbeat(const String& sn, const String& ip, bool needsJobCheck = false, 
                            const uint8_t* statusData = nullptr, size_t statusDataLen = 0) {
  int nextIdx = (hbBufferWriteIdx + 1) % HB_BUFFER_SIZE;
  if (nextIdx == hbBufferReadIdx) {
    // Buffer full, drop oldest
    hbBufferReadIdx = (hbBufferReadIdx + 1) % HB_BUFFER_SIZE;
  }
  
  HeartbeatEntry& entry = hbBuffer[hbBufferWriteIdx];
  strncpy((char*)entry.sensorSn, sn.c_str(), sizeof(entry.sensorSn) - 1);
  entry.sensorSn[sizeof(entry.sensorSn) - 1] = '\0';
  strncpy((char*)entry.sensorIp, ip.c_str(), sizeof(entry.sensorIp) - 1);
  entry.sensorIp[sizeof(entry.sensorIp) - 1] = '\0';
  entry.hasData = true;
  entry.needsJobCheck = needsJobCheck;
  
  if (statusData && statusDataLen > 0 && statusDataLen <= sizeof(entry.statusData)) {
    memcpy((void*)entry.statusData, statusData, statusDataLen);
    entry.statusDataLen = statusDataLen;
  } else {
    entry.statusDataLen = 0;
  }
  
  hbBufferWriteIdx = nextIdx;
}

// Forward declaration - implemented after ensureDir
static void processHeartbeatBuffer();

// Collector AP State (sensor intake / command execution)
bool hadStation = false;
unsigned long lastActivityMillis = 0;
unsigned long lastHeartbeatMillis = 0;  // Track last actual heartbeat from any sensor
static WiFiEventId_t stationConnectedEventId;

// RTC Memory
RTC_DATA_ATTR time_t rtc_last_known_time = 0;
RTC_DATA_ATTR uint32_t rtc_last_sleep_duration_s = 0;
RTC_DATA_ATTR State rtc_next_state = STATE_INITIAL;

// TX state (collector) for HTTP uploads from SD queue
static bool txActive = false;

// =============================
// Small utils (SD queue)
// =============================
static const char* QUEUE_DIR = "/queue";
static const char* RECEIVED_DIR = "/received";
static const char* JOB_FILE = "/jobs/job.json";
static const char* QUEUE_NS = "queue_store";

static void ensureDir(const char* path) {
  if (!initSdCard()) {
    Serial.println("[SD] initSdCard() failed, retrying...");
    delay(100);
    initSdCard();
  }
  if (!sd.exists(path)) {
    if (!sd.mkdir(path)) {
      Serial.printf("[SD] mkdir(%s) failed!\n", path);
    } else {
      Serial.printf("[SD] mkdir(%s) OK\n", path);
    }
  }
}

// Process buffered heartbeats from main loop (safe for SD and job operations)
static void processHeartbeatBuffer() {
  while (hbBufferReadIdx != hbBufferWriteIdx) {
    HeartbeatEntry& entry = hbBuffer[hbBufferReadIdx];
    
    if (entry.hasData) {
      String sn = String((char*)entry.sensorSn);
      String ip = String((char*)entry.sensorIp);
      
      // Update last heartbeat time
      lastHeartbeatMillis = millis();
      
      // Log heartbeat to SD
      if (initSdCard()) {
        ensureDir(RECEIVED_DIR);
        
        // Get timestamp
        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);
        char timestamp[32];
        if (timeinfo && timeinfo->tm_year > (2023 - 1900)) {
          snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                   timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                   timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        } else {
          snprintf(timestamp, sizeof(timestamp), "T%lu", millis());
        }
        
        // Append heartbeat to CSV
        FsFile f = sd.open("/received/heartbeat_api.csv", O_WRONLY | O_CREAT | O_APPEND);
        if (f) {
          f.printf("%s,%s,%s\n", timestamp, sn.c_str(), ip.c_str());
          f.close();
          Serial.printf("[HB-BUFFER] Logged heartbeat to SD: %s\n", sn.c_str());
        }
        
        // Save status data if present
        if (entry.statusDataLen > 0) {
          char statusFile[64];
          snprintf(statusFile, sizeof(statusFile), "/received/status_%s_%lu.txt", 
                   sn.c_str(), (unsigned long)now);
          FsFile sf = sd.open(statusFile, O_WRONLY | O_CREAT | O_TRUNC);
          if (sf) {
            sf.write((uint8_t*)entry.statusData, entry.statusDataLen);
            sf.close();
            Serial.printf("[HB-BUFFER] Saved status data: %s (%d bytes)\n", 
                         statusFile, (int)entry.statusDataLen);
          }
        }
      }
      
      // Execute jobs if needed
      if (entry.needsJobCheck) {
        Serial.printf("[HB-BUFFER] Checking jobs for SN=%s IP=%s\n", sn.c_str(), ip.c_str());
        bool didJobs = processJobsForSN(sn, ip);
        if (didJobs) {
          Serial.printf("[HB-BUFFER] Jobs executed for SN=%s\n", sn.c_str());
        } else {
          Serial.printf("[HB-BUFFER] No jobs found for SN=%s\n", sn.c_str());
        }
      }
      
      entry.hasData = false;
    }
    
    hbBufferReadIdx = (hbBufferReadIdx + 1) % HB_BUFFER_SIZE;
  }
}

// Log heartbeat to CSV file (like sensordaemon)
static void appendToHeartbeatLog(const String& sensorSn) {
  if (!initSdCard()) return;
  
  ensureDir(RECEIVED_DIR);
  const char* hbFile = "/received/heartbeat_api.csv";
  
  // Get current time (epoch or millis if no RTC sync)
  time_t now;
  time(&now);
  struct tm* timeinfo = localtime(&now);
  
  char timestamp[32];
  if (timeinfo && timeinfo->tm_year > (2023 - 1900)) {
    // Valid time - use ISO 8601 format
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
             timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  } else {
    // No valid time - use millis
    snprintf(timestamp, sizeof(timestamp), "T%lu", millis());
  }
  
  // Append to CSV file
  FsFile f = sd.open(hbFile, O_WRONLY | O_CREAT | O_APPEND);
  if (f) {
    f.printf("%s,%s\n", timestamp, sensorSn.c_str());
    f.close();
    Serial.printf("[HB] Logged heartbeat: %s,%s\n", timestamp, sensorSn.c_str());
  } else {
    Serial.printf("[HB] Failed to open %s\n", hbFile);
  }
}

// next progressive filename: /queue/entry_00000001.bin
static String nextQueueFilename() {
  ensureDir(QUEUE_DIR);
  preferences.begin(QUEUE_NS, false);
  uint32_t idx = preferences.getUInt("idx", 0);
  idx++;
  preferences.putUInt("idx", idx);
  preferences.end();
  char name[64];
  snprintf(name, sizeof(name), "%s/entry_%08lu.bin", QUEUE_DIR, (unsigned long)idx);
  return String(name);
}

// find oldest file in /queue (lexicographically)
static bool findOldestQueueFile(String& outName) {
  if (!initSdCard()) return false;
  ensureDir(QUEUE_DIR);
  FsFile dir = sd.open(QUEUE_DIR);
  if (!dir) return false;
  String best = "";
  while (true) {
    FsFile f = dir.openNextFile();
    if (!f) break;
    if (f.isDir()) {
      f.close();
      continue;
    }
    char fname[64];
    f.getName(fname, sizeof(fname));
    String name = String(fname);
    f.close();
    if (name.endsWith(".bin")) {
      if (best.length() == 0 || name < best) best = name;
    }
  }
  dir.close();
  if (best.length() == 0) return false;
  char full[96];
  snprintf(full, sizeof(full), "%s/%s", QUEUE_DIR, best.c_str());
  outName = String(full);
  return true;
}

// =============================
// DEBUG HELPERS
// =============================
static void debugPrintTime(const char* tag) {
  time_t now;
  time(&now);
  struct tm* t = localtime(&now);
  if (t)
    Serial.printf("[DEBUG_TIME] %s -> %04d-%02d-%02d %02d:%02d:%02d (epoch=%ld)\n",
                  tag,
                  t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                  t->tm_hour, t->tm_min, t->tm_sec, now);
  else
    Serial.printf("[DEBUG_TIME] %s -> invalid time\n", tag);
}

// =============================
// ROOT / REPEATER helpers
// =============================
void ensureWiFiAPRoot() {
  static bool up = false;
  if (up) return;
  String ssid = config.apSSID.length() ? config.apSSID : String("Root_AP");
  String pass = config.apPASS;
  String ipStr = config.apIP.length() ? config.apIP : String("192.168.10.1");
  bool ok = safeBringUpAP(ssid, pass, ipStr, "ROOT");
  if (ok) {
    Serial.printf("[ROOT] SoftAP %s: OK | IP=%s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("[ROOT] Failed to start AP!");
  }
  up = ok;
}

// =============================
// ROOT: HTTP Server (/health, /time, /ingest, /jobs, /firmware)
// =============================
static bool rootHttpActive = false;
static AsyncWebServer rootServer(8080);

void ensureRootHttpServer() {
  if (rootHttpActive) return;
  if (!initSdCard()) return;
  ensureDir(RECEIVED_DIR);

  rootServer.on("/health", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
  });

  rootServer.on("/time", HTTP_GET, [](AsyncWebServerRequest* req) {
    time_t now;
    time(&now);
    String json = String("{\"epoch\":") + String((unsigned long)now) + "}";
    req->send(200, "application/json", json);
  });

  // Serve jobs files for collectors
  rootServer.on("/jobs/config_jobs.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!initSdCard()) {
      req->send(404, "text/plain", "SD card not available");
      return;
    }
    String path = "/jobs/config_jobs.json";
    if (sd.exists(path.c_str())) {
      FsFile file = sd.open(path.c_str(), O_RDONLY);
      if (file) {
        String content = "";
        while (file.available()) {
          content += (char)file.read();
        }
        file.close();
        req->send(200, "application/json", content);
        Serial.println("[ROOT] Served /jobs/config_jobs.json");
      } else {
        req->send(500, "text/plain", "Failed to open file");
      }
    } else {
      req->send(404, "text/plain", "File not found");
    }
  });

  rootServer.on("/jobs/firmware_jobs.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!initSdCard()) {
      req->send(404, "text/plain", "SD card not available");
      return;
    }
    String path = "/jobs/firmware_jobs.json";
    if (sd.exists(path.c_str())) {
      FsFile file = sd.open(path.c_str(), O_RDONLY);
      if (file) {
        String content = "";
        while (file.available()) {
          content += (char)file.read();
        }
        file.close();
        req->send(200, "application/json", content);
        Serial.println("[ROOT] Served /jobs/firmware_jobs.json");
      } else {
        req->send(500, "text/plain", "Failed to open file");
      }
    } else {
      req->send(404, "text/plain", "File not found");
    }
  });

  // Serve firmware hex files
  // Note: For large files, consider using chunked response to avoid memory issues
  rootServer.on("/firmware/*", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!initSdCard()) {
      req->send(404, "text/plain", "SD card not available");
      return;
    }
    String path = req->url();
    if (sd.exists(path.c_str())) {
      FsFile file = sd.open(path.c_str(), O_RDONLY);
      if (file) {
        // Read entire file content (works for reasonably sized files)
        size_t fileSize = file.size();
        String content = "";
        content.reserve(fileSize + 1);
        while (file.available()) {
          content += (char)file.read();
        }
        file.close();
        req->send(200, "application/octet-stream", content);
        Serial.printf("[ROOT] Served %s (%d bytes)\n", path.c_str(), fileSize);
      } else {
        req->send(500, "text/plain", "Failed to open file");
      }
    } else {
      req->send(404, "text/plain", "File not found");
    }
  });

  // Multipart/form-data upload to /ingest
  rootServer.on(
    "/ingest", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      static FsFile upFile;
      static String current;
      if (index == 0) {
        char name[64];
        snprintf(name, sizeof(name), "%lu_", (unsigned long)millis());
        current = String(RECEIVED_DIR) + "/" + name + filename;
        upFile = sd.open(current.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        Serial.printf("[ROOT] Receiving file: %s\n", current.c_str());
      }
      if (upFile) { upFile.write(data, len); }
      if (final) {
        if (upFile) upFile.close();
        request->send(200, "text/plain", "OK");
        Serial.printf("[ROOT] Saved file: %s\n", current.c_str());
      }
    });

  rootServer.begin();
  rootHttpActive = true;
  Serial.println("[ROOT] HTTP server started on :8080 (/health, /time, /ingest, /jobs, /firmware)");
}

// Start Repeater WiFi AP (dynamic control)
void startRepeaterWiFiAP() {
  if (repeaterWiFiAPActive) return;
  
  String ssid = config.apSSID.length() ? config.apSSID : String("Repeater_AP");
  String pass = config.apPASS;
  String ipStr = config.apIP.length() ? config.apIP : String("192.168.20.1");
  bool ok = safeBringUpAP(ssid, pass, ipStr, "REPEATER");
  
  if (ok) {
    repeaterWiFiAPActive = true;
    repeaterAPStartTime = millis();
    Serial.printf("[REPEATER] WiFi AP started: %s | IP=%s\n", 
                  ssid.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("[REPEATER] Failed to start WiFi AP!");
  }
}

// Stop Repeater WiFi AP (dynamic control)
void stopRepeaterWiFiAP() {
  if (!repeaterWiFiAPActive) return;
  
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  repeaterWiFiAPActive = false;
  
  Serial.println("[REPEATER] WiFi AP stopped, entering light sleep mode");
}

void ensureWiFiAPRepeater() {
  static bool up = false;
  if (up) return;
  String ssid = config.apSSID.length() ? config.apSSID : String("Repeater_AP");
  String pass = config.apPASS;
  String ipStr = config.apIP.length() ? config.apIP : String("192.168.20.1");
  bool ok = safeBringUpAP(ssid, pass, ipStr, "REPEATER");
  if (ok) {
    Serial.printf("[REPEATER] SoftAP %s: OK | IP=%s\n", ssid.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("[REPEATER] Failed to start AP!");
  }
  up = ok;
}

// =============================
// REPEATER: lightweight /time relay
// =============================
static bool repeaterHttpActive = false;
static AsyncWebServer rptServer(8080);

bool syncTimeFromUplink(unsigned long timeout_ms) {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[TIME] STA to %s...\n", config.uplinkSSID.c_str());
    WiFi.begin(config.uplinkSSID.c_str(), config.uplinkPASS.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeout_ms) {
      delay(200);
      // Reset watchdog during WiFi connection attempt to prevent timeout
      esp_task_wdt_reset();
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TIME] STA connect failed");
    return false;
  }
  // Auto-detect parent IP if not configured (use gateway IP from DHCP)
  String targetHost = config.uplinkHost;
  if (targetHost.length() == 0 || targetHost == "Auto" || targetHost == "auto") {
    IPAddress gateway = WiFi.gatewayIP();
    targetHost = gateway.toString();
    Serial.printf("[TIME] Auto-detected parent IP: %s (gateway)\n", targetHost.c_str());
  }
  
  WiFiClient client;
  if (!client.connect(targetHost.c_str(), config.uplinkPort)) {
    Serial.println("[TIME] Connect host failed");
    return false;
  }
  client.print(String("GET /time HTTP/1.1\r\nHost: ") + targetHost + "\r\nConnection: close\r\n\r\n");
  unsigned long t0 = millis();
  while (client.connected() && !client.available() && millis() - t0 < 3000) delay(10);
  String resp = "";
  while (client.available()) resp += (char)client.read();
  client.stop();
  int p = resp.indexOf("{\"epoch\":");
  if (p < 0) {
    Serial.println("[TIME] epoch JSON not found");
    return false;
  }
  unsigned long epoch = 0;
  for (int i = p + 9; i < resp.length(); ++i) {
    char c = resp[i];
    if (c >= '0' && c <= '9') {
      epoch = epoch * 10 + (c - '0');
    } else if (epoch > 0) break;
  }
  if (epoch > 1700000000UL) {
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    persistRtcTime((time_t)epoch);
    needToSyncTime = false;
    Serial.printf("[TIME] Synced from uplink: %lu\n", epoch);
    return true;
  }
  return false;
}

void ensureRepeaterHttpServer() {
  if (repeaterHttpActive) return;
  
  // Time endpoint
  rptServer.on("/time", HTTP_GET, [](AsyncWebServerRequest* req) {
    time_t now;
    time(&now);
    if (now < 1700000000UL) { now = restoreRtcTime(); }
    String json = String("{\"epoch\":") + String((unsigned long)now) + "}";
    req->send(200, "application/json", json);
  });
  
  // Job endpoints - serve jobs to Collectors (same as Root)
  rptServer.on("/jobs/config_jobs.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!initSdCard()) {
      req->send(404, "text/plain", "SD card not available");
      return;
    }
    String path = "/jobs/config_jobs.json";
    if (sd.exists(path.c_str())) {
      FsFile file = sd.open(path.c_str(), O_RDONLY);
      if (file) {
        String content = "";
        while (file.available()) {
          content += (char)file.read();
        }
        file.close();
        req->send(200, "application/json", content);
        Serial.println("[REPEATER] Served /jobs/config_jobs.json to Collector");
      } else {
        req->send(500, "text/plain", "Failed to open file");
      }
    } else {
      req->send(404, "text/plain", "File not found");
    }
  });

  rptServer.on("/jobs/firmware_jobs.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!initSdCard()) {
      req->send(404, "text/plain", "SD card not available");
      return;
    }
    String path = "/jobs/firmware_jobs.json";
    if (sd.exists(path.c_str())) {
      FsFile file = sd.open(path.c_str(), O_RDONLY);
      if (file) {
        String content = "";
        while (file.available()) {
          content += (char)file.read();
        }
        file.close();
        req->send(200, "application/json", content);
        Serial.println("[REPEATER] Served /jobs/firmware_jobs.json to Collector");
      } else {
        req->send(500, "text/plain", "Failed to open file");
      }
    } else {
      req->send(404, "text/plain", "File not found");
    }
  });

  // Serve firmware hex files to Collectors
  rptServer.on("/firmware/*", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!initSdCard()) {
      req->send(404, "text/plain", "SD card not available");
      return;
    }
    String path = req->url();
    if (sd.exists(path.c_str())) {
      FsFile file = sd.open(path.c_str(), O_RDONLY);
      if (file) {
        size_t fileSize = file.size();
        String content = "";
        content.reserve(fileSize + 1);
        while (file.available()) {
          content += (char)file.read();
        }
        file.close();
        req->send(200, "application/octet-stream", content);
        Serial.printf("[REPEATER] Served %s to Collector (%d bytes)\n", path.c_str(), fileSize);
      } else {
        req->send(500, "text/plain", "Failed to open file");
      }
    } else {
      req->send(404, "text/plain", "File not found");
    }
  });
  
  // Multipart/form-data upload endpoint for Collectors to send data
  rptServer.on(
    "/ingest", HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
      static FsFile upFile;
      static String current;
      if (index == 0) {
        if (!initSdCard()) {
          Serial.println("[REPEATER] SD card init failed for upload");
          return;
        }
        ensureDir(RECEIVED_DIR);
        char name[64];
        snprintf(name, sizeof(name), "%lu_", (unsigned long)millis());
        current = String(RECEIVED_DIR) + "/" + name + filename;
        upFile = sd.open(current.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        Serial.printf("[REPEATER] Receiving file from Collector: %s\n", current.c_str());
      }
      if (upFile) { upFile.write(data, len); }
      if (final) {
        if (upFile) upFile.close();
        request->send(200, "text/plain", "OK");
        Serial.printf("[REPEATER] Saved file from Collector: %s\n", current.c_str());
      }
    });
  
  rptServer.begin();
  repeaterHttpActive = true;
  Serial.println("[REPEATER] HTTP server ready on :8080 (/time, /jobs, /firmware, /ingest)");
}

// =============================
// Collector: HTTP upload to Root
// =============================
bool uploadFileToRoot(const String& fullPath, const String& basename) {
  if (!initSdCard()) return false;
  FsFile f = sd.open(fullPath.c_str(), O_RDONLY);
  if (!f) {
    Serial.printf("[HTTP UP] Cannot open %s\n", fullPath.c_str());
    return false;
  }
  f.seekEnd(0);
  uint32_t fsize = f.curPosition();
  f.rewind();

  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[UPLINK] Connecting STA to %s...\n", config.uplinkSSID.c_str());
    WiFi.begin(config.uplinkSSID.c_str(), config.uplinkPASS.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      delay(200);
      // Reset watchdog during WiFi connection attempt to prevent timeout
      esp_task_wdt_reset();
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[UPLINK] STA connect failed");
    f.close();
    return false;
  }

  // Auto-detect parent IP if not configured (use gateway IP from DHCP)
  String targetHost = config.uplinkHost;
  if (targetHost.length() == 0 || targetHost == "Auto" || targetHost == "auto") {
    IPAddress gateway = WiFi.gatewayIP();
    targetHost = gateway.toString();
    Serial.printf("[HTTP UP] Auto-detected parent IP: %s (gateway)\n", targetHost.c_str());
  }
  
  WiFiClient client;
  Serial.printf("[HTTP UP] Connecting to %s:%d...\n", targetHost.c_str(), config.uplinkPort);
  if (!client.connect(targetHost.c_str(), config.uplinkPort)) {
    Serial.println("[HTTP UP] Connect failed");
    f.close();
    return false;
  }

  String boundary = "----esp32bound" + String(millis());
  String head = "POST /ingest HTTP/1.1\r\nHost: " + targetHost + "\r\n";
  head += "Connection: close\r\nContent-Type: multipart/form-data; boundary=" + boundary + "\r\n";
  String pre = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + basename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  String post = "\r\n--" + boundary + "--\r\n";
  uint32_t contentLength = pre.length() + fsize + post.length();
  head += "Content-Length: " + String(contentLength) + "\r\n\r\n";

  client.print(head);
  client.print(pre);
  uint8_t buf[SD_CHUNK_SIZE];
  while (f.available()) {
    int rd = f.read(buf, sizeof(buf));
    if (rd > 0) client.write(buf, rd);
    delay(0);
  }
  client.print(post);
  f.close();

  unsigned long t0 = millis();
  while (client.connected() && millis() - t0 < 10000) {
    while (client.available()) {
      client.read();
      t0 = millis();
    }
    delay(10);
  }
  client.stop();
  Serial.printf("[HTTP UP] Uploaded %s (%lu bytes)\n", basename.c_str(), (unsigned long)fsize);
  return true;
}

// Collector: Download file from root server
// =============================
bool downloadFileFromRoot(const String& remotePath, const String& localPath) {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[DOWNLOAD] Connecting STA to %s...\n", config.uplinkSSID.c_str());
    WiFi.begin(config.uplinkSSID.c_str(), config.uplinkPASS.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      delay(200);
      // Reset watchdog during WiFi connection attempt to prevent timeout
      esp_task_wdt_reset();
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[DOWNLOAD] STA connect failed");
    return false;
  }

  // Auto-detect parent IP if not configured (use gateway IP from DHCP)
  String targetHost = config.uplinkHost;
  if (targetHost.length() == 0 || targetHost == "Auto" || targetHost == "auto") {
    IPAddress gateway = WiFi.gatewayIP();
    targetHost = gateway.toString();
    Serial.printf("[DOWNLOAD] Auto-detected parent IP: %s (gateway)\n", targetHost.c_str());
  }
  
  WiFiClient client;
  Serial.printf("[DOWNLOAD] Fetching http://%s:%d%s...\n", 
                targetHost.c_str(), config.uplinkPort, remotePath.c_str());
  
  if (!client.connect(targetHost.c_str(), config.uplinkPort)) {
    Serial.println("[DOWNLOAD] Connect failed");
    return false;
  }

  String request = String("GET ") + remotePath + " HTTP/1.1\r\n";
  request += "Host: " + config.uplinkHost + "\r\n";
  request += "Connection: close\r\n\r\n";
  client.print(request);

  // Wait for response
  unsigned long t0 = millis();
  while (client.connected() && !client.available() && millis() - t0 < 5000) {
    delay(10);
  }
  
  if (!client.available()) {
    Serial.println("[DOWNLOAD] No response");
    client.stop();
    return false;
  }

  // Read headers
  String line;
  int contentLength = -1;
  bool is404 = false;
  while (client.available()) {
    line = client.readStringUntil('\n');
    line.trim();
    if (line.startsWith("HTTP/")) {
      if (line.indexOf("404") > 0) is404 = true;
    }
    if (line.startsWith("Content-Length:")) {
      contentLength = line.substring(15).toInt();
    }
    if (line.length() == 0) break; // End of headers
  }

  if (is404) {
    Serial.printf("[DOWNLOAD] File not found: %s\n", remotePath.c_str());
    client.stop();
    return false;
  }

  // Download body to file
  if (!initSdCard()) {
    client.stop();
    return false;
  }

  // Ensure directory exists
  int lastSlash = localPath.lastIndexOf('/');
  if (lastSlash > 0) {
    String dir = localPath.substring(0, lastSlash);
    ensureDir(dir.c_str());
  }

  FsFile f = sd.open(localPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) {
    Serial.printf("[DOWNLOAD] Cannot create %s\n", localPath.c_str());
    client.stop();
    return false;
  }

  int bytesReceived = 0;
  while (client.connected() || client.available()) {
    if (client.available()) {
      uint8_t buf[512];
      int len = client.read(buf, sizeof(buf));
      if (len > 0) {
        f.write(buf, len);
        bytesReceived += len;
      }
    }
    delay(1);
  }
  
  f.close();
  client.stop();
  
  Serial.printf("[DOWNLOAD] Downloaded %s (%d bytes) -> %s\n", 
                remotePath.c_str(), bytesReceived, localPath.c_str());
  return bytesReceived > 0;
}

// Collector: Sync jobs from root
// =============================
static void syncJobsFromRoot() {
  if (config.role != ROLE_COLLECTOR) return;
  
  Serial.println("[SYNC] Syncing jobs from root...");
  
  // Reset watchdog before potentially long downloads
  esp_task_wdt_reset();
  
  // Download config jobs
  bool cfgOk = downloadFileFromRoot("/jobs/config_jobs.json", "/jobs/config_jobs.json");
  if (cfgOk) {
    Serial.println("[SYNC] Config jobs updated");
  }
  
  // Download firmware jobs
  bool fwOk = downloadFileFromRoot("/jobs/firmware_jobs.json", "/jobs/firmware_jobs.json");
  if (fwOk) {
    Serial.println("[SYNC] Firmware jobs updated");
  }
  
  // Reset job cache to force reload
  resetJobCache();
}

// Collector: Upload ALL queued files (queue-based with timeout)
// =============================
void uploadAllQueuedFiles(unsigned long maxDurationMs = 0) {
  if (config.role != ROLE_COLLECTOR) return;
  
  Serial.println("[QUEUE-UPLOAD] Starting queue-based file upload...");
  
  unsigned long startTime = millis();
  int filesUploaded = 0;
  int filesSkipped = 0;
  const int MAX_RETRY = 3;  // Retry failed uploads up to 3 times
  String oldest;
  
  // Upload ALL files in queue until empty OR timeout reached
  while (findOldestQueueFile(oldest)) {
    // Reset watchdog to prevent timeout during long upload sessions
    esp_task_wdt_reset();
    
    // Check if we've exceeded the maximum duration
    if (maxDurationMs > 0 && (millis() - startTime) > maxDurationMs) {
      Serial.printf("[QUEUE-UPLOAD] Max duration reached (%lu ms), stopping upload session\n", maxDurationMs);
      Serial.printf("[QUEUE-UPLOAD] Uploaded %d files, skipped %d files\n", filesUploaded, filesSkipped);
      Serial.println("[QUEUE-UPLOAD] Remaining files will be uploaded in next cycle");
      return;
    }
    
    if (!initSdCard()) {
      Serial.println("[QUEUE-UPLOAD] SD card access failed");
      break;
    }
    
    String base = oldest.substring(String(QUEUE_DIR).length() + 1);
    Serial.printf("[QUEUE-UPLOAD] Uploading file %d: %s\n", filesUploaded + filesSkipped + 1, base.c_str());
    
    bool ok = false;
    for (int retry = 0; retry < MAX_RETRY && !ok; retry++) {
      if (retry > 0) {
        Serial.printf("[QUEUE-UPLOAD] Retry attempt %d/%d for: %s\n", retry, MAX_RETRY - 1, base.c_str());
        delay(1000);  // Wait 1 second before retry
      }
      ok = uploadFileToRoot(oldest, base);
      
      // Check timeout even during retries
      if (maxDurationMs > 0 && (millis() - startTime) > maxDurationMs) {
        Serial.println("[QUEUE-UPLOAD] Timeout reached during retry, aborting");
        break;
      }
    }
    
    if (ok && initSdCard()) { 
      sd.remove(oldest.c_str());
      filesUploaded++;
      Serial.printf("[QUEUE-UPLOAD] Successfully uploaded and removed: %s\n", oldest.c_str());
    } else {
      filesSkipped++;
      Serial.printf("[QUEUE-UPLOAD] Failed to upload after %d attempts: %s (continuing with next file)\n", 
                    MAX_RETRY, oldest.c_str());
      // Continue with next file instead of breaking
      // Note: Failed file remains in queue for next cycle
    }
    
    delay(100); // Small delay between uploads
  }
  
  Serial.printf("[QUEUE-UPLOAD] Queue upload complete. Files uploaded: %d, skipped: %d\n", 
                filesUploaded, filesSkipped);
}

// Collector: Upload queue and sync jobs (legacy function, now calls queue-based upload)
// =============================
void processQueue() {
  // For COLLECTOR: uplink via HTTP
  if (config.role == ROLE_COLLECTOR) {
    String oldest;
    if (!findOldestQueueFile(oldest)) {
      // No files in queue - sync jobs from root
      syncJobsFromRoot();
      return;
    }
    
    if (!initSdCard()) return;
    String base = oldest.substring(String(QUEUE_DIR).length() + 1);
    bool ok = uploadFileToRoot(oldest, base);
    if (ok && initSdCard()) { 
      sd.remove(oldest.c_str());
      Serial.printf("[QUEUE] Removed uploaded file: %s\n", oldest.c_str());
    }
    return;
  }
}

// =============================
// JOB EXECUTION (JSON-based)
// =============================

// very small helper to do one HTTP GET (for STATUS / CONFIGURE)
static bool doSimpleHttpGet(const String& url, String& bodyOut, unsigned long timeoutMs = 5000) {
  Serial.printf("[HTTP] GET %s\n", url.c_str());
  WiFiClient client;
  // Extract host & path from full URL "http://x.x.x.x/....."
  String host, path;
  if (url.startsWith("http://")) {
    int hostStart = 7;
    int slash = url.indexOf('/', hostStart);
    if (slash < 0) slash = url.length();
    host = url.substring(hostStart, slash);
    path = (slash < (int)url.length()) ? url.substring(slash) : "/";
  } else {
    Serial.println("[HTTP] URL must start with http://");
    return false;
  }

  if (!client.connect(host.c_str(), 80)) {
    Serial.println("[HTTP] connect() failed");
    return false;
  }

  client.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");

  unsigned long t0 = millis();
  while (client.connected() && !client.available() && (millis() - t0) < timeoutMs) {
    delay(10);
  }
  if (!client.available()) {
    Serial.println("[HTTP] no data");
    client.stop();
    return false;
  }

  String resp;
  while (client.available()) {
    resp += (char)client.read();
  }
  client.stop();

  int bodyIndex = resp.indexOf("\r\n\r\n");
  if (bodyIndex >= 0) {
    bodyOut = resp.substring(bodyIndex + 4);
  } else {
    bodyOut = resp;
  }
  return true;
}

// Execute a single JSON job from JOB_FILE; returns true if a job was found & executed
static bool processSingleJobIfAny() {
  if (!initSdCard()) return false;
  if (!sd.exists(JOB_FILE)) return false;

  FsFile f = sd.open(JOB_FILE, O_RDONLY);
  if (!f) {
    Serial.println("[JOBS] Failed to open job file");
    return false;
  }

  String json;
  while (f.available()) {
    json += (char)f.read();
  }
  f.close();

  StaticJsonDocument<8192> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("[JOBS] JSON parse error: %s\n", err.c_str());
    return false;
  }

  const char* type = doc["type"] | "";
  String sensorIp = doc["sensor_ip"] | "";
  String sensorSn = doc["sensor_sn"] | "";
  if (sensorIp.length() == 0) {
    // fallback if only host is provided
    sensorIp = "192.168.4.2";
  }

  if (strlen(type) == 0) {
    Serial.println("[JOBS] Missing 'type' in job");
    return false;
  }

  // Common epoch in ms (like python)
  unsigned long epoch_ms = millis();  // locally we don't have RTC epoch necessarily

  if (strcmp(type, "STATUS") == 0) {
    String url = String("http://") + sensorIp + "/api?command=STATUS&datetime=" + String(epoch_ms) + "&";
    String body;
    if (!doSimpleHttpGet(url, body)) {
      Serial.println("[JOBS] STATUS command failed");
      return true;  // job existed, we tried
    }
    Serial.printf("[JOBS] STATUS response body:\n%s\n", body.c_str());
  } else if (strcmp(type, "CONFIGURE") == 0) {
    String url = String("http://") + sensorIp + "/api?command=CONFIGURE&datetime=" + String(epoch_ms) + "&";

    JsonObject params = doc["params"].as<JsonObject>();
    for (JsonPair kv : params) {
      const char* k = kv.key().c_str();
      String v = kv.value().as<String>();
      url += String(k) + "=" + v + "&";
    }
    String body;
    if (!doSimpleHttpGet(url, body)) {
      Serial.println("[JOBS] CONFIGURE command failed");
      return true;
    }
    Serial.printf("[JOBS] CONFIGURE response body:\n%s\n", body.c_str());
  } else if (strcmp(type, "FIRMWARE_UPDATE") == 0) {
    String hexPath = doc["hex_path"] | "/firmware/vibration_sensor_app_v1.17.hex";

    FirmwareJob job;
    job.sensorIp = sensorIp;
    job.sensorSN = sensorSn;
    job.hexPath = hexPath;
    job.maxLines = doc["max_lines"] | 0;                             // 0 => all
    job.totalTimeoutMs = doc["timeout_ms"] | (8UL * 60UL * 1000UL);  // default 8 minutes

    bool ok = executeFirmwareJob(job);
    Serial.printf("[JOBS] Firmware update job finished -> %s\n", ok ? "OK" : "FAIL");
  } else {
    Serial.printf("[JOBS] Unknown job type: %s\n", type);
  }

  // rename job so that it doesn't run again
  String doneName = String(JOB_FILE) + ".done";
  if (sd.exists(doneName.c_str())) {
    sd.remove(doneName.c_str());
  }
  sd.rename(JOB_FILE, doneName.c_str());
  return true;
}

// =============================
// Time Initialization
// =============================
void initializeTime() {
  time_t persisted = restoreRtcTime();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint32_t slept = rtc_last_sleep_duration_s;

  if (persisted > 1600000000UL) {
    time_t estimate = persisted;
    if (cause == ESP_SLEEP_WAKEUP_TIMER && slept > 0 && slept < 24 * 3600UL) {
      estimate += slept;
      Serial.printf("[TIME] Woke from TIMER. persisted=%lu, slept=%u s -> estimate=%lu\n",
                    (unsigned long)persisted, slept, (unsigned long)estimate);
    } else {
      Serial.printf("[TIME] Woke (cause=%d). Using persisted=%lu as estimate\n",
                    (int)cause, (unsigned long)persisted);
    }
    struct timeval tv;
    tv.tv_sec = estimate;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    needToSyncTime = true;
  } else {
    needToSyncTime = true;
    Serial.println("[TIME] No persisted time available; will require sync");
  }
  rtc_last_sleep_duration_s = 0;
}

// =============================
// Operational Mode
// =============================
void startOperationalMode() {
  WiFi.mode(WIFI_OFF);
  delay(200);
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);
  Serial.printf("[BOOT] Wake cause=%d, rtc_last_sleep_duration_s=%u\n",
                (int)esp_sleep_get_wakeup_cause(), rtc_last_sleep_duration_s);

  initializeTime();
  debugPrintTime("After initializeTime()");

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    currentState = rtc_next_state;
    Serial.printf("[SCHEDULER] Waking up for pre-scheduled state: %s\n",
                  (currentState == STATE_UPLINK ? "UPLINK" : "AP"));
  } else {
    currentState = STATE_INITIAL;
  }
  Serial.println("[OPMODE] Started.");
}

// =============================
// Deep Sleep & Scheduler
// =============================
void stopAPMode() {
  if (apActive) {
    if (stationConnectedEventId) {
      WiFi.removeEvent(stationConnectedEventId);
      stationConnectedEventId = 0;
    }
    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    apActive = false;
    Serial.println("[AP] Stopped.");
  }
}

void goToDeepSleep(unsigned int seconds) {
  if (seconds < 2) seconds = 2;
  setStatusLed(STATUS_SLEEPING);
  time_t now;
  time(&now);
  debugPrintTime("Before deep sleep");
  struct tm* timeinfo = localtime(&now);
  if (timeinfo->tm_year > (2023 - 1900)) {
    rtc_last_known_time = now;
    persistRtcTime(now);
  }
  rtc_last_sleep_duration_s = seconds;
  stopAPMode();
  
  // Stop BLE before deep sleep
  if (config.bleBeaconEnabled) {
    bleBeacon.stop();
    Serial.println("[BLE-MESH] Stopped BLE beacon before sleep");
  }
  
  Serial.printf("[SLEEP] Entering deep sleep for %u seconds.\n", seconds);
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  delay(200);
  esp_deep_sleep_start();
}

void printSchedulerInfo(time_t now) {
  uint32_t uplink_interval_s = config.uplinkIntervalMin * 60;
  uint32_t time_to_next_uplink = uplink_interval_s - (now % uplink_interval_s);
  Serial.printf("[SCHEDULER] Next UPLINK in: %u sec.\n", time_to_next_uplink);
  if (config.role == ROLE_COLLECTOR) {
    uint32_t time_to_next_ap = config.collectorApCycleSec - (now % config.collectorApCycleSec);
    Serial.printf("[SCHEDULER] Next AP in:   %u sec.\n", time_to_next_ap);
  }
}

void decideAndGoToSleep() {
  time_t now;
  time(&now);
  struct tm* timeinfo = localtime(&now);

  if (timeinfo->tm_year < (2023 - 1900)) {
    Serial.println("[SCHEDULER] Time not set, will try to sync uplink soon.");
    rtc_next_state = STATE_UPLINK;
    goToDeepSleep(30);
    return;
  }

  Serial.println("[SCHEDULER] Deciding next action before sleeping...");
  printSchedulerInfo(now);

  uint32_t uplink_interval_s = config.uplinkIntervalMin * 60;
  uint32_t time_to_next_uplink = uplink_interval_s - (now % uplink_interval_s);
  uint32_t sleep_for;

  if (config.role == ROLE_COLLECTOR) {
    uint32_t time_to_next_ap = config.collectorApCycleSec - (now % config.collectorApCycleSec);

    if (time_to_next_uplink <= time_to_next_ap) {
      // Next window is UPLINK
      rtc_next_state = STATE_UPLINK;
      sleep_for = time_to_next_uplink;

      // If via repeater, wake +30s after the uplink boundary so repeater is ON
      if (config.uplinkRoute == UPLINK_VIA_REPEATER) {
        sleep_for += 30;
      }

      // open slightly earlier (20s) for safety (unless small)
      if (sleep_for > 20) {
        sleep_for -= 20;
        Serial.printf("[SCHEDULER] Drift correction: starting uplink 20s earlier (sleep=%u)\n", sleep_for);
      } else {
        Serial.printf("[SCHEDULER] Drift correction skipped (sleep too short: %u)\n", sleep_for);
      }
    } else {
      // Next window is AP (sensor intake / jobs)
      rtc_next_state = STATE_COLLECTOR_AP;
      sleep_for = time_to_next_ap;
    }
  } else if (config.role == ROLE_REPEATER) {
    // REPEATER → stays awake with BLE beacon active
    // Don't go to deep sleep - allows instant wake-up by collectors
    // Note: Light sleep is managed by the Arduino/ESP-IDF framework automatically
    // when CPU is idle. BLE beacon continues advertising during light sleep.
    // Original scheduling logic removed: Repeater no longer uses scheduled uplink windows,
    // instead stays continuously available for collectors to connect at any time.
    Serial.println("[SCHEDULER] Repeater stays active with BLE beacon (automatic light sleep)");
    return; // Don't call goToDeepSleep
  } else {
    // ROOT → always on, should never reach here
    Serial.println("[SCHEDULER] Root should always be active");
    return;
  }

  goToDeepSleep(sleep_for);
}

// =============================
// Main Loop
// =============================
void loopOperationalMode() {
  esp_task_wdt_reset();

  // ROOT
  if (config.role == ROLE_ROOT) {
    ensureWiFiAPRoot();
    ensureRootHttpServer();
    
    // Root doesn't need BLE - always on and accessible via WiFi
    
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 10000) {
      debugPrintTime("Root loop");
      lastPrint = 0;
    }
    return;
  }

  // REPEATER
  if (config.role == ROLE_REPEATER) {
    // Initialize BLE beacon with wake-up callback (only once)
    if (config.bleBeaconEnabled && !bleBeacon.isActive()) {
      // Use actual AP SSID (same logic as ensureWiFiAPRepeater)
      String actualAPSSID = config.apSSID.length() ? config.apSSID : String("Repeater_AP");
      bleBeacon.begin(actualAPSSID, config.nodeName, 0, &repeaterWakeupCb); // 0 = Repeater role, with callback
      bleBeacon.startAdvertising();
      Serial.println("[BLE-MESH] Repeater BLE beacon active (WiFi AP OFF by default)");
    }
    
    // Sync time once at startup (before WiFi AP is turned off)
    static bool timeSynced = false;
    if (!timeSynced) {
      syncTimeFromUplink(5000);
      timeSynced = true;
    }
    
    // Check if WiFi AP should be stopped after transfer completion
    // WiFi AP is started by wake-up callback and should stop when no clients connected
    if (repeaterWiFiAPActive) {
      int numClients = WiFi.softAPgetStationNum();
      unsigned long apRunTime = millis() - repeaterAPStartTime;
      
      // Stop AP if no clients for timeout OR if running for max time
      if (numClients == 0 && apRunTime > REPEATER_NO_CLIENT_TIMEOUT_MS) {
        Serial.println("[REPEATER] No clients connected, stopping WiFi AP");
        stopRepeaterWiFiAP();
      } else if (apRunTime > REPEATER_MAX_AP_TIME_MS) {
        Serial.printf("[REPEATER] Max AP time exceeded (%lu ms), stopping WiFi AP\n", apRunTime);
        if (numClients > 0) {
          Serial.printf("[REPEATER] Warning: Force stopping with %d client(s) still connected\n", numClients);
        }
        stopRepeaterWiFiAP();
      }
    }
    
    // Repeater stays in light sleep with BLE beacon active when WiFi AP is off
    // No deep sleep - allows instant wake-up when collector sends BLE signal
    return;
  }

  // NON-ROOT STATE MACHINE
  switch (currentState) {

    // ============================================
    // STATE_INITIAL
    // ============================================
    case STATE_INITIAL:
      {
        decideAndGoToSleep();
        break;
      }

    // ============================================
    // STATE_COLLECTOR_AP  (SENSORS CONNECT HERE)
    // ============================================
    case STATE_COLLECTOR_AP:
      {
        static bool jobProcessedThisWindow = false;

        if (!apActive) {

          // --- SD init ---
          if (!initSdCard()) {
            Serial.println("[SD] (Re)Initializing SD card failed before AP start.");
          } else {
            Serial.println("[SD] Card initialized successfully.");
          }
          
          // NOTE: SD writer task removed - causes mutex crashes from AsyncWebServer callbacks
          // All SD operations now happen in main loop context

          // Reset job cache for new AP session
          sjm_resetJobCache();

          setStatusLed(STATUS_WIFI_ACTIVITY);
          Serial.println("[STATE] Executing: COLLECTOR AP");

          WiFi.mode(WIFI_AP_STA);
          WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                            IPAddress(192, 168, 4, 1),
                            IPAddress(255, 255, 255, 0));

          bool ap_ok = WiFi.softAP(config.sensorAP_SSID.c_str(), SENSOR_AP_PASSWORD);
          if (ap_ok) {
            Serial.printf("[AP] SoftAP started. SSID=%s | IP=%s\n",
                          config.sensorAP_SSID.c_str(), WiFi.softAPIP().toString().c_str());
          } else {
            Serial.println("[AP] SoftAP failed to start!");
          }

          // --- STATION EVENTS ---
          // Track when stations connect to update activity timeout
          stationConnectedEventId =
            WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
              if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {

                hadStation = true;
                lastActivityMillis = millis();

                char macStr[20];
                sprintf(macStr, "%02x:%02x:%02x:%02x:%02x:%02x",
                        info.wifi_ap_staconnected.mac[0],
                        info.wifi_ap_staconnected.mac[1],
                        info.wifi_ap_staconnected.mac[2],
                        info.wifi_ap_staconnected.mac[3],
                        info.wifi_ap_staconnected.mac[4],
                        info.wifi_ap_staconnected.mac[5]);

                Serial.printf("[AP] Station connected: %s (waiting for heartbeat POST)\n", macStr);
              }

              else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
                Serial.println("[AP] Station disconnected.");
              }
            });
          apActive = true;
          hadStation = false;
          jobProcessedThisWindow = false;
          lastActivityMillis = millis();
          lastHeartbeatMillis = 0; // Reset heartbeat tracking for new session

          // ======================================================
          //                HEARTBEAT INTEGRATION
          // ======================================================

          // Sensor HTTP server on port 3000 (your actual port)


          // ======================================================
          //                HEARTBEAT INTEGRATION
          // ======================================================

          // ΧΡΗΣΙΜΟΠΟΙΟΥΜΕ ΤΟ GLOBAL sensorServer
          heartbeatManager.begin(sensorServer);

          // -------- STATUS (heartbeats_after_measurement = 1)
          // When sensor sends a heartbeat with value=1, send STATUS command to get full status
          heartbeatManager.onStatus([](const SensorHeartbeatContext& ctx) {
            // Buffer for main loop processing (safe - no FreeRTOS)
            Serial.printf("[HB] STATUS heartbeat received for SN=%s IP=%s\n",
                          ctx.sensorSn.c_str(),
                          ctx.lastIp.toString().c_str());
            
            bufferHeartbeat(ctx.sensorSn, ctx.lastIp.toString(), false);
            lastActivityMillis = millis();
          });

          // -------- OTHER (Config / Firmware) - heartbeats_after_measurement > 1
          // When sensor sends heartbeat with value>1, check for and execute jobs
          heartbeatManager.onOther([](const SensorHeartbeatContext& ctx) {
            // Buffer for main loop processing with job check (safe - no FreeRTOS)
            Serial.printf("[HB] OTHER heartbeat received for SN=%s IP=%s\n",
                          ctx.sensorSn.c_str(),
                          ctx.lastIp.toString().c_str());
            
            bufferHeartbeat(ctx.sensorSn, ctx.lastIp.toString(), true);
            lastActivityMillis = millis();
          });

          // ======================================================
          //       LEGACY COMPATIBILITY ENDPOINTS
          // ======================================================
          // Support for sensors using old API format
          
          // Legacy GET /api/heartbeat?sensor_sn=XXX&ip=YYY
          // Buffer heartbeat for main loop processing
          sensorServer.on("/api/heartbeat", HTTP_GET, [](AsyncWebServerRequest *request) {
            if (!request->hasParam("sensor_sn")) {
              request->send(400, "text/plain", "Missing sensor_sn");
              return;
            }
            
            String sensorSn = request->getParam("sensor_sn")->value();
            IPAddress remoteIp = request->client()->remoteIP();
            
            // Log to Serial and buffer for main loop processing
            Serial.printf("[HB-LEGACY] GET /api/heartbeat from SN=%s IP=%s\n", 
                         sensorSn.c_str(), remoteIp.toString().c_str());
            
            // Buffer with job check enabled - main loop will process
            bufferHeartbeat(sensorSn, remoteIp.toString(), true);
            
            request->send(200, "text/plain", "OK");
            lastActivityMillis = millis();
          });

          // Legacy POST /api/status - sensor sends status data directly
          // Buffer status data for main loop processing
          sensorServer.on(
            "/api/status",
            HTTP_POST,
            [](AsyncWebServerRequest *request) {
              request->send(400, "text/plain", "Expected JSON body");
            },
            nullptr,
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              if (index != 0 && index + len != total) return;
              
              DynamicJsonDocument doc(1024);
              DeserializationError err = deserializeJson(doc, data, len);
              if (err) {
                request->send(400, "text/plain", "Invalid JSON");
                return;
              }
              
              // Extract S/N from the data field
              String dataStr = doc["data"] | "";
              String sensorSn = "";
              
              // Parse S/N from format: "MODE=...,S/N=25000120,..."
              int snPos = dataStr.indexOf("S/N=");
              if (snPos >= 0) {
                int commaPos = dataStr.indexOf(',', snPos);
                if (commaPos >= 0) {
                  sensorSn = dataStr.substring(snPos + 4, commaPos);
                } else {
                  sensorSn = dataStr.substring(snPos + 4);
                }
                sensorSn.trim();
              }
              
              IPAddress remoteIp = request->client()->remoteIP();
              
              // Log to Serial
              Serial.printf("[HB-LEGACY] POST /api/status from SN=%s IP=%s (%d bytes)\n", 
                           sensorSn.c_str(), remoteIp.toString().c_str(), (int)len);
              
              // Buffer heartbeat with status data - main loop will save to SD
              bufferHeartbeat(sensorSn, remoteIp.toString(), false, data, len);
              
              request->send(200, "text/plain", "OK");
              lastActivityMillis = millis();
            }
          );

          // Legacy POST /api/measure - sensor sends measurement data
          sensorServer.on(
            "/api/measure",
            HTTP_POST,
            [](AsyncWebServerRequest *request) {
              // This is called AFTER all body chunks are received
              IPAddress remoteIp = request->client()->remoteIP();
              Serial.printf("[HB-LEGACY] POST /api/measure completed from IP=%s\n", 
                           remoteIp.toString().c_str());
              request->send(200, "text/plain", "OK");
              lastActivityMillis = millis();
            },
            nullptr,
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
              // This is called for each chunk of data received
              // Update activity timestamp on EVERY chunk to prevent timeout during long uploads
              lastActivityMillis = millis();
              
              if (index == 0) {
                // First chunk - log start with sensor info
                IPAddress remoteIp = request->client()->remoteIP();
                Serial.printf("[HB-LEGACY] POST /api/measure started from IP=%s (total=%d bytes)\n", 
                             remoteIp.toString().c_str(), total);
              }
              
              // Just receive the data - don't send response here
              // The response is sent in the main handler above after all chunks are received
            }
          );

          sensorServer.begin();
          Serial.println("[HB] Heartbeat server started on :3000");

          // ======================================================
        }

        // ---- ASYNC HEARTBEAT-BASED PROCESSING ----
        // Sensors send POST requests to /event/heartbeat (port 3000)
        // The heartbeat callbacks (onStatus/onOther) handle job execution asynchronously
        // No active polling needed - event-driven architecture

        // ---- TIMEOUT CHECK ----
        // ---- PROCESS BUFFERED HEARTBEATS (SD writes and job execution) ----
        // This runs in main loop context where SD and job operations are safe
        processHeartbeatBuffer();

        // ---- TIMEOUT CHECK ----
        // Check for any sensor activity (heartbeats OR data transfers) periodically
        // Use the configured collectorDataTimeoutSec when sensors are connected
        // Only check periodically to avoid race conditions with ongoing transfers
        static unsigned long lastTimeoutCheck = 0;
        int numConnected = WiFi.softAPgetStationNum();
        const unsigned long ACTIVITY_CHECK_INTERVAL = 10000; // Check every 10 seconds
        
        unsigned long now = millis();
        if (now - lastTimeoutCheck >= ACTIVITY_CHECK_INTERVAL) {
          lastTimeoutCheck = now;
          
          unsigned long timeSinceLastActivity = now - lastActivityMillis;
          unsigned long timeout;
          
          if (numConnected > 0) {
            // Sensors connected - use data timeout (allows time for measurement uploads)
            timeout = config.collectorDataTimeoutSec * 1000UL;
            
            if (timeSinceLastActivity > timeout) {
              Serial.printf("[AP] %d sensor(s) connected but no activity for %lu sec, entering sleep.\n",
                           numConnected, timeSinceLastActivity / 1000);
              Serial.println("[AP] Inactivity timeout reached.");
              stopAPMode();
              decideAndGoToSleep();
              break;
            }
          } else {
            // No sensors connected - use shorter window timeout
            timeout = hadStation ? (config.collectorDataTimeoutSec * 1000UL) : (config.collectorApWindowSec * 1000UL);
            
            if (timeSinceLastActivity > timeout) {
              if (hadStation)
                Serial.println("[AP] Inactivity timeout reached.");
              else
                Serial.println("[AP] Window finished (no station).");

              stopAPMode();
              decideAndGoToSleep();
              break;
            }
          }
        }

        break;
      }



    // ============================================
    // STATE_UPLINK (UPLINK WINDOW)
    // ============================================
    case STATE_UPLINK:
      {
        static time_t state_start_time = 0;
        static bool started = false;
        static bool bleScanned = false;

        if (!started) {
          setStatusLed(STATUS_SENDING_DATA);
          Serial.printf("[STATE] Executing: UPLINK APPOINTMENT (%s)\n",
                        (config.role == ROLE_REPEATER ? "REPEATER" : "COLLECTOR"));

          // BLE Scan for parent discovery (Collector/Repeater finding their parent)
          if (config.bleBeaconEnabled && !bleScanned) {
            Serial.println("[BLE-MESH] Scanning for parent node...");
            String scannerName = config.nodeName + "_Scanner";
            bleScanner.begin(scannerName);
            BLEScannerManager::ScanResult result = bleScanner.scanForParent(config.bleScanDurationSec);
            
            if (result.found) {
              Serial.printf("[BLE-MESH] Found parent SSID: %s (Role: %s, RSSI: %d dBm)\n",
                           result.apSSID.c_str(),
                           result.nodeRole == 1 ? "Root" : "Repeater",
                           result.rssi);
              
              // If parent is Repeater (role=0), send wake-up signal to start WiFi AP
              if (result.nodeRole == 0) {
                Serial.println("[BLE-WAKEUP] Parent is Repeater, sending wake-up signal...");
                if (bleScanner.sendWakeupSignal(result.address)) {
                  Serial.println("[BLE-WAKEUP] Wake-up signal sent successfully, waiting for WiFi AP...");
                  // Wait for WiFi AP to start (give Repeater time to bring up AP)
                  delay(REPEATER_AP_STARTUP_DELAY_MS);
                } else {
                  Serial.println("[BLE-WAKEUP] Failed to send wake-up signal");
                }
              }
              
              // Override uplinkSSID with discovered AP SSID from BLE
              config.uplinkSSID = result.apSSID;
              Serial.printf("[BLE-MESH] Using discovered AP SSID for WiFi: %s\n", config.uplinkSSID.c_str());
            } else {
              Serial.println("[BLE-MESH] No parent found via BLE, proceeding with configured uplink");
            }
            
            bleScanner.stop();
            bleScanned = true;
          }

          time(&state_start_time);
          started = true;

          time_t nowtmp;
          time(&nowtmp);
          if (nowtmp < 1700000000UL) {
            syncTimeFromUplink(6000);
          }
        }

        if (config.role == ROLE_COLLECTOR) {
          // Check elapsed time before starting upload
          time_t now;
          time(&now);
          int elapsed = now - state_start_time;
          unsigned long elapsedMs = (unsigned long)elapsed * 1000;
          
          // Calculate remaining time for upload operations
          unsigned long uplinkMaxWindowMs = config.uplinkMaxWindowSec * 1000UL;
          unsigned long remainingTimeMs = (elapsedMs < uplinkMaxWindowMs) ? 
                                          (uplinkMaxWindowMs - elapsedMs) : 0;
          
          if (remainingTimeMs < 10000) {
            Serial.printf("[UPLINK] Insufficient time remaining (%lu ms), skipping upload\n", remainingTimeMs);
            started = false;
            bleScanned = false;
            decideAndGoToSleep();
            return;
          }
          
          // Queue-based upload with timeout (use 80% of remaining time for uploads)
          unsigned long uploadTimeoutMs = (remainingTimeMs * 80) / 100;
          Serial.printf("[UPLINK] Starting upload with timeout: %lu ms\n", uploadTimeoutMs);
          uploadAllQueuedFiles(uploadTimeoutMs);
          
          // After uploading, sync jobs from parent (if time permits)
          time(&now);
          elapsed = now - state_start_time;
          elapsedMs = (unsigned long)elapsed * 1000;
          
          unsigned long uplinkMaxWindowMs = config.uplinkMaxWindowSec * 1000UL;
          if (elapsedMs < uplinkMaxWindowMs) {
            syncJobsFromRoot();
          } else {
            Serial.println("[UPLINK] Timeout reached, skipping job sync");
          }
          
          // Always exit after upload attempt (success or timeout)
          Serial.println("[UPLINK] Upload session complete → sleeping.");
          started = false;
          bleScanned = false; // Reset for next cycle
          decideAndGoToSleep();
          return;
        }

        // For REPEATER role (no upload operations)
        time_t now;
        time(&now);
        int elapsed = now - state_start_time;

        if (elapsed < config.uplinkWindowSec) {
          delay(50);
          return;
        }

        Serial.printf("[UPLINK] Window finished after %d sec.\n", elapsed);
        started = false;
        bleScanned = false; // Reset for next cycle
        decideAndGoToSleep();
        break;
      }
  }
}
