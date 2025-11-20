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

    // 2-second wait before sending, mirroring python daemon's "wait_for_commands" behavior
    delay(2000);

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

    Serial.println("[CONFIG] Response body:");
    Serial.println(body);

    if (body.indexOf("OK") >= 0) {
        Serial.println("[CONFIG] SUCCESS (OK found in response)");
        return true;
    }

    Serial.println("[CONFIG] FAILED (no OK in response)");
    return false;
}
