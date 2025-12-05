#ifndef BLE_MESH_BEACON_H
#define BLE_MESH_BEACON_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

// BLE Service UUID for mesh node identification
#define BLE_MESH_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
// BLE Characteristic UUID for wake-up signal
#define BLE_WAKEUP_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Callback interface for wake-up events
class BLEWakeupCallback {
public:
    virtual void onWakeupRequest() = 0;
};

// BLE Characteristic callback handler
class WakeupCharacteristicCallbacks : public BLECharacteristicCallbacks {
public:
    WakeupCharacteristicCallbacks(BLEWakeupCallback* cb) : callback(cb) {}
    
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0 && value[0] == 0x01 && callback) {
            Serial.println("[BLE-WAKEUP] Wake-up signal received!");
            callback->onWakeupRequest();
        }
    }
    
private:
    BLEWakeupCallback* callback;
};

// BLE Beacon Manager for Repeaters/Root
// Advertises the node's presence so children can discover and wake it up
class BLEBeaconManager {
public:
    void begin(const String& apSSID, const String& nodeName, uint8_t nodeRole, BLEWakeupCallback* wakeupCb = nullptr) {
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
        
        // Create BLE Service for wake-up
        BLEService* pService = pServer->createService(BLE_MESH_SERVICE_UUID);
        if (!pService) {
            Serial.println("[BLE-BEACON] ERROR: Failed to create BLE service");
            return;
        }
        
        // Create wake-up characteristic (writable by Collector to wake Repeater)
        if (wakeupCb != nullptr && nodeRole == 0) { // Only for Repeater role
            pWakeupChar = pService->createCharacteristic(
                BLE_WAKEUP_CHAR_UUID,
                BLECharacteristic::PROPERTY_WRITE
            );
            if (pWakeupChar) {
                pWakeupChar->setCallbacks(new WakeupCharacteristicCallbacks(wakeupCb));
                Serial.println("[BLE-BEACON] Wake-up characteristic created");
            }
        }
        
        // Start the service
        pService->start();
        
        // Get advertising object
        pAdvertising = BLEDevice::getAdvertising();
        if (!pAdvertising) {
            Serial.println("[BLE-BEACON] ERROR: Failed to get advertising object");
            return;
        }
        
        // Add service UUID to advertisement
        pAdvertising->addServiceUUID(BLE_MESH_SERVICE_UUID);
        
        // Set advertising parameters
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);  // Min connection interval
        pAdvertising->setMaxPreferred(0x12);  // Max connection interval
        
        // Add manufacturer data with node role and AP SSID
        // Format: [role_byte, apSSID_bytes...]
        // We advertise AP SSID because that's what's needed for WiFi connection
        BLEAdvertisementData advData;
        std::string mfgData;
        mfgData.push_back(nodeRole); // 0=Repeater, 1=Root
        mfgData.append(apSSID.c_str());  // Use AP SSID instead of node name
        advData.setManufacturerData(mfgData);
        advData.setCompleteServices(BLEUUID(BLE_MESH_SERVICE_UUID));
        pAdvertising->setAdvertisementData(advData);
        
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
    BLECharacteristic* pWakeupChar = nullptr;
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
        
        // Find the strongest signal with our service UUID
        int bestRSSI = -999;
        int bestIndex = -1;
        
        for (int i = 0; i < count; i++) {
            BLEAdvertisedDevice device = foundDevices.getDevice(i);
            
            // Check if device has our mesh service UUID
            if (device.haveServiceUUID() && device.isAdvertisingService(BLEUUID(BLE_MESH_SERVICE_UUID))) {
                int rssi = device.getRSSI();
                
                // Extract AP SSID from manufacturer data if available
                String apSSID = String(device.getName().c_str());  // Default to BLE name
                if (device.haveManufacturerData()) {
                    std::string mfgData = device.getManufacturerData();
                    if (mfgData.length() > 1) {
                        // Extract AP SSID from manufacturer data (skip first byte which is role)
                        apSSID = "";
                        for (size_t j = 1; j < mfgData.length(); j++) {
                            apSSID += (char)mfgData[j];
                        }
                    }
                }
                
                Serial.printf("[BLE-SCAN] Found mesh node AP: %s, RSSI: %d\n", 
                             apSSID.c_str(), rssi);
                
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
            
            // Extract role and AP SSID from manufacturer data
            if (bestDevice.haveManufacturerData()) {
                std::string mfgData = bestDevice.getManufacturerData();
                if (mfgData.length() > 0) {
                    result.nodeRole = mfgData[0];  // First byte is role
                    
                    // Extract AP SSID (rest of manufacturer data)
                    if (mfgData.length() > 1) {
                        result.apSSID = "";
                        for (size_t i = 1; i < mfgData.length(); i++) {
                            result.apSSID += (char)mfgData[i];
                        }
                    } else {
                        // Fallback to BLE device name if no SSID in manufacturer data
                        result.apSSID = result.nodeName;
                    }
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

    // Send wake-up signal to a discovered parent (Repeater)
    // Returns true if signal was sent successfully
    bool sendWakeupSignal(const String& deviceAddress) {
        if (!isInitialized) {
            Serial.println("[BLE-WAKEUP] Error: Scanner not initialized");
            return false;
        }
        
        Serial.printf("[BLE-WAKEUP] Connecting to device %s to send wake-up signal...\n", 
                     deviceAddress.c_str());
        
        BLEAddress bleAddress(deviceAddress.c_str());
        BLEClient* pClient = BLEDevice::createClient();
        
        if (!pClient) {
            Serial.println("[BLE-WAKEUP] Failed to create BLE client");
            return false;
        }
        
        // Connect to the device
        if (!pClient->connect(bleAddress)) {
            Serial.println("[BLE-WAKEUP] Failed to connect to device");
            delete pClient;
            return false;
        }
        
        Serial.println("[BLE-WAKEUP] Connected to device");
        
        // Get the service
        BLERemoteService* pRemoteService = pClient->getService(BLE_MESH_SERVICE_UUID);
        if (!pRemoteService) {
            Serial.println("[BLE-WAKEUP] Failed to find service");
            pClient->disconnect();
            delete pClient;
            return false;
        }
        
        // Get the characteristic
        BLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(BLE_WAKEUP_CHAR_UUID);
        if (!pRemoteChar) {
            Serial.println("[BLE-WAKEUP] Failed to find wake-up characteristic");
            pClient->disconnect();
            delete pClient;
            return false;
        }
        
        // Check if characteristic is writable
        if (!pRemoteChar->canWrite()) {
            Serial.println("[BLE-WAKEUP] Characteristic not writable");
            pClient->disconnect();
            delete pClient;
            return false;
        }
        
        // Send wake-up signal (write 0x01)
        uint8_t wakeupValue = 0x01;
        pRemoteChar->writeValue(&wakeupValue, 1, true);
        Serial.println("[BLE-WAKEUP] Wake-up signal sent!");
        
        // Wait a moment for the write to complete
        delay(500);
        
        // Disconnect
        pClient->disconnect();
        delete pClient;
        
        return true;
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
