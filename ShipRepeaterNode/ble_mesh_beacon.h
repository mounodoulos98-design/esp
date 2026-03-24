#ifndef BLE_MESH_BEACON_H
#define BLE_MESH_BEACON_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "esp_bt.h"

// Advertising interval for the Repeater beacon.
// Units: 0.625 ms  →  800 × 0.625 ms = 500 ms.
// At 500 ms interval a 5-second scan catches ~10 beacons, giving reliable
// discovery while the BT radio duty cycle remains low during light sleep.
#define BLE_ADV_INTERVAL_UNITS 800   // 800 units × 0.625 ms = 500 ms

// BLE Service UUID for mesh node identification
#define BLE_MESH_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// Magic 2-byte "company ID" used as a mesh-node fingerprint in manufacturer
// data.  Lets the Collector find Repeater/Root beacons via a manufacturer-data
// check as a second, independent detection path alongside the service UUID.
// Manufacturer data byte layout (returned by getManufacturerData()):
//   [0] = BLE_MESH_COMPANY_ID_LO  ← magic low  byte
//   [1] = BLE_MESH_COMPANY_ID_HI  ← magic high byte
//   [2] = nodeRole  (0=Repeater, 1=Root)
//   [3..] = apSSID  (variable length, no null terminator)
#define BLE_MESH_COMPANY_ID_LO 0x53  // 'S'
#define BLE_MESH_COMPANY_ID_HI 0x4D  // 'M'

// BLE Beacon Manager for Repeaters/Root
// Advertises the node's presence so children can discover and wake it up
class BLEBeaconManager {
public:
    void begin(const String& apSSID, const String& nodeName, uint8_t nodeRole) {
        Serial.println("[BLE-BEACON] Initializing BLE Beacon...");
        
        // Initialize BLE
        try {
            BLEDevice::init(nodeName.c_str());
        } catch (...) {
            Serial.println("[BLE-BEACON] ERROR: Failed to initialize BLE");
            return;
        }

        // BLE modem sleep on ESP32-C6 with IDF 5.x is handled automatically
        // by the BLE stack; no explicit esp_bt_sleep_enable() call needed.
        
        // Create BLE Server (needed for advertising)
        pServer = BLEDevice::createServer();
        if (!pServer) {
            Serial.println("[BLE-BEACON] ERROR: Failed to create BLE server");
            BLEDevice::deinit(true);
            return;
        }
        
        // Get advertising object
        pAdvertising = BLEDevice::getAdvertising();
        if (!pAdvertising) {
            Serial.println("[BLE-BEACON] ERROR: Failed to get advertising object");
            BLEDevice::deinit(true);
            return;
        }
        
        // ── Advertising interval ─────────────────────────────────────────────
        pAdvertising->setMinInterval(BLE_ADV_INTERVAL_UNITS);
        pAdvertising->setMaxInterval(BLE_ADV_INTERVAL_UNITS + 16); // small jitter
        pAdvertising->setScanResponse(true);

        // ── Primary advertisement: service UUID only ─────────────────────────
        // Keeping the primary advertisement small (flags + 128-bit UUID = ~21
        // bytes) ensures the UUID is never crowded out and is always parsed
        // correctly by the remote scanner.
        BLEAdvertisementData advData;
        advData.setCompleteServices(BLEUUID(BLE_MESH_SERVICE_UUID));
        pAdvertising->setAdvertisementData(advData);

        // ── Scan response: device name + manufacturer data ───────────────────
        // The Collector uses active scan, so it receives the scan response too.
        // Manufacturer data layout: [company_lo][company_hi][role][ssid_bytes]
        //   • company_lo / company_hi = magic mesh fingerprint (0x53 0x4D)
        //   • role  0=Repeater, 1=Root
        //   • ssid  AP SSID bytes (no null terminator)
        BLEAdvertisementData scanRsp;
        scanRsp.setName(nodeName.c_str());
        String mfgData;
        mfgData += (char)BLE_MESH_COMPANY_ID_LO;
        mfgData += (char)BLE_MESH_COMPANY_ID_HI;
        mfgData += (char)nodeRole;
        mfgData += apSSID;
        scanRsp.setManufacturerData(mfgData);
        pAdvertising->setScanResponseData(scanRsp);

        isInitialized = true;
        Serial.printf("[BLE-BEACON] Initialized: AP SSID=%s, adv interval=%ums\n",
                      apSSID.c_str(),
                      (unsigned)(BLE_ADV_INTERVAL_UNITS * 625 / 1000));
    }

    void startAdvertising() {
        if (!isInitialized) {
            Serial.println("[BLE-BEACON] Error: Not initialized");
            return;
        }
        if (!isAdvertising) {
            BLEDevice::startAdvertising();
            isAdvertising = true;
            Serial.println("[BLE-BEACON] Started advertising");
        }
    }

    void stopAdvertising() {
        if (isAdvertising) {
            pAdvertising->stop();
            isAdvertising = false;
            Serial.println("[BLE-BEACON] Stopped advertising");
        }
    }

    void stop() {
        stopAdvertising();
        if (isInitialized) {
            BLEDevice::deinit(true);
            isInitialized = false;
            Serial.println("[BLE-BEACON] BLE Beacon stopped");
        }
    }

    bool isActive() const {
        return isAdvertising;
    }

private:
    BLEServer* pServer = nullptr;
    BLEAdvertising* pAdvertising = nullptr;
    bool isInitialized = false;
    bool isAdvertising = false;
};

// BLE Scanner for Collectors/Repeaters
// Scans for parent nodes to discover and wake them up
class BLEScannerManager {
public:
    struct ScanResult {
        bool found = false;
        String apSSID;      // WiFi AP SSID (what we need to connect)
        String nodeName;    // Node name (for logging/display)
        uint8_t nodeRole = 0;
        int rssi = 0;
        String address;
    };

