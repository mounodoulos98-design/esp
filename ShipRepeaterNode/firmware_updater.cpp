#include "firmware_updater.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <vector>

// SD from elsewhere
extern SdFat sd;
extern bool initSdCard();

// ------------------------------------------
// Simple HTTP GET helper (no JSON parsing)
// ------------------------------------------
static bool httpGetSensor(const String& hostIp,
                          const String& pathAndQuery,
                          String& bodyOut,
                          unsigned long timeoutMs = 5000UL)
{
    WiFiClient client;

    String request =
        String("GET ") + pathAndQuery + " HTTP/1.1\r\n" +
        "Host: " + hostIp + "\r\n" +
        "Connection: close\r\n\r\n";

    Serial.printf("[FW] HTTP GET http://%s%s\n",
                  hostIp.c_str(), pathAndQuery.c_str());

    if (!client.connect(hostIp.c_str(), 80)) {
        Serial.println("[FW] ERROR: connect() failed");
        return false;
    }

    client.print(request);

    unsigned long start = millis();
    String response;

    while (millis() - start < timeoutMs) {
        while (client.available()) {
            char c = client.read();
            response += c;
        }
        if (!client.connected()) break;
        delay(1);
    }
    client.stop();

    int headerEnd = response.indexOf("\r\n\r\n");
    if (headerEnd >= 0) {
        bodyOut = response.substring(headerEnd + 4);
    } else {
        bodyOut = response;
    }

    if (bodyOut.length() == 0) {
        Serial.println("[FW] WARNING: empty body");
    }
    return true;
}

// ------------------------------------------
// Execute firmware job (Intel HEX over HTTP)
// ------------------------------------------
bool executeFirmwareJob(const FirmwareJob& job)
{
    Serial.printf("[FW] Starting firmware job for SN=%s IP=%s\n",
                  job.sensorSN.c_str(), job.sensorIp.c_str());

    if (job.sensorIp.length() == 0) {
        Serial.println("[FW] ERROR: empty sensor IP");
        return false;
    }

    if (!initSdCard()) {
        Serial.println("[FW] ERROR: SD init failed");
        return false;
    }

    if (!sd.exists(job.hexPath.c_str())) {
        Serial.printf("[FW] ERROR: hex file not found: %s\n",
                      job.hexPath.c_str());
        return false;
    }

    FsFile f = sd.open(job.hexPath.c_str(), O_RDONLY);
    if (!f) {
        Serial.printf("[FW] ERROR: cannot open hex file: %s\n",
                      job.hexPath.c_str());
        return false;
    }

    // Read file into lines
    std::vector<String> lines;
    String cur;
    while (f.available()) {
        char c = f.read();
        if (c == '\r') {
            continue;
        } else if (c == '\n') {
            if (cur.length() > 0) {
                lines.push_back(cur);
                cur = "";
            }
        } else {
            cur += c;
        }
    }
    if (cur.length() > 0) {
        lines.push_back(cur);
    }
    f.close();

    if (lines.empty()) {
        Serial.println("[FW] ERROR: hex file is empty");
        return false;
    }

    Serial.printf("[FW] Loaded %u hex lines from %s\n",
                  (unsigned)lines.size(), job.hexPath.c_str());

    uint32_t maxLines = job.maxLines;
    if (maxLines == 0 || maxLines > lines.size()) {
        maxLines = lines.size();
    }

    unsigned long totalTimeoutMs =
        (job.totalTimeoutMs > 0) ? job.totalTimeoutMs : (8UL * 60UL * 1000UL);

    unsigned long globalStart = millis();
    const int MAX_TRIES = 3;

    for (uint32_t i = 0; i < maxLines; ++i) {
        if (millis() - globalStart > totalTimeoutMs) {
            Serial.println("[FW] ERROR: global timeout reached");
            return false;
        }

        String line = lines[i];
        line.trim();
        if (line.length() == 0) continue;
        if (!line.startsWith(":")) {
            Serial.printf("[FW] WARNING: skipping invalid line %u: %s\n",
                          (unsigned)i, line.c_str());
            continue;
        }

        // Replace ':' with '.' per Diagnonic requirement
        String hexPayload = line;
        hexPayload.replace(":", ".");

        String path = "/api?command=FIRMWARE_UPDATE&hex=" + hexPayload + "&d=0";

        bool ok = false;
        for (int t = 0; t < MAX_TRIES; ++t) {
            String body;
            if (httpGetSensor(job.sensorIp, path, body, 5000UL)) {
                ok = true;
                break;
            }
            Serial.printf("[FW] Line %u: try %d failed, retrying...\n",
                          (unsigned)i, t + 1);
            delay(4000);
        }

        if (!ok) {
            Serial.printf("[FW] ERROR: giving up at line %u\n", (unsigned)i);
            return false;
        }

        // Small delay to avoid overwhelming the sensor
        delay(10);
        esp_task_wdt_reset();
    }

    Serial.println("[FW] Firmware update job completed successfully");
    return true;
}
