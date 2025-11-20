#include "status_utils.h"
#include <ArduinoJson.h>

/**
 * Parse STATUS response body as JSON: {"res":"OK","data":"KEY=VAL,..."}
 */
bool parseStatusResponse(const String& responseBody, StatusData& statusData) {
    // Initialize output structure with defaults
    statusData.serialNumber = "";
    statusData.firmwareVersion = "";
    statusData.rssi = 0;
    statusData.rssiPercent = 0;
    statusData.ssid = "";
    statusData.macAddress = "";
    statusData.localIp = "";
    statusData.batteryVoltage = 0.0;
    statusData.batteryPercent = 0;

    if (responseBody.length() == 0) {
        Serial.println("[STATUS_UTILS] ERROR: Empty response body");
        return false;
    }

    // Parse JSON response
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, responseBody);
    
    if (error) {
        Serial.printf("[STATUS_UTILS] ERROR: JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Validate JSON structure
    if (!doc.containsKey("res") || !doc.containsKey("data")) {
        Serial.println("[STATUS_UTILS] ERROR: Invalid JSON structure (missing 'res' or 'data')");
        return false;
    }

    String res = doc["res"].as<String>();
    if (res != "OK") {
        Serial.printf("[STATUS_UTILS] ERROR: Response status is not OK: %s\n", res.c_str());
        return false;
    }

    String data = doc["data"].as<String>();
    if (data.length() == 0) {
        Serial.println("[STATUS_UTILS] ERROR: Empty data field");
        return false;
    }

    // Parse data field: "KEY=VAL,KEY=VAL,..."
    int pos = 0;
    while (true) {
        int comma = data.indexOf(',', pos);
        String part = (comma < 0) ? data.substring(pos) : data.substring(pos, comma);
        int eq = part.indexOf('=');
        
        if (eq > 0) {
            String key = part.substring(0, eq);
            String val = part.substring(eq + 1);
            key.trim();
            val.trim();

            // Extract relevant fields
            if (key == "S/N") {
                statusData.serialNumber = val;
            } else if (key == "FIRMWARE_VERSION") {
                statusData.firmwareVersion = val;
            } else if (key == "RSSI") {
                statusData.rssi = val.toInt();
                statusData.rssiPercent = convertRssiToPercent(statusData.rssi);
            } else if (key == "SSID") {
                statusData.ssid = val;
            } else if (key == "MAC_ADDRESS") {
                statusData.macAddress = val;
            } else if (key == "LOCAL_IP") {
                statusData.localIp = val;
            } else if (key == "BATTERY_VOLTAGE") {
                statusData.batteryVoltage = val.toFloat();
                statusData.batteryPercent = convertBatteryVoltageToPercent(statusData.batteryVoltage);
            }
        }
        
        if (comma < 0) break;
        pos = comma + 1;
    }

    // Validate that we got at least the S/N
    if (statusData.serialNumber.length() == 0) {
        Serial.println("[STATUS_UTILS] ERROR: S/N not found in data");
        return false;
    }

    Serial.printf("[STATUS_UTILS] Parsed: SN=%s, FW=%s, RSSI=%d(%d%%), SSID=%s, MAC=%s, IP=%s, BAT=%.2fV(%d%%)\n",
                  statusData.serialNumber.c_str(),
                  statusData.firmwareVersion.c_str(),
                  statusData.rssi,
                  statusData.rssiPercent,
                  statusData.ssid.c_str(),
                  statusData.macAddress.c_str(),
                  statusData.localIp.c_str(),
                  statusData.batteryVoltage,
                  statusData.batteryPercent);

    return true;
}

/**
 * Convert RSSI value (in dBm) to signal level percentage (0-100%)
 */
int convertRssiToPercent(int rssi) {
    const int RSSI_MIN = -100;
    const int RSSI_MAX = -50;
    
    if (rssi <= RSSI_MIN) {
        return 0;
    } else if (rssi >= RSSI_MAX) {
        return 100;
    } else {
        // Linear mapping from RSSI range to 0-100%
        int percent = (rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN);
        return percent;
    }
}

/**
 * Convert battery voltage to battery level percentage
 * Temperature/load-adjusted for Li-SOCl₂ batteries at ≈40°C and ≈60 mA CCV
 */
int convertBatteryVoltageToPercent(float voltage) {
    // Battery voltage mapping for Li-SOCl₂ batteries
    // Based on HAT Sensor 3.00V minimum workable voltage
    // Captures plateau-then-knee shape typical of Li-SOCl₂ batteries
    const float v1 = 3.00;  // 0%
    const float v2 = 3.26;  // 50%
    const float v3 = 3.30;  // 100%
    
    int percent;
    
    if (voltage <= v1) {
        percent = 0;
    } else if (voltage >= v3) {
        percent = 100;
    } else if (voltage <= v2) {
        // Linear interpolation between v1 and v2 (0% to 50%)
        percent = (int)((voltage - v1) * 50.0 / (v2 - v1));
    } else {
        // Linear interpolation between v2 and v3 (50% to 100%)
        percent = 50 + (int)((voltage - v2) * 50.0 / (v3 - v2));
    }
    
    // Clamp between 0 and 100
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    return percent;
}
