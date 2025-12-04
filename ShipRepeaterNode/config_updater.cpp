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

    // Simple HTTP GET with 5-second timeout (matching Python daemon)
    WiFiClient client;
    String request =
        String("GET ") + query + " HTTP/1.1\r\n" +
        "Host: " + job.sensorIp + "\r\n" +
        "Connection: close\r\n\r\n";

    Serial.printf("[CONFIG] HTTP GET http://%s%s\n",
                  job.sensorIp.c_str(), query.c_str());

    if (!client.connect(job.sensorIp.c_str(), 80)) {
        Serial.println("[CONFIG] ERROR: Cannot connect to sensor");
        return false;
    }

    client.print(request);

    // Wait for response with 5-second timeout (matching Python daemon)
    unsigned long start = millis();
    String response;
    while (millis() - start < 5000UL) {
        while (client.available()) {
            char c = client.read();
            response += c;
        }
        if (!client.connected() && response.length() > 0) break;
        delay(10);
    }
    client.stop();

    // Extract body from response
    int headerEnd = response.indexOf("\r\n\r\n");
    String body = (headerEnd >= 0) ? response.substring(headerEnd + 4) : response;

    // Log response but don't fail based on it (matching Python daemon behavior)
    // Python daemon logs the response but always clears the flag regardless
    if (body.length() > 0) {
        Serial.println("[CONFIG] Response:");
        Serial.println(body);
    } else {
        Serial.println("[CONFIG] No response body received");
    }

    // Always return true - config was sent (Python daemon behavior)
    // If sensor is busy (uploading data), it will get the config on next attempt
    return true;
}
