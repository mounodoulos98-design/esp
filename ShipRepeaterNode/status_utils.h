#pragma once

#include <Arduino.h>

// StatusInfo holds parsed data from sensor STATUS response
struct StatusInfo {
    String sn;               // Serial number (S/N)
    String firmwareVersion;  // Firmware version
    String mode;            // Operating mode
    int rssi;               // RSSI in dBm
    int rssiPercent;        // RSSI as percentage (0-100)
    int batteryLevel;       // Battery level estimate (0-100, -1 if unavailable)
};

// Parse STATUS response in JSON format: {"res":"OK","data":"KEY=VAL,..."}
// Returns true if parsing successful and S/N found, false otherwise
bool parseStatusResponse(const String& body, StatusInfo& info);

// Convert RSSI dBm to percentage (0-100)
// Typical range: -90 dBm (0%) to -30 dBm (100%)
int rssiToPercent(int rssiDbm);
