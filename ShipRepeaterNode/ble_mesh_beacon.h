#ifndef BLE_MESH_BEACON_H
#define BLE_MESH_BEACON_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// BLE Service UUID for mesh node identification
#define BLE_MESH_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

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
        
        // Create BLE Server (needed for advertising)
        pServer = BLEDevice::createServer();
        if (!pServer) {
            Serial.println("[BLE-BEACON] ERROR: Failed to create BLE server");
            return;
        }
        
        // Get advertising object
        pAdvertising = BLEDevice::getAdvertising();
        if (!pAdvertising) {
            Serial.println("[BLE-BEACON] ERROR: Failed to get advertising object");
            return;
        }

        // Bug 3 fix: split advertisement data to avoid exceeding the 31-byte PDU limit.
        //
        // Primary advertisement: service UUID only (18 bytes).
        // A 128-bit UUID + manufacturer data easily overflows 31 bytes, which
        // silently drops the UUID so the scanner never recognises the device.
        BLEAdvertisementData primaryData;
        primaryData.setCompleteServices(BLEUUID(BLE_MESH_SERVICE_UUID));
        pAdvertising->setAdvertisementData(primaryData);

        // Scan response: manufacturer data with a custom magic prefix.
        // Format: [0x53, 0x4D, role_byte, apSSID_bytes...]
        // Bytes 0x53/0x4D ("SM") are used as a non-registered custom company-ID
        // (little-endian 16-bit field in the BT manufacturer data AD type).
        // This lets the scanner identify mesh nodes even when the service-UUID
        // check fails due to radio contention or advertisement PDU caching.
        BLEAdvertisementData scanRespData;
        std::string mfgData;
        mfgData.push_back(0x53); // company ID low byte  (magic 'S')
        mfgData.push_back(0x4D); // company ID high byte (magic 'M')
        mfgData.push_back(nodeRole);
        mfgData.append(apSSID.c_str());
        scanRespData.setManufacturerData(mfgData);
        pAdvertising->setScanResponseData(scanRespData);

        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMaxPreferred(0x12);
        
        isInitialized = true;
        Serial.printf("[BLE-BEACON] BLE Beacon initialized (advertising AP SSID: %s)\n", apSSID.c_str());
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
        
        BLEScanResults foundDevices = pBLEScan->start(scanDurationSeconds, false);
        int count = foundDevices.getCount();
        
        Serial.printf("[BLE-SCAN] Found %d devices\n", count);
        
        // Find the strongest signal matching our mesh service UUID or magic bytes.
        // Bug 3 fix: dual detection avoids missing nodes when the UUID advertisement
        // packet is dropped due to radio contention or advertisement PDU overflow.
        int bestRSSI = -999;
        int bestIndex = -1;
        
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice device = foundDevices.getDevice(i);

            // Debug: print every found device so misses can be diagnosed
            std::string mfgRaw = device.haveManufacturerData() ? device.getManufacturerData() : "";
            Serial.printf("[BLE-SCAN][%d] addr=%s rssi=%d uuid=%s mfg=%d bytes\n",
                         i,
                         device.getAddress().toString().c_str(),
                         device.getRSSI(),
                         device.haveServiceUUID() ? device.getServiceUUID().toString().c_str() : "none",
                         (int)mfgRaw.size());

            // Primary detection: advertised service UUID
            bool isMeshNode = (device.haveServiceUUID() &&
                               device.isAdvertisingService(BLEUUID(BLE_MESH_SERVICE_UUID)));

            // Fallback detection: custom magic prefix 0x53/0x4D ("SM") in the
            // manufacturer data AD type — catches nodes whose service UUID was
            // not returned (scan response not received or PDU overflow).
            if (!isMeshNode && device.haveManufacturerData()) {
                std::string mfg = device.getManufacturerData();
                if (mfg.size() >= 2 &&
                    (uint8_t)mfg[0] == 0x53 &&
                    (uint8_t)mfg[1] == 0x4D) {
                    isMeshNode = true;
                }
            }

            if (!isMeshNode) continue;

            int rssi = device.getRSSI();

            // Extract AP SSID from manufacturer data if available.
            // New format: [0x53, 0x4D, role_byte, apSSID_bytes...]
            // Legacy format (no magic bytes): [role_byte, apSSID_bytes...]
            String apSSID = String(device.getName().c_str());
            if (device.haveManufacturerData()) {
                std::string mfgData = device.getManufacturerData();
                size_t ssidStart = 1; // legacy: role at [0], SSID from [1]
                if (mfgData.size() >= 2 &&
                    (uint8_t)mfgData[0] == 0x53 &&
                    (uint8_t)mfgData[1] == 0x4D) {
                    ssidStart = 3; // new: magic[0..1], role[2], SSID from [3]
                }
                if (mfgData.size() > ssidStart) {
                    apSSID = "";
                    for (size_t j = ssidStart; j < mfgData.size(); j++) {
                        apSSID += (char)mfgData[j];
                    }
                }
            }
            
            Serial.printf("[BLE-SCAN] Found mesh node AP: %s, RSSI: %d\n", 
                         apSSID.c_str(), rssi);
            
            if (rssi > bestRSSI) {
                bestRSSI  = rssi;
                bestIndex = i;
            }
        }
        
        if (bestIndex >= 0) {
            BLEAdvertisedDevice bestDevice = foundDevices.getDevice(bestIndex);
            result.found    = true;
            result.nodeName = String(bestDevice.getName().c_str());
            result.rssi     = bestRSSI;
            result.address  = String(bestDevice.getAddress().toString().c_str());
            
            // Extract role and AP SSID from manufacturer data
            if (bestDevice.haveManufacturerData()) {
                std::string mfgData = bestDevice.getManufacturerData();
                size_t ssidStart = 1; // legacy offset
                if (mfgData.size() >= 2 &&
                    (uint8_t)mfgData[0] == 0x53 &&
                    (uint8_t)mfgData[1] == 0x4D) {
                    // New format with magic bytes
                    ssidStart = 3;
                    if (mfgData.size() > 2) {
                        result.nodeRole = (uint8_t)mfgData[2];
                    }
                } else if (mfgData.size() > 0) {
                    // Legacy format
                    result.nodeRole = (uint8_t)mfgData[0];
                }
                if (mfgData.size() > ssidStart) {
                    result.apSSID = "";
                    for (size_t i = ssidStart; i < mfgData.size(); i++) {
                        result.apSSID += (char)mfgData[i];
                    }
                } else {
                    result.apSSID = result.nodeName;
                }
            } else {
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
