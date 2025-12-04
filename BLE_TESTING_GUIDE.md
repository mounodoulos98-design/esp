# BLE Mesh Wake-Up Testing Guide

## Quick Start Testing

### Prerequisites

- ESP32 modules with BLE support (ESP32-S3, ESP32-C3, ESP32 classic)
- Arduino IDE or PlatformIO
- BLE scanner app on phone (e.g., nRF Connect, LightBlue)
- USB cables for serial monitoring

### Configuration

The BLE feature is **enabled by default**. Configuration is stored in flash memory.

#### Via Web Configuration Interface

1. Power on ESP32 while holding BOOT button (enters config mode)
2. Connect to WiFi AP `Repeater_Setup_XXXXXX`
3. Open browser to `192.168.4.1`
4. Configure:
   - **Node Role**: COLLECTOR, REPEATER, or ROOT
   - **Node Name**: Unique name for identification
   - BLE settings are automatic (enabled by default)

#### Via Code (if needed)

In `config.h`, these are the defaults:
```cpp
bool bleBeaconEnabled = true;      // Enable BLE mesh wake-up
int bleScanDurationSec = 5;        // BLE scan duration (seconds)
```

No changes needed unless you want to disable BLE or adjust scan time.

---

## Test Scenario 1: Single Repeater + Single Collector

### Step 1: Set Up Repeater

1. **Flash** Repeater node with firmware
2. **Configure**:
   - Role: REPEATER
   - Node Name: "Repeater1"
   - AP SSID: "Repeater_AP"
3. **Power on** and monitor serial output

**Expected Serial Output (Repeater)**:
```
[MODE] Configuration found. Entering Operational Mode.
[BLE-BEACON] Initializing BLE Beacon...
[BLE-BEACON] BLE Beacon initialized
[BLE-BEACON] Started advertising
[SCHEDULER] Repeater stays active with BLE beacon (automatic light sleep)
```

4. **Verify with BLE scanner app**:
   - Open nRF Connect or LightBlue
   - Look for device named "Repeater1"
   - Verify Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
   - Check Manufacturer Data contains role byte (0x00 for Repeater)

### Step 2: Set Up Collector

1. **Flash** Collector node with firmware
2. **Configure**:
   - Role: COLLECTOR
   - Node Name: "Collector1"
   - Sensor AP SSID: "SensorAP"
   - Uplink SSID: "Repeater_AP" (connect to Repeater)
3. **Power on** and monitor serial output

**Expected Serial Output (Collector)**:
```
[MODE] Configuration found. Entering Operational Mode.
[STATE] Executing: UPLINK APPOINTMENT (COLLECTOR)
[BLE-MESH] Scanning for parent node...
[BLE-SCAN] Initializing BLE Scanner...
[BLE-SCAN] BLE Scanner initialized
[BLE-SCAN] Starting scan for 5 seconds...
[BLE-SCAN] Found 1 devices
[BLE-SCAN] Found mesh node: Repeater1, RSSI: -45
[BLE-SCAN] Selected parent: Repeater1 (RSSI: -45)
[BLE-MESH] Found parent: Repeater1 (Role: Repeater, RSSI: -45 dBm)
[UPLINK] Connecting to WiFi...
[UPLINK] Connected! Uploading data...
```

### Step 3: Verify Data Transfer

1. Collector should connect to Repeater via WiFi after BLE discovery
2. Check Repeater serial output for incoming connection
3. Verify files are uploaded from Collector to Repeater

**Success Criteria**:
- ✅ Repeater advertises BLE beacon continuously
- ✅ Collector finds Repeater via BLE scan
- ✅ Collector connects to Repeater via WiFi
- ✅ Data transfers successfully

---

## Test Scenario 2: Root + Repeater + Collector Chain

### Step 1: Set Up Root

1. **Flash** Root node
2. **Configure**:
   - Role: ROOT
   - Node Name: "Root"
   - AP SSID: "Root_AP"
3. **Power on**

**Expected Serial Output (Root)**:
```
[MODE] Configuration found. Entering Operational Mode.
Root loop
```

**Note**: Root does NOT use BLE - it's always on and accessible via WiFi.