    // Returns true if manufacturer data starts with our magic mesh company ID.
    // Signature: [BLE_MESH_COMPANY_ID_LO][BLE_MESH_COMPANY_ID_HI][role][ssid...]
    static bool isMeshManufacturerData(const String& mfg) {
        return mfg.length() >= 3 &&
               (uint8_t)mfg[0] == BLE_MESH_COMPANY_ID_LO &&
               (uint8_t)mfg[1] == BLE_MESH_COMPANY_ID_HI;
    }

    void begin(const String& scannerName = "MeshScanner") {
        Serial.println("[BLE-SCAN] Initializing BLE Scanner...");
        try {
            BLEDevice::init(scannerName.c_str());  // Use unique name for debugging
        } catch (...) {
            Serial.println("[BLE-SCAN] ERROR: Failed to initialize BLE");
            return;
        }
        
        pBLEScan = BLEDevice::getScan();
        if (!pBLEScan) {
            Serial.println("[BLE-SCAN] ERROR: Failed to get BLE scan object");
            BLEDevice::deinit(true);
            return;
        }
        
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        isInitialized = true;
        Serial.println("[BLE-SCAN] BLE Scanner initialized");
    }

    ScanResult scanForParent(int scanDurationSeconds = 5) {
        ScanResult result;
        
        if (!isInitialized) {
            Serial.println("[BLE-SCAN] Error: Not initialized");
            return result;
        }

        Serial.printf("[BLE-SCAN] Starting scan for %d seconds...\n", scanDurationSeconds);
        
        BLEScanResults* foundDevices = pBLEScan->start(scanDurationSeconds, false);
        int count = foundDevices->getCount();
        
        Serial.printf("[BLE-SCAN] Found %d devices\n", count);
        
        // Find the strongest mesh node signal.
        // A device is a mesh node if it either:
        //   (a) advertises BLE_MESH_SERVICE_UUID, OR
        //   (b) has manufacturer data beginning with our magic company ID
        //       (BLE_MESH_COMPANY_ID_LO / HI).
        // Using both checks in parallel makes discovery robust against BLE
        // library or platform quirks where 128-bit UUID matching may fail.
        int bestRSSI = -999;
        int bestIndex = -1;
        
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice device = foundDevices->getDevice(i);
            
            // --- detection path 1: service UUID ---
            bool hasMeshUUID = device.haveServiceUUID() &&
                               device.isAdvertisingService(BLEUUID(BLE_MESH_SERVICE_UUID));

            // --- detection path 2: magic company ID in manufacturer data ---
            bool hasMeshMfg = false;
            if (device.haveManufacturerData()) {
                hasMeshMfg = isMeshManufacturerData(device.getManufacturerData());
            }

            if (hasMeshUUID || hasMeshMfg) {
                int rssi = device.getRSSI();
                
                // Preview the SSID for the log (extracted below in detail)
                String previewSSID = device.getName().c_str();
                if (device.haveManufacturerData()) {
                    String mfg = device.getManufacturerData();
                    if (hasMeshMfg && mfg.length() > 3) {
                        previewSSID = "";
                        for (size_t j = 3; j < mfg.length(); j++) previewSSID += (char)mfg[j];
                    }
                }
                Serial.printf("[BLE-SCAN] Found mesh node AP: %s, RSSI: %d (UUID=%d mfg=%d)\n",
                              previewSSID.c_str(), rssi, hasMeshUUID ? 1 : 0, hasMeshMfg ? 1 : 0);

                if (rssi > bestRSSI) {
                    bestRSSI = rssi;
                    bestIndex = i;
                }
            }
        }
        
        if (bestIndex >= 0) {
            BLEAdvertisedDevice bestDevice = foundDevices->getDevice(bestIndex);
            result.found = true;
            result.nodeName = String(bestDevice.getName().c_str());
            result.rssi = bestRSSI;
            result.address = String(bestDevice.getAddress().toString().c_str());
            
            // Extract role and AP SSID from manufacturer data.
            // New format: [company_lo][company_hi][role][ssid...]
            if (bestDevice.haveManufacturerData()) {
                String mfgData = bestDevice.getManufacturerData();
                if (isMeshManufacturerData(mfgData)) {
                    result.nodeRole = (uint8_t)mfgData[2];  // byte 2 = role
                    if (mfgData.length() > 3) {
                        result.apSSID = "";
                        for (size_t i = 3; i < mfgData.length(); i++) {
                            result.apSSID += (char)mfgData[i];
                        }
                    } else {
                        result.apSSID = result.nodeName;
                    }
                } else {
                    // Legacy / unexpected format: fall back to BLE device name
                    result.apSSID = result.nodeName;
                }
            } else {
                // No manufacturer data, use BLE name as fallback
                result.apSSID = result.nodeName;
            }
            
            const char* roleStr = (result.nodeRole == 0) ? "Repeater" : "Root";
            Serial.printf("[BLE-SCAN] Selected parent SSID: %s (Role: %s, RSSI: %d)\n", 
                         result.apSSID.c_str(), roleStr, result.rssi);
        } else {
            Serial.println("[BLE-SCAN] No mesh nodes found");
        }
        
        pBLEScan->clearResults();
        return result;
    }

    void stop() {
        if (isInitialized) {
            pBLEScan->stop();
            BLEDevice::deinit(true);
            isInitialized = false;
            Serial.println("[BLE-SCAN] BLE Scanner stopped");
        }
    }

private:
    BLEScan* pBLEScan = nullptr;
    bool isInitialized = false;
};

#endif // BLE_MESH_BEACON_H
