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
    void begin(const String& nodeName, uint8_t nodeRole) {
        Serial.println("[BLE-BEACON] Initializing BLE Beacon...");
        
        // Initialize BLE
        BLEDevice::init(nodeName.c_str());
        
        // Create BLE Server (needed for advertising)
        pServer = BLEDevice::createServer();
        
        // Get advertising object
        pAdvertising = BLEDevice::getAdvertising();
        
        // Add service UUID to advertisement
        pAdvertising->addServiceUUID(BLE_MESH_SERVICE_UUID);
        
        // Set advertising parameters
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);  // Min connection interval
        pAdvertising->setMaxPreferred(0x12);  // Max connection interval
        
        // Add manufacturer data with node role
        // Format: [role_byte, name_bytes...]
        std::string advData;
        advData.push_back(nodeRole); // 0=Repeater, 1=Root
        advData.append(nodeName.c_str());
        pAdvertising->setManufacturerData(advData);
        
        isInitialized = true;
        Serial.println("[BLE-BEACON] BLE Beacon initialized");
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
        String nodeName;
        uint8_t nodeRole = 0;
        int rssi = 0;
        String address;
    };

    void begin() {
        Serial.println("[BLE-SCAN] Initializing BLE Scanner...");
        BLEDevice::init("MeshScanner");  // Descriptive name for debugging
        pBLEScan = BLEDevice::getScan();
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
        
        // Find the strongest signal with our service UUID
        int bestRSSI = -999;
        int bestIndex = -1;
        
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice device = foundDevices.getDevice(i);
            
            // Check if device has our mesh service UUID
            if (device.haveServiceUUID() && device.isAdvertisingService(BLEUUID(BLE_MESH_SERVICE_UUID))) {
                int rssi = device.getRSSI();
                Serial.printf("[BLE-SCAN] Found mesh node: %s, RSSI: %d\n", 
                             device.getName().c_str(), rssi);
                
                if (rssi > bestRSSI) {
                    bestRSSI = rssi;
                    bestIndex = i;
                }
            }
        }
        
        if (bestIndex >= 0) {
            BLEAdvertisedDevice bestDevice = foundDevices.getDevice(bestIndex);
            result.found = true;
            result.nodeName = String(bestDevice.getName().c_str());
            result.rssi = bestRSSI;
            result.address = String(bestDevice.getAddress().toString().c_str());
            
            // Try to extract role from manufacturer data
            if (bestDevice.haveManufacturerData()) {
                std::string mfgData = bestDevice.getManufacturerData();
                if (mfgData.length() > 0) {
                    result.nodeRole = mfgData[0];
                }
            }
            
            Serial.printf("[BLE-SCAN] Selected parent: %s (RSSI: %d)\n", 
                         result.nodeName.c_str(), result.rssi);
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