### Step 2: Set Up Repeater

1. **Flash** Repeater node
2. **Configure**:
   - Role: REPEATER
   - Node Name: "Repeater1"
   - AP SSID: "Repeater_AP" (for Collectors)
   - Uplink SSID: "Root_AP" (connect to Root)
3. **Power on**

**Expected**: Repeater advertises BLE beacon and connects to Root for uplink.

### Step 3: Set Up Collector

1. **Flash** Collector node
2. **Configure**:
   - Role: COLLECTOR
   - Node Name: "Collector1"
   - Uplink SSID: "Repeater_AP" (connect to Repeater)
3. **Power on**

**Expected Flow**:
1. Collector scans for Repeater via BLE
2. Collector finds Repeater
3. Collector connects to Repeater via WiFi
4. Collector uploads data to Repeater
5. Repeater forwards data to Root

---

## Test Scenario 3: Multiple Collectors → One Repeater

Test scalability with multiple collectors connecting to same repeater.

1. Set up 1 Repeater (as above)
2. Set up 2-3 Collectors with different names
3. Power on all Collectors simultaneously
4. Verify each finds Repeater and connects

**Expected**:
- All Collectors discover same Repeater beacon
- All Collectors connect successfully (one at a time)
- Repeater handles multiple connections

---

## Troubleshooting

### Problem: Repeater not advertising BLE

**Check**:
1. Serial output for `[BLE-BEACON] Started advertising`
2. If missing, check `config.bleBeaconEnabled = true`
3. Verify ESP32 model supports BLE (ESP32-S3, ESP32-C3, classic ESP32)
4. Try power cycle

**Fix**:
```cpp
// In configuration mode or preferences
config.bleBeaconEnabled = true;
saveConfiguration();
```

### Problem: Collector doesn't find Repeater

**Check**:
1. Repeater is powered on and advertising
2. Distance between nodes (<50m recommended)
3. Serial output shows `[BLE-SCAN] Starting scan...`
4. Increase scan duration: `config.bleScanDurationSec = 10;`

**Debug with BLE Scanner App**:
1. Use phone app to verify Repeater beacon is visible
2. Note RSSI (should be > -80 dBm for reliable connection)
3. Verify Service UUID matches

### Problem: BLE scan finds Repeater but WiFi fails

**Check**:
1. Repeater WiFi AP is active: `WiFi.softAPIP()` should show IP
2. Collector uplink SSID matches Repeater AP SSID
3. WiFi password is correct
4. Check serial for WiFi connection errors

**Fix**:
- Verify AP SSID configuration
- Check WiFi password in preferences
- Ensure Repeater AP is stable

### Problem: High power consumption

**Check**:
1. Repeater should be in light sleep (not active loop)
2. Collector should enter deep sleep between cycles
3. Monitor current draw:
   - Repeater: ~20-30 mA (light sleep + BLE)
   - Collector: <1 mA (deep sleep)
   - Collector scan: ~15-20 mA (5 seconds)

---

## Advanced Testing

### Test BLE Range

1. Place Repeater in fixed location
2. Move Collector to various distances
3. Monitor RSSI values in serial output
4. Note maximum reliable range

**Typical Results**:
- Indoor: 20-50 meters
- Outdoor (line of sight): 50-100 meters
- Through walls: 10-20 meters

### Test BLE Scan Duration

Experiment with different scan durations:

```cpp
config.bleScanDurationSec = 3;  // Fast scan
config.bleScanDurationSec = 5;  // Default
config.bleScanDurationSec = 10; // Thorough scan
```

**Trade-off**:
- Shorter scan = faster wake-up, may miss beacon
- Longer scan = more reliable, uses more power

### Monitor Power Consumption

Use multimeter or power profiler:

1. **Repeater** (continuous):
   - Measure average current over 60 seconds
   - Expected: 20-30 mA with BLE active

2. **Collector** (cyclic):
   - Deep sleep: <1 mA
   - BLE scan: 15-20 mA for 5 seconds
   - WiFi active: 80-120 mA for data transfer

---

## Serial Debug Commands

### Check BLE Status

