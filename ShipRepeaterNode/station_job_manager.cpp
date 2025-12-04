#include "station_job_manager.h"
#include "config.h"
#include "firmware_updater.h"
#include "config_updater.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>

extern SdFat sd;
extern bool initSdCard();

// ---------------------
// Εσωτερική κατάσταση
// ---------------------
static std::vector<PendingStation> g_stations;

// JSON job files
static const char* FW_JOBS_PATH  = "/jobs/firmware_jobs.json";
static const char* CFG_JOBS_PATH = "/jobs/config_jobs.json";

// Job cache to avoid re-reading JSON on every heartbeat
static StaticJsonDocument<16384> g_fwJobsCache;
static StaticJsonDocument<16384> g_cfgJobsCache;
static bool g_fwJobsCached = false;
static bool g_cfgJobsCached = false;
static unsigned long g_lastJobLoadTime = 0;

// ---------------------
// Helper για SD + JSON
// ---------------------
static bool readJsonFile(const char* path, StaticJsonDocument<16384>& doc) {
    if (!initSdCard()) return false;
    if (!sd.exists(path)) return false;
    FsFile f = sd.open(path, O_RDONLY);
    if (!f) return false;

    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[JOBS] JSON parse error in %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

static bool writeJsonFile(const char* path, StaticJsonDocument<16384>& doc) {
    if (!initSdCard()) return false;
    FsFile f = sd.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
        Serial.printf("[JOBS] Cannot open %s for writing\n", path);
        return false;
    }
    if (serializeJson(doc, f) == 0) {
        Serial.printf("[JOBS] Failed to serialize JSON to %s\n", path);
        f.close();
        return false;
    }
    f.close();
    return true;
}

// ---------------------
// STATUS request:
//   - GET /api?command=STATUS&datetime=<ms>&
//   - Παίρνουμε S/N από το body
// ---------------------
bool sjm_requestStatus(const String& ip, String& snOut) {
    if (ip.length() == 0 || ip == "0.0.0.0") return false;

    // Wait 2s πριν το STATUS, όπως ο daemon
    delay(2000);

    WiFiClient client;
    unsigned long epochMs = millis();
    String path = "/api?command=STATUS&datetime=" + String(epochMs) + "&";

    String request =
        String("GET ") + path + " HTTP/1.1\r\n" +
        "Host: " + ip + "\r\n" +
        "Connection: close\r\n\r\n";

    Serial.printf("[STATUS] HTTP GET http://%s%s\n", ip.c_str(), path.c_str());

    if (!client.connect(ip.c_str(), 80)) {
        Serial.println("[STATUS] ERROR: connect() failed");
        return false;
    }

    client.print(request);

    unsigned long start = millis();
    String response;
    while (millis() - start < 5000UL) {
        while (client.available()) {
            char c = client.read();
            response += c;
        }
        if (!client.connected()) break;
        delay(1);
    }
    client.stop();

    int headerEnd = response.indexOf("\r\n\r\n");
    String body = (headerEnd >= 0) ? response.substring(headerEnd + 4) : response;

    if (body.length() == 0) {
        Serial.println("[STATUS] WARNING: empty body");
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
        Serial.println("[STATUS] WARNING: S/N not found in body");
        return false;
    }

    Serial.printf("[STATUS] SN=%s for IP=%s\n", snOut.c_str(), ip.c_str());
    return true;
}

// ---------------------
// Helper: Get jobs array from JSON document
// Supports both {"jobs": [...]} and [...] formats
static JsonArray getJobsArray(JsonDocument& doc) {
    // Try {"jobs": [...]} format first
    JsonArray arr = doc["jobs"].as<JsonArray>();
    if (!arr.isNull()) {
        return arr;
    }
    // Try root array format [...]
    arr = doc.as<JsonArray>();
    return arr;
}

