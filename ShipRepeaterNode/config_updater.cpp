#include "config_updater.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClient.h>

// Simple CONFIGURE HTTP GET (Diagnonic style)
// No JSON response, we only check if body contains "OK".
bool cu_sendConfiguration(const ConfigJob& job)
{
    Serial.printf("[CONFIG] Starting configuration update for SN=%s IP=%s\n",
                  job.sensorSn.c_str(), job.sensorIp.c_str());

    if (job.sensorIp.length() == 0) {
        Serial.println("[CONFIG] ERROR: empty sensor IP");
        return false;
    }

    // Time stamp in ms, like python (epoch_ms). If we don't have real epoch, millis() is still monotonic.
    unsigned long epochMs = millis();
    String query = "/api?command=CONFIGURE&datetime=" + String(epochMs) + "&";

    // Append params from JSON
    for (JsonPair kv : job.params) {
        const char* key = kv.key().c_str();
        String value = kv.value().as<String>();
        query += String(key) + "=" + value + "&";
    }

    // Try up to 2 times (like Python daemon retry logic)
    for (int attempt = 1; attempt <= 2; attempt++) {
        if (attempt > 1) {
            Serial.printf("[CONFIG] Retry attempt %d/2\n", attempt);
            delay(2000); // Wait before retry
        } else {
            // 2-second wait before first attempt, mirroring python daemon's "wait_for_commands" behavior
            delay(2000);
        }

        WiFiClient client;
        String request =
            String("GET ") + query + " HTTP/1.1\r\n" +
            "Host: " + job.sensorIp + "\r\n" +
            "Connection: close\r\n\r\n";

        Serial.printf("[CONFIG] HTTP GET http://%s%s\n",
                      job.sensorIp.c_str(), query.c_str());

        if (!client.connect(job.sensorIp.c_str(), 80)) {
            Serial.println("[CONFIG] ERROR: Cannot connect to sensor");
            if (attempt == 2) return false;
            continue; // Try again
        }

        client.print(request);

        // Wait longer for response (8 seconds like Python daemon uses for some commands)
        unsigned long start = millis();
        String response;
        while (millis() - start < 8000UL) {
            while (client.available()) {
                char c = client.read();
                response += c;
            }
            if (!client.connected() && response.length() > 0) break;
            delay(10); // Small delay to let more data arrive
        }
        client.stop();

        // Allow sensor time to process
        delay(500);

        int headerEnd = response.indexOf("\r\n\r\n");
        String body = (headerEnd >= 0) ? response.substring(headerEnd + 4) : response;

        Serial.println("[CONFIG] Response body:");
        Serial.println(body);

        if (body.length() > 0 && (body.indexOf("OK") >= 0 || body.indexOf("Success") >= 0)) {
            Serial.println("[CONFIG] SUCCESS (OK found in response)");
            return true;
        }
        
        if (attempt < 2) {
            Serial.println("[CONFIG] No valid response, will retry...");
        }
    }

    Serial.println("[CONFIG] FAILED after 2 attempts");
    return false;
}