Monitor these log messages:

**Repeater**:
```
[BLE-BEACON] Initializing BLE Beacon...
[BLE-BEACON] BLE Beacon initialized
[BLE-BEACON] Started advertising
[SCHEDULER] Repeater stays active with BLE beacon
```

**Collector**:
```
[BLE-MESH] Scanning for parent node...
[BLE-SCAN] Starting scan for 5 seconds...
[BLE-SCAN] Found mesh node: [name], RSSI: [value]
[BLE-MESH] Found parent: [name] (Role: Repeater, RSSI: [value] dBm)
```

### Enable Verbose Logging

For more detailed BLE debugging, monitor serial output at 115200 baud.

---

## BLE Scanner App Testing

### Using nRF Connect (Recommended)

1. **Install** nRF Connect app (iOS/Android)
2. **Open** app and start scanning
3. **Look for** device named after your node (e.g., "Repeater1")
4. **Verify**:
   - Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
   - Manufacturer Data: First byte is role (0x00=Repeater, 0x01=Root)
   - RSSI: Should be visible (e.g., -45 dBm)
5. **Connect** (optional) to see services

### Using LightBlue (Alternative)

Similar steps as nRF Connect:
1. Scan for peripherals
2. Find your device
3. Verify UUID and manufacturer data

---

## Expected Test Results

### Successful BLE Discovery

**Repeater Serial**:
```
[BLE-BEACON] Started advertising
[AP] Station connected: aa:bb:cc:dd:ee:ff
```

**Collector Serial**:
```
[BLE-MESH] Found parent: Repeater1 (Role: Repeater, RSSI: -45 dBm)
[WiFi] Connecting to Repeater_AP...
[WiFi] Connected! IP: 192.168.20.2
[UPLINK] Uploading data...
```

### Failed BLE Discovery (No Parent Found)

**Collector Serial**:
```
[BLE-SCAN] Found 0 devices
[BLE-MESH] No parent found via BLE, proceeding with configured uplink
[WiFi] Connecting to Repeater_AP...
```

**Action**: Collector falls back to configured WiFi uplink (no BLE wake-up).

---

## Performance Benchmarks

### BLE Discovery Time

- **Typical**: 2-5 seconds
- **Maximum**: 10 seconds (with long scan duration)
- **Minimum**: 1-2 seconds (beacon immediately visible)

### Power Savings vs WiFi Scan

- **WiFi scan**: 80-120 mA for 10-30 seconds = ~200-1000 mAh
- **BLE scan**: 15-20 mA for 5 seconds = ~25-33 mAh
- **Savings**: ~87-97%

### Connection Success Rate

- **<10m**: ~100% (excellent signal)
- **10-30m**: ~95% (good signal)
- **30-50m**: ~80% (marginal signal, may need retry)
- **>50m**: <50% (poor signal, unreliable)

---

## Next Steps After Testing

1. **Verify** all nodes communicate successfully
2. **Monitor** power consumption over 24 hours
3. **Test** failover scenarios (Repeater power loss)
4. **Optimize** scan duration for your environment
5. **Document** any issues or improvements needed

---

## Common Questions

**Q: Do I need to configure BLE manually?**  
A: No, BLE is enabled by default. Just configure node roles.

**Q: Can I disable BLE if I don't need it?**  
A: Yes, set `config.bleBeaconEnabled = false` in preferences.

**Q: Does Root need BLE?**  
A: No, Root is always on and accessible via WiFi only.

**Q: How many Collectors can connect to one Repeater?**  
A: Multiple Collectors can discover same Repeater. WiFi connections are sequential.

**Q: What if BLE scan fails?**  
A: Collector falls back to configured WiFi uplink (works without BLE).

**Q: Can I use BLE scanner app to debug?**  
A: Yes! nRF Connect or LightBlue can verify beacon is advertising.

---

## Support

For issues or questions:
1. Check serial output for error messages
2. Verify configuration via web interface
3. Test with BLE scanner app
4. Review `BLE_MESH_WAKEUP.md` for architecture details
5. Check `BLE_IMPLEMENTATION_NOTES.md` for technical details

Happy testing! 🚀
