#include "station_job_manager.h"
#include "config.h"
#include "firmware_updater.h"
#include "config_updater.h"
#include "http_utils.h"
#include "logging.h"
#include "tuning.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>

extern SdFat sd;
extern bool initSdCard();
extern RuntimeTuning g_tuning;
extern SemaphoreHandle_t sdCardMutex;

// ---------------------
// Εσωτερική κατάσταση
// ---------------------
static std::vector<PendingStation> g_stations;

// JSON job files
static const char* FW_JOBS_PATH  = "/jobs/firmware_jobs.json";
static const char* CFG_JOBS_PATH = "/jobs/config_jobs.json";

// ---------------------
// Helper για SD + JSON with mutex protection
// ---------------------
static bool readJsonFile(const char* path, StaticJsonDocument<16384>& doc) {
    if (sdCardMutex && xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        if (!initSdCard()) {
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        if (!sd.exists(path)) {
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        
        // Check file size
        FsFile f = sd.open(path, O_RDONLY);
        if (!f) {
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        
        f.seekEnd(0);
        uint32_t fsize = f.curPosition();
        f.rewind();
        
        if (fsize > 14000) {
            LOG_WARN("JOBS", "JSON file %s is large (%lu bytes), may cause memory issues", 
                     path, (unsigned long)fsize);
        }

        DeserializationError err = deserializeJson(doc, f);
        f.close();
        xSemaphoreGive(sdCardMutex);
        
        if (err) {
            LOG_ERROR("JOBS", "JSON parse error in %s: %s", path, err.c_str());
            return false;
        }
        return true;
    }
    LOG_ERROR("JOBS", "Failed to acquire SD mutex for reading %s", path);
    return false;
}

static bool writeJsonFile(const char* path, StaticJsonDocument<16384>& doc) {
    if (sdCardMutex && xSemaphoreTake(sdCardMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        if (!initSdCard()) {
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        
        FsFile f = sd.open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (!f) {
            LOG_ERROR("JOBS", "Cannot open %s for writing", path);
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        if (serializeJson(doc, f) == 0) {
            LOG_ERROR("JOBS", "Failed to serialize JSON to %s", path);
            f.close();
            xSemaphoreGive(sdCardMutex);
            return false;
        }
        f.close();
        xSemaphoreGive(sdCardMutex);
        return true;
    }
    LOG_ERROR("JOBS", "Failed to acquire SD mutex for writing %s", path);
    return false;
}

// ---------------------
// STATUS request:
//   - GET /api?command=STATUS&datetime=<ms>
//   - Παίρνουμε S/N από το body
// ---------------------
static bool sjm_requestStatus(const String& ip, String& snOut) {
    if (ip.length() == 0 || ip == "0.0.0.0") return false;

    // Wait before STATUS request
    delay(g_tuning.statusDelayMs);

    unsigned long epochMs = millis();
    String path = "/api?command=STATUS&datetime=" + String(epochMs);

    LOG_DEBUG("STATUS", "Requesting status from %s", ip.c_str());

    // Use new HTTP utility
    String body;
    if (!httpGet(ip, path, body, g_tuning.httpTimeoutMs, g_tuning.httpRetries, true)) {
        LOG_ERROR("STATUS", "HTTP GET failed for %s", ip.c_str());
        return false;
    }

    if (body.length() == 0) {
        LOG_WARN("STATUS", "Empty body from %s", ip.c_str());
        return false;
    }

    // status body: MODE=...,FIRMWARE_VERSION=...,S/N=324269,...
    snOut = "";
    int pos = 0;
    while (true) {
        int comma = body.indexOf(',', pos);
        String part = (comma < 0) ? body.substring(pos) : body.substring(pos, comma);
        int eq = part.indexOf('=');
        if (eq > 0) {
            String key = part.substring(0, eq);
            String val = part.substring(eq + 1);
            key.trim();
            val.trim();
            if (key == "S/N") {
                snOut = val;
                break;
            }
        }
        if (comma < 0) break;
        pos = comma + 1;
    }

    if (snOut.length() == 0) {
        LOG_WARN("STATUS", "S/N not found in body from %s", ip.c_str());
        return false;
    }

    LOG_INFO("STATUS", "SN=%s for IP=%s", snOut.c_str(), ip.c_str());
    return true;
}

// ---------------------
// Βρίσκουμε jobs για συγκεκριμένο SN
// Προτεραιότητα: FW πρώτα, μετά CONFIG
// ---------------------
static bool processJobsForSN(const String& sn, const String& ip) {
    bool didSomething = false;

    // 1) Firmware jobs (προτεραιότητα)
    {
        StaticJsonDocument<16384> doc;
        if (readJsonFile(FW_JOBS_PATH, doc)) {
            JsonArray arr = doc["jobs"].as<JsonArray>();
            if (!arr.isNull()) {
                for (size_t i = 0; i < arr.size(); ++i) {
                    JsonObject jobObj = arr[i];
                    String jobSn = jobObj["sn"].as<String>();
                    
                    // Schema validation
                    if (jobSn.length() == 0) {
                        LOG_WARN("JOBS", "FW job missing 'sn', skipping");
                        continue;
                    }
                    if (!jobObj.containsKey("hex_path")) {
                        LOG_WARN("JOBS", "FW job for SN=%s missing 'hex_path', skipping", jobSn.c_str());
                        continue;
                    }
                    
                    if (jobSn == sn) {
                        FirmwareJob fw;
                        fw.sensorSN  = sn;
                        fw.sensorIp  = ip;
                        fw.hexPath   = jobObj["hex_path"] | String("/firmware/default.hex");
                        fw.maxLines  = jobObj["max_lines"] | 0;
                        fw.totalTimeoutMs = jobObj["timeout_ms"] | (8UL * 60UL * 1000UL);
                        fw.lineRateLimitMs = jobObj["line_rate_limit_ms"] | 0;

                        LOG_INFO("JOBS", "Found FW job for SN=%s", sn.c_str());
                        bool ok = executeFirmwareJob(fw);
                        LOG_INFO("JOBS", "FW job result for SN=%s -> %s", 
                                 sn.c_str(), ok ? "OK" : "FAIL");

                        arr.remove(i);
                        if (arr.size() == 0) {
                            sd.remove(FW_JOBS_PATH);
                        } else {
                            writeJsonFile(FW_JOBS_PATH, doc);
                        }
                        didSomething = true;

                        // Αν υπήρχε FW job, δεν κάνουμε CONFIG στο ίδιο window
                        return didSomething;
                    }
                }
            }
        }
    }

    // 2) Configuration jobs
    {
        StaticJsonDocument<16384> doc;
        if (readJsonFile(CFG_JOBS_PATH, doc)) {
            JsonArray arr = doc["jobs"].as<JsonArray>();
            if (!arr.isNull()) {
                for (size_t i = 0; i < arr.size(); ++i) {
                    JsonObject jobObj = arr[i];
                    String jobSn = jobObj["sn"].as<String>();
                    
                    if (jobSn.length() == 0) {
                        LOG_WARN("JOBS", "CONFIG job missing 'sn', skipping");
                        continue;
                    }
                    
                    if (jobSn == sn) {
                        JsonObject params = jobObj["params"].as<JsonObject>();

                        ConfigJob cfg;
                        cfg.sensorSn = sn;
                        cfg.sensorIp = ip;
                        cfg.params   = params;

                        LOG_INFO("JOBS", "Found CONFIG job for SN=%s", sn.c_str());
                        bool ok = cu_sendConfiguration(cfg);
                        LOG_INFO("JOBS", "CONFIG job result for SN=%s -> %s",
                                 sn.c_str(), ok ? "OK" : "FAIL");

                        arr.remove(i);
                        if (arr.size() == 0) {
                            sd.remove(CFG_JOBS_PATH);
                        } else {
                            writeJsonFile(CFG_JOBS_PATH, doc);
                        }
                        didSomething = true;
                        break;
                    }
                }
            }
        }
    }

    return didSomething;
}

// ---------------------
// IP resolution
// ---------------------
extern "C" {
#include "esp_wifi.h"
#include "tcpip_adapter.h"
}

static void updateStationIPs() {
    wifi_sta_list_t wifi_sta_list;
    tcpip_adapter_sta_list_t adapter_sta_list;
    memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
    memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));

    if (esp_wifi_ap_get_sta_list(&wifi_sta_list) != ESP_OK) {
        return;
    }
    if (tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list) != ESP_OK) {
        return;
    }

    for (auto& st : g_stations) {
        // Αν έχουμε ήδη κανονική IP (όχι 0.0.0.0), δεν χρειάζεται update
        if (st.ip.length() > 0 && st.ip != "0.0.0.0") continue;

        for (int j = 0; j < adapter_sta_list.num; ++j) {
            const wifi_sta_info_t& wi = wifi_sta_list.sta[j];
            const tcpip_adapter_sta_info_t& ai = adapter_sta_list.sta[j];

            char macStr[20];
            sprintf(macStr, "%02x:%02x:%02x:%02x:%02x:%02x",
                    wi.mac[0], wi.mac[1], wi.mac[2],
                    wi.mac[3], wi.mac[4], wi.mac[5]);

            if (!st.mac.equalsIgnoreCase(String(macStr))) continue;

            uint32_t ipraw = ai.ip.addr;
            if (ipraw == 0) {
                // DHCP δεν έχει δώσει IP ακόμα, μην γράψεις 0.0.0.0
                continue;
            }

            IPAddress ipAddr(
                ipraw & 0xFF,
                (ipraw >> 8) & 0xFF,
                (ipraw >> 16) & 0xFF,
                (ipraw >> 24) & 0xFF
            );
            String ipStr = ipAddr.toString();

            st.ip = ipStr;
            Serial.printf("[SJM] MAC %s -> IP %s\n",
                          st.mac.c_str(), st.ip.c_str());
        }
    }
}

// ---------------------
// Δημόσιο API
// ---------------------
void sjm_init() {
    g_stations.clear();
    Serial.println("[SJM] init()");
}

void sjm_addStation(const String& mac) {
    // Αν υπάρχει ήδη station με την ίδια MAC, κάνε update, μην δημιουργείς διπλό
    for (auto& st : g_stations) {
        if (st.mac.equalsIgnoreCase(mac)) {
            st.connectedAtMillis = millis();
            st.ip = "";  // θα την ξαναβρούμε
            Serial.printf("[SJM] Station refreshed: %s\n", mac.c_str());
            return;
        }
    }

    PendingStation st;
    st.mac = mac;
    st.ip  = "";
    st.connectedAtMillis = millis();
    g_stations.push_back(st);
    Serial.printf("[SJM] New station added: %s\n", mac.c_str());
}

void sjm_processStations() {
    if (g_stations.empty()) return;

    // Προσπαθούμε να συμπληρώσουμε IPs από MAC list
    updateStationIPs();

    unsigned long now = millis();
    std::vector<String> toRemove;

    for (auto& st : g_stations) {
        // Περιμένουμε τουλάχιστον 2s μετά το connect
        if (now - st.connectedAtMillis < 2000UL) continue;

        // Αν δεν έχουμε έγκυρη IP, μην προσπαθήσεις STATUS
        if (st.ip.length() == 0 || st.ip == "0.0.0.0") continue;

        String sn;
        if (!sjm_requestStatus(st.ip, sn)) {
            Serial.printf("[SJM] STATUS failed for MAC=%s IP=%s\n",
                          st.mac.c_str(), st.ip.c_str());
            toRemove.push_back(st.mac);
            continue;
        }

        bool didJobs = processJobsForSN(sn, st.ip);
        if (didJobs) {
            Serial.printf("[SJM] Jobs processed for SN=%s (IP=%s)\n",
                          sn.c_str(), st.ip.c_str());
        } else {
            Serial.printf("[SJM] No jobs for SN=%s (IP=%s)\n",
                          sn.c_str(), st.ip.c_str());
        }

        // Μία φορά ανά σύνδεση
        toRemove.push_back(st.mac);
    }

    if (!toRemove.empty()) {
        g_stations.erase(
            std::remove_if(
                g_stations.begin(),
                g_stations.end(),
                [&](const PendingStation& st) {
                    for (auto& mac : toRemove) {
                        if (st.mac.equalsIgnoreCase(mac)) return true;
                    }
                    return false;
                }),
            g_stations.end()
        );
    }
}

// ---------------------
// Cleanup stale .done files
// ---------------------
void sjm_cleanupStaleDoneFiles() {
    if (!initSdCard()) return;
    
    const char* JOBS_DIR = "/jobs";
    if (!sd.exists(JOBS_DIR)) return;
    
    FsFile jobsDir = sd.open(JOBS_DIR, O_RDONLY);
    if (!jobsDir || !jobsDir.isDir()) {
        LOG_WARN("JOBS", "Cannot open /jobs directory");
        return;
    }
    
    unsigned long now = millis();
    unsigned long maxAge = g_tuning.jobCleanupAgeHours * 3600UL * 1000UL;
    int cleaned = 0;
    
    FsFile entry;
    while (entry.openNext(&jobsDir, O_RDONLY)) {
        char name[64];
        if (entry.getName(name, sizeof(name))) {
            String filename(name);
            if (filename.endsWith(".done")) {
                // Get file modification time
                uint16_t date, time;
                if (entry.getModifyDateTime(&date, &time)) {
                    // Note: This is a simplified check. For accurate age tracking,
                    // we'd need to convert FAT date/time to epoch and compare properly.
                    // For now, we just log and skip complex logic.
                    String fullPath = String(JOBS_DIR) + "/" + filename;
                    LOG_DEBUG("JOBS", "Found .done file: %s", fullPath.c_str());
                    
                    // Since we don't have accurate timestamp tracking,
                    // we'll just note this for future enhancement
                    // In production, you'd want to track creation time in a metadata file
                }
            }
        }
        entry.close();
    }
    jobsDir.close();
    
    if (cleaned > 0) {
        LOG_INFO("JOBS", "Cleaned up %d stale .done files", cleaned);
    }
}
