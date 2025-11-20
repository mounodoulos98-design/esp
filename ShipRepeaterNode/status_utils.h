#ifndef STATUS_UTILS_H
#define STATUS_UTILS_H

#include <Arduino.h>

/**
 * Structure to hold parsed STATUS response data
 */
struct StatusData {
    String serialNumber;       // S/N
    String firmwareVersion;    // FIRMWARE_VERSION
    int rssi;                  // RSSI (raw dBm value)
    int rssiPercent;          // RSSI converted to percentage (0-100)
    String ssid;              // SSID
    String macAddress;        // MAC_ADDRESS
    String localIp;           // LOCAL_IP
    float batteryVoltage;     // BATTERY_VOLTAGE
    int batteryPercent;       // Battery percentage (0-100), mapped from voltage
};

/**
 * Parse STATUS response body as JSON: {"res":"OK","data":"KEY=VAL,..."}
 * Extracts S/N, firmware version, RSSI, SSID, MAC, LOCAL_IP, battery voltage
 * 
 * @param responseBody The HTTP response body
 * @param statusData Output structure to fill with parsed data
 * @return true if parsing succeeded, false otherwise
 */
bool parseStatusResponse(const String& responseBody, StatusData& statusData);

/**
 * Convert RSSI value (in dBm) to signal level percentage (0-100%)
 * Typical RSSI values range from -100 dBm (weak) to -50 dBm (strong)
 * 
 * @param rssi RSSI value in dBm (typically -100 to -50)
 * @return Signal level percentage (0-100)
 */
int convertRssiToPercent(int rssi);

/**
 * Convert battery voltage to battery level percentage
 * Uses temperature/load-adjusted mapping for Li-SOCl₂ batteries:
 *   3.00V → 0%
 *   3.26V → 50%
 *   3.30V → 100%
 * 
 * @param voltage Battery voltage in volts
 * @return Battery level percentage (0-100)
 */
int convertBatteryVoltageToPercent(float voltage);

#endif // STATUS_UTILS_H