// Βρίσκουμε jobs για συγκεκριμένο SN
// Προτεραιότητα: FW πρώτα, μετά CONFIG
// ---------------------
bool processJobsForSN(const String& sn, const String& ip) {
    bool didSomething = false;

    // Load jobs into cache if not already loaded (only once per AP session)
    if (!g_fwJobsCached && g_lastJobLoadTime == 0) {
        g_fwJobsCached = readJsonFile(FW_JOBS_PATH, g_fwJobsCache);
        if (!g_fwJobsCached) {
            g_fwJobsCache.clear(); // No FW jobs
        }
    }
    if (!g_cfgJobsCached && g_lastJobLoadTime == 0) {
        g_cfgJobsCached = readJsonFile(CFG_JOBS_PATH, g_cfgJobsCache);
        if (!g_cfgJobsCached) {
            g_cfgJobsCache.clear(); // No config jobs
        }
    }
    // Mark that we've attempted to load jobs (set after both attempts)
    if (g_lastJobLoadTime == 0) {
        g_lastJobLoadTime = millis();
    }

    // 1) Firmware jobs (προτεραιότητα)
    if (g_fwJobsCached) {
        StaticJsonDocument<16384>& doc = g_fwJobsCache;
        JsonArray arr = getJobsArray(doc);
            if (!arr.isNull()) {
                for (size_t i = 0; i < arr.size(); ++i) {
                    JsonObject jobObj = arr[i];
                    String jobSn = jobObj["sn"].as<String>();
                    if (jobSn == sn) {
                        FirmwareJob fw;
                        fw.sensorSN  = sn;
                        fw.sensorIp  = ip;
                        fw.hexPath   = jobObj["hex_path"] | String("/firmware/default.hex");
                        fw.maxLines  = jobObj["max_lines"] | 0;
                        fw.totalTimeoutMs = jobObj["timeout_ms"] | (8UL * 60UL * 1000UL);

                        Serial.printf("[JOBS] Found FW job for SN=%s\n", sn.c_str());
                        bool ok = executeFirmwareJob(fw);
                        Serial.printf("[JOBS] FW job result for SN=%s -> %s\n",
                                      sn.c_str(), ok ? "OK" : "FAIL");

                        // Only remove job on success
                        if (ok) {
                            arr.remove(i);
                            if (arr.size() == 0) {
                                sd.remove(FW_JOBS_PATH);
                                g_fwJobsCached = false;
                                g_fwJobsCache.clear();
                            } else {
                                writeJsonFile(FW_JOBS_PATH, doc);
                            }
                        }
                        didSomething = ok;

                        // Αν υπήρχε FW job, δεν κάνουμε CONFIG στο ίδιο window
                        return didSomething;
                    }
                }
            }
        }

    // 2) Configuration jobs
    if (g_cfgJobsCached) {
        StaticJsonDocument<16384>& doc = g_cfgJobsCache;
        JsonArray arr = getJobsArray(doc);
            if (!arr.isNull()) {
                for (size_t i = 0; i < arr.size(); ++i) {
                    JsonObject jobObj = arr[i];
                    String jobSn = jobObj["sn"].as<String>();
                    if (jobSn == sn) {
                        JsonObject params = jobObj["params"].as<JsonObject>();

                        ConfigJob cfg;
                        cfg.sensorSn = sn;
                        cfg.sensorIp = ip;
                        cfg.params   = params;

                        Serial.printf("[JOBS] Found CONFIG job for SN=%s\n", sn.c_str());
                        bool ok = cu_sendConfiguration(cfg);
                        Serial.printf("[JOBS] CONFIG job result for SN=%s -> %s\n",
                                      sn.c_str(), ok ? "OK" : "FAIL");

                        // Only remove job on success
                        if (ok) {
                            arr.remove(i);
                            if (arr.size() == 0) {
                                sd.remove(CFG_JOBS_PATH);
                                g_cfgJobsCached = false;
                                g_cfgJobsCache.clear();
                            } else {
                                writeJsonFile(CFG_JOBS_PATH, doc);
                            }
                        }
                        didSomething = ok;
                        break;
                    }
                }
            }
        }

    return didSomething;
}

// Reset job cache (call when AP session starts)
void sjm_resetJobCache() {
    g_fwJobsCached = false;
    g_cfgJobsCached = false;
    g_fwJobsCache.clear();
    g_cfgJobsCache.clear();
    g_lastJobLoadTime = 0;
    Serial.println("[JOBS] Cache reset");
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
