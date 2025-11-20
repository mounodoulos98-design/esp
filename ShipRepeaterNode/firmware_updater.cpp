#include "firmware_updater.h"
#include "http_utils.h"
#include "logging.h"
#include "tuning.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <vector>

// SD from elsewhere
extern SdFat sd;
extern bool initSdCard();
extern RuntimeTuning g_tuning;

// Helper to validate response body for firmware line push
static bool validateFirmwareResponse(const String& body, bool requireOk) {
    if (!requireOk) {
        return true;  // Validation disabled
    }
    
    // Check for "OK" in response
    if (body.indexOf("OK") >= 0) {
        return true;
    }
    
    LOG_WARN("FW", "Response validation failed, no OK found in: %s", body.c_str());
    return false;
}

// ------------------------------------------
// Execute firmware job (Intel HEX over HTTP)
// ------------------------------------------
bool executeFirmwareJob(const FirmwareJob& job)
{
    LOG_INFO("FW", "Starting firmware job for SN=%s IP=%s", 
             job.sensorSN.c_str(), job.sensorIp.c_str());

    if (job.sensorIp.length() == 0) {
        LOG_ERROR("FW", "Empty sensor IP");
        return false;
    }

    if (!initSdCard()) {
        LOG_ERROR("FW", "SD init failed");
        return false;
    }

    if (!sd.exists(job.hexPath.c_str())) {
        LOG_ERROR("FW", "Hex file not found: %s", job.hexPath.c_str());
        return false;
    }

    FsFile f = sd.open(job.hexPath.c_str(), O_RDONLY);
    if (!f) {
        LOG_ERROR("FW", "Cannot open hex file: %s", job.hexPath.c_str());
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
        LOG_ERROR("FW", "Hex file is empty");
        return false;
    }

    LOG_INFO("FW", "Loaded %u hex lines from %s", (unsigned)lines.size(), job.hexPath.c_str());

    uint32_t maxLines = job.maxLines;
    if (maxLines == 0 || maxLines > lines.size()) {
        maxLines = lines.size();
    }

    unsigned long totalTimeoutMs =
        (job.totalTimeoutMs > 0) ? job.totalTimeoutMs : (8UL * 60UL * 1000UL);

    unsigned long globalStart = millis();
    unsigned long lineDelayMs = (job.lineRateLimitMs > 0) ? 
                                 job.lineRateLimitMs : 
                                 g_tuning.firmwareLineDelayMs;
    int progressInterval = g_tuning.fwProgressLogInterval;
    bool requireOk = g_tuning.fwRequireOkPerLine;

    unsigned long totalLineTime = 0;
    uint32_t successfulLines = 0;
    
    for (uint32_t i = 0; i < maxLines; ++i) {
        unsigned long lineStart = millis();
        unsigned long elapsed = millis() - globalStart;
        
        // Check global timeout
        if (elapsed > totalTimeoutMs) {
            LOG_ERROR("FW", "Global timeout reached at line %u/%u", i, maxLines);
            return false;
        }
        
        // Predictive timeout: check if we'll likely exceed timeout
        if (i > 0 && successfulLines > 0) {
            unsigned long avgTimePerLine = totalLineTime / successfulLines;
            unsigned long remaining = maxLines - i;
            unsigned long estimatedRemaining = avgTimePerLine * remaining;
            
            if (elapsed + estimatedRemaining > totalTimeoutMs) {
                LOG_WARN("FW", "Predicted timeout: %lu ms remaining, need ~%lu ms for %lu lines",
                         totalTimeoutMs - elapsed, estimatedRemaining, remaining);
            }
        }

        String line = lines[i];
        line.trim();
        if (line.length() == 0) continue;
        if (!line.startsWith(":")) {
            LOG_WARN("FW", "Skipping invalid line %u: %s", (unsigned)i, line.c_str());
            continue;
        }

        // Replace ':' with '.' per Diagnonic requirement
        String hexPayload = line;
        hexPayload.replace(":", ".");

        String path = "/api?command=FIRMWARE_UPDATE&hex=" + hexPayload + "&d=0";

        // Use new HTTP utility with retries
        String body;
        bool ok = httpGet(job.sensorIp, path, body, g_tuning.httpTimeoutMs, 
                         g_tuning.httpRetries, false);
        
        if (ok && requireOk) {
            ok = validateFirmwareResponse(body, true);
        }

        if (!ok) {
            LOG_ERROR("FW", "Failed at line %u after retries", (unsigned)i);
            return false;
        }

        successfulLines++;
        unsigned long lineDuration = millis() - lineStart;
        totalLineTime += lineDuration;
        
        // Progress logging
        if (progressInterval > 0 && (i + 1) % progressInterval == 0) {
            float percent = ((float)(i + 1) / (float)maxLines) * 100.0f;
            LOG_INFO("FW", "Progress: %u/%u (%.1f%%)", i + 1, maxLines, percent);
        }

        // Delay between lines
        if (lineDelayMs > 0) {
            delay(lineDelayMs);
        }
        esp_task_wdt_reset();
    }

    // Final statistics
    unsigned long totalDuration = millis() - globalStart;
    float linesPerSec = (float)successfulLines / ((float)totalDuration / 1000.0f);
    LOG_INFO("FW", "Firmware update completed: %u lines in %lu ms (%.2f lines/sec)",
             successfulLines, totalDuration, linesPerSec);
    
    return true;
}
