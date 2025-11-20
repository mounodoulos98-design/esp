#ifndef SENSOR_HEARTBEAT_MANAGER_H
#define SENSOR_HEARTBEAT_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

struct SensorHeartbeatContext {
    String sensorSn;
    IPAddress lastIp;
    int heartbeatsAfterMeasurement = 1;  // simplified semantics
    String lastFirmwareVersion;          // optional
    unsigned long lastHeartbeatMillis = 0;  // timestamp of last heartbeat
    String lastAction = "";              // last action taken
};

class SensorHeartbeatManager {
public:
    using StatusCallback = std::function<void(const SensorHeartbeatContext&)>;
    using OtherCommandCallback = std::function<void(const SensorHeartbeatContext&)>;

    void begin(AsyncWebServer& server) {
        // /event/heartbeat όπως στο sensorsdaemon (POST με JSON)
        server.on(
            "/event/heartbeat",
            HTTP_POST,
            // onRequest handler – εδώ δεν έχουμε το body ακόμα,
            // απλά επιστρέφουμε 400 αν κάποιος δεν στείλει JSON body.
            [this](AsyncWebServerRequest *request) {
                request->send(
                    400,
                    "application/json",
                    "{\"success\":false,\"message\":\"Expected JSON body\"}"
                );
            },
            nullptr,
            // onBody handler – εδώ έρχεται το JSON από το sensor
            [this](AsyncWebServerRequest *request,
                   uint8_t *data,
                   size_t len,
                   size_t index,
                   size_t total) {
                this->handleHeartbeatBody(request, data, len, index, total);
            }
        );
    }

    void onStatus(StatusCallback cb) { statusCb = cb; }
    void onOther(OtherCommandCallback cb) { otherCb = cb; }
    
    // Purge sensor contexts older than timeout
    void purgeOldContexts(unsigned long timeoutMs) {
        unsigned long now = millis();
        auto it = sensors.begin();
        while (it != sensors.end()) {
            if (it->lastHeartbeatMillis > 0 && 
                (now - it->lastHeartbeatMillis) > timeoutMs) {
                it = sensors.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // Get count of active sensors
    size_t getSensorCount() const { return sensors.size(); }

private:
    std::vector<SensorHeartbeatContext> sensors;
    StatusCallback statusCb;
    OtherCommandCallback otherCb;

    SensorHeartbeatContext* findOrCreate(const String& sn, const IPAddress& ip) {
        for (auto &s : sensors) {
            if (s.sensorSn == sn) {
                s.lastIp = ip;
                s.lastHeartbeatMillis = millis();
                return &s;
            }
        }
        SensorHeartbeatContext ctx;
        ctx.sensorSn = sn;
        ctx.lastIp = ip;
        ctx.lastHeartbeatMillis = millis();
        sensors.push_back(ctx);
        return &sensors.back();
    }

    void handleHeartbeatBody(AsyncWebServerRequest *request,
                             uint8_t *data,
                             size_t len,
                             size_t index,
                             size_t total) {
        // Για απλότητα: θέλουμε όλο το JSON σε ένα chunk (όπως κάνει συνήθως ο sensor).
        // Αν έρθει σε πολλά chunks, αγνοούμε μέχρι να έρθει το τελευταίο.
        if (index != 0) {
            if (index + len != total) {
                // περιμένουμε το τελευταίο chunk
                return;
            }
        }

        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, data, len);
        if (err) {
            request->send(
                400,
                "application/json",
                "{\"success\":false,\"message\":\"invalid json\"}"
            );
            return;
        }

        String sensorSn = doc["sensor_sn"] | "";
        if (sensorSn.isEmpty()) {
            request->send(
                400,
                "application/json",
                "{\"success\":false,\"message\":\"missing sensor_sn\"}"
            );
            return;
        }

        IPAddress remoteIp = request->client()->remoteIP();
        SensorHeartbeatContext* ctx = findOrCreate(sensorSn, remoteIp);

        if (doc.containsKey("heartbeats_after_measurement")) {
            ctx->heartbeatsAfterMeasurement =
                doc["heartbeats_after_measurement"].as<int>();
        }
        
        // Support optional firmware version field
        if (doc.containsKey("firmware_version")) {
            ctx->lastFirmwareVersion = doc["firmware_version"].as<String>();
        }

        String action = "ignored";

        // Απλοποιημένη λογική sensorsdaemon:
        // - 1  → STATUS
        // - >1 → Other (CONFIGURE / FW κλπ)
        if (ctx->heartbeatsAfterMeasurement == 1) {
            action = "Status Command";
            ctx->lastAction = "STATUS";
            if (statusCb) {
                statusCb(*ctx);
            }
        } else if (ctx->heartbeatsAfterMeasurement > 1) {
            action = "Other Command";
            ctx->lastAction = "OTHER";
            if (otherCb) {
                otherCb(*ctx);
            }
        }

        DynamicJsonDocument resp(256);
        resp["success"] = true;
        resp["message"] = String("heartbeat processed, action: ") + action;

        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
    }
};

#endif // SENSOR_HEARTBEAT_MANAGER_H
