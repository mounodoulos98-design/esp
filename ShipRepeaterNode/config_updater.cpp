#include "config_updater.h"
#include "config.h"
#include "http_utils.h"
#include "logging.h"
#include "tuning.h"
#include <WiFi.h>
#include <WiFiClient.h>

extern RuntimeTuning g_tuning;

// Simple CONFIGURE HTTP GET (Diagnonic style)
// No JSON response, we only check if body contains "OK".
bool cu_sendConfiguration(const ConfigJob& job)
{
    LOG_INFO("CONFIG", "Starting configuration update for SN=%s IP=%s",
             job.sensorSn.c_str(), job.sensorIp.c_str());

    if (job.sensorIp.length() == 0) {
        LOG_ERROR("CONFIG", "Empty sensor IP");
        return false;
    }

    // Time stamp in ms, like python (epoch_ms). If we don't have real epoch, millis() is still monotonic.
    unsigned long epochMs = millis();
    String query = "/api?command=CONFIGURE&datetime=" + String(epochMs);

    // Append params from JSON with URL encoding
    for (JsonPair kv : job.params) {
        const char* key = kv.key().c_str();
        String value;
        
        // Handle both numeric and string values
        if (kv.value().is<int>() || kv.value().is<float>()) {
            value = kv.value().as<String>();
        } else {
            value = kv.value().as<String>();
        }
        
        // URL encode the value
        String encodedValue = urlEncode(value);
        query += "&" + String(key) + "=" + encodedValue;
    }

    // Wait before sending, mirroring python daemon's "wait_for_commands" behavior
    delay(g_tuning.configureDelayMs);

    // Use new HTTP utility
    String body;
    bool ok = httpGet(job.sensorIp, query, body, g_tuning.httpTimeoutMs, 
                     g_tuning.httpRetries, false);

    if (!ok) {
        LOG_ERROR("CONFIG", "HTTP request failed");
        return false;
    }

    LOG_DEBUG("CONFIG", "Response body: %s", body.c_str());

    if (body.indexOf("OK") >= 0) {
        LOG_INFO("CONFIG", "SUCCESS (OK found in response)");
        return true;
    }

    LOG_WARN("CONFIG", "FAILED (no OK in response)");
    return false;
}
