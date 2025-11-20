#include "status_utils.h"
#include <ArduinoJson.h>

// Convert RSSI dBm to percentage (0-100)
// Typical range: -90 dBm (0%) to -30 dBm (100%)
int rssiToPercent(int rssiDbm) {
    if (rssiDbm >= -30) return 100;
    if (rssiDbm <= -90) return 0;
    
    // Linear interpolation between -90 dBm and -30 dBm
    return (int)(((rssiDbm + 90) * 100.0) / 60.0);
}

// Parse STATUS response in JSON format: {"res":"OK","data":"KEY=VAL,..."}
// Returns true if parsing successful and S/N found, false otherwise
bool parseStatusResponse(const String& body, StatusInfo& info) {
    // Initialize struct with defaults
    info.sn = "";
    info.firmwareVersion = "";
    info.mode = "";
    info.rssi = -100;
    info.rssiPercent = 0;
    info.batteryLevel = -1;  // -1 indicates unavailable
    
    if (body.length() == 0) {
        Serial.println("[STATUS_UTILS] Empty body");
        return false;
    }
    
    // Try to parse as JSON
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, body);
    
    if (err) {
        Serial.printf("[STATUS_UTILS] JSON parse error: %s\n", err.c_str());
        Serial.printf("[STATUS_UTILS] Body: %s\n", body.c_str());
        return false;
    }
    
    // Check for "res" field
    const char* res = doc["res"];
    if (!res) {
        Serial.println("[STATUS_UTILS] Missing 'res' field in JSON");
        return false;
    }
    
    if (strcmp(res, "OK") != 0) {
        Serial.printf("[STATUS_UTILS] Response not OK: %s\n", res);
        return false;
    }
    
    // Get "data" field
    const char* data = doc["data"];
    if (!data) {
        Serial.println("[STATUS_UTILS] Missing 'data' field in JSON");
        return false;
    }
    
    // Parse data field: "KEY=VAL,KEY2=VAL2,..."
    String dataStr = String(data);
    int pos = 0;
    bool foundSN = false;
    
    while (true) {
        int comma = dataStr.indexOf(',', pos);
        String part = (comma < 0) ? dataStr.substring(pos) : dataStr.substring(pos, comma);
        int eq = part.indexOf('=');
        
        if (eq > 0) {
            String key = part.substring(0, eq);
            String val = part.substring(eq + 1);
            key.trim();
            val.trim();
            
            if (key == "S/N") {
                info.sn = val;
                foundSN = true;
            } else if (key == "FIRMWARE_VERSION") {
                info.firmwareVersion = val;
            } else if (key == "MODE") {
                info.mode = val;
            } else if (key == "RSSI") {
                info.rssi = val.toInt();
                info.rssiPercent = rssiToPercent(info.rssi);
            } else if (key == "BATTERY_LEVEL") {
                info.batteryLevel = val.toInt();
            }
        }
        
        if (comma < 0) break;
        pos = comma + 1;
    }
    
    if (!foundSN) {
        Serial.println("[STATUS_UTILS] S/N not found in data field");
        return false;
    }
    
    Serial.printf("[STATUS_UTILS] Parsed: SN=%s, FW=%s, MODE=%s, RSSI=%d dBm (%d%%)",
                  info.sn.c_str(), 
                  info.firmwareVersion.c_str(),
                  info.mode.c_str(),
                  info.rssi,
                  info.rssiPercent);
    
    if (info.batteryLevel >= 0) {
        Serial.printf(", Battery=%d%%", info.batteryLevel);
    }
    Serial.println();
    
    return true;
}
