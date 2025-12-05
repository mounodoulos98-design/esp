# BLE Mesh Wake-Up System

## Overview

The BLE (Bluetooth Low Energy) mesh wake-up system enables efficient power management in the mesh network by allowing child nodes (Collectors) to discover and wake up their parent nodes (Repeaters) before establishing WiFi connections.

## Architecture

### Roles

1. **Root Node**
   - Always powered on
   - **No BLE beacon** - always accessible via WiFi
   - Receives data from Repeaters and Collectors
   - Provides jobs and firmware updates

2. **Repeater Node** ⚡ **UPDATED with Dynamic WiFi AP Control**
   - **Light sleep with continuous BLE beacon** (not deep sleep)
   - **WiFi AP OFF by default** - only turns ON when needed
   - BLE beacon advertises continuously (acts as wake-up trigger)
   - Collector sends BLE write to wake-up characteristic
   - Repeater wakes up and starts WiFi AP
   - WiFi AP stays active until transfers complete
   - WiFi AP shuts down after timeout → back to light sleep
   - **Power savings: 50-70% reduction vs always-on AP**
   - Forwards data from Collectors to Root

3. **Collector Node**
   - Deep sleep between sensor collection and uplink windows
   - Scans for parent (Repeater) via BLE before connecting
   - Two modes:
     - **Scheduled uplink**: Wake at scheduled time, scan for parent, connect
     - **On-demand**: Light sleep + scan until parent found, then wake and send
   - Collects sensor data via WiFi AP
   - Sends data to parent via WiFi mesh

## How It Works

### BLE Beacon Advertising (Root/Repeater)

When a Root or Repeater node is active, it advertises a BLE beacon with:
- **Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **Node Name**: Configured name of the node
- **Node Role**: 0 = Repeater, 1 = Root
- **Manufacturer Data**: Contains role byte + node name

This allows child nodes to:
1. Discover parent nodes within BLE range (~50-100m)
2. Identify the role of each parent
3. Select the best parent based on RSSI (signal strength)

### BLE Scanning (Collector/Repeater)

Before establishing a WiFi connection during the mesh appointment window, child nodes:

1. **Wake up** from deep sleep at scheduled time
2. **Initialize BLE scanner**
3. **Scan for BLE beacons** (default: 5 seconds)
4. **Filter** for devices with mesh service UUID
5. **Select best parent** based on RSSI
6. **Connect via WiFi** to the discovered parent
7. **Transfer data** or receive jobs
8. **Go back to sleep** until next scheduled window

### Power Savings

The BLE wake-up mechanism provides several power benefits:

- **Short scan time**: BLE scanning is low power (~15-20 mA)
- **Targeted connection**: Only connect to confirmed available parents
- **No blind WiFi scanning**: Avoids power-hungry WiFi scans
- **Quick discovery**: BLE beacon detected in seconds vs. WiFi association

## Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                         ROOT NODE                            │
│  - Always ON                                                 │
│  - WiFi AP active                                            │
│  - NO BLE beacon (always accessible via WiFi)                │
└─────────────────────────────────────────────────────────────┘
                              ▲
                              │ WiFi connection established
                              │ (no BLE scan needed)
                              │
┌─────────────────────────────────────────────────────────────┐
│                       REPEATER NODE                          │
│  Default State (Light Sleep):                                │
│  1. WiFi AP: OFF (power saving)                             │
│  2. BLE Beacon advertising continuously (Role: 0)            │
│  3. Light sleep mode (instant wake-up capability)            │
│  4. Power consumption: ~10-15 mA                             │
│                                                              │
│  When Collector Sends BLE Wake-up:                          │
│  1. Receive BLE write to wake-up characteristic              │
│  2. Start WiFi AP                                           │
│  3. Start HTTP server                                       │
│  4. Receive data via WiFi from Collector                     │
│  5. Send jobs/firmware back to Collector                     │
│  6. Wait for all transfers to complete                       │
│  7. Detect no clients for 30s                               │
│  8. Stop WiFi AP → return to light sleep                     │
│  9. Power consumption while active: ~110-145 mA              │
└─────────────────────────────────────────────────────────────┘
                              ▲
                              │ BLE scan discovers Repeater
                              │ WiFi connection established
                              │
┌─────────────────────────────────────────────────────────────┐
│                      COLLECTOR NODE                          │
│  Sensor Collection Cycle:                                    │
│  1. Wake up for sensor collection                            │
│  2. Start WiFi AP for sensors                                │
│  3. Collect sensor data                                      │
│  4. Store data to SD card                                    │
│  5. Go to deep sleep                                         │
│                                                              │
│  Uplink Cycle (Scheduled):                                   │
│  1. Wake up on schedule                                      │
│  2. Scan for parent (Repeater) via BLE                       │
│  3. Connect to parent via WiFi                               │
│  4. Upload queued data                                       │
│  5. Receive jobs/firmware                                    │
│  6. Go to deep sleep                                         │
│                                                              │
│  Uplink Cycle (On-Demand):                                   │
│  1. Enter light sleep after data collection                  │
│  2. Scan for parent (Repeater) via BLE                       │
│  3. When found, connect via WiFi                             │
│  4. Upload data immediately                                  │
│  5. Go to deep sleep                                         │
└─────────────────────────────────────────────────────────────┘
                              ▲
                              │ WiFi AP connection
                              │ Heartbeat POST
                              │
                         ┌─────────┐
                         │ SENSORS │
                         └─────────┘
```

## Configuration

### Enable/Disable BLE Beacon

In `config.h` and stored in flash preferences:

```cpp
config.bleBeaconEnabled = true;  // Enable BLE mesh wake-up system
config.bleScanDurationSec = 5;   // BLE scan duration in seconds
```

### BLE Beacon Behavior by Role

| Role      | Advertises BLE Beacon | Scans for Parent | Sleep Mode    |
|-----------|----------------------|------------------|---------------|
| Root      | ❌ No                | ❌ No            | Always awake  |
| Repeater  | ✅ Continuously      | ❌ No            | Light sleep   |
| Collector | ❌ No                | ✅ Yes           | Deep sleep    |

## Implementation Details

### BLE Beacon Manager (`ble_mesh_beacon.h`)

- **Class**: `BLEBeaconManager`
- **Purpose**: Advertise node presence for discovery and enable wake-up
- **Used by**: Root and Repeater nodes
- **Methods**:
  - `begin(apSSID, nodeName, nodeRole, wakeupCallback)` - Initialize BLE beacon with optional wake-up callback
  - `startAdvertising()` - Start advertising
  - `stopAdvertising()` - Stop advertising
  - `stop()` - Deinitialize BLE
- **New Features** ⚡:
  - Wake-up characteristic (UUID: `beb5483e-36e1-4688-b7f5-ea07361b26a8`)
  - Write-only characteristic for Collectors to send wake-up signal
  - Callback interface for handling wake-up requests
  - Dynamic WiFi AP control integration

### BLE Scanner Manager (`ble_mesh_beacon.h`)

- **Class**: `BLEScannerManager`
- **Purpose**: Discover parent nodes and send wake-up signals
- **Used by**: Collector and Repeater nodes
- **Methods**:
  - `begin(scannerName)` - Initialize BLE scanner
  - `scanForParent(duration)` - Scan and return best parent with address
  - `sendWakeupSignal(address)` - **NEW!** Send BLE wake-up signal to parent Repeater
  - `stop()` - Deinitialize BLE
- **Wake-up Protocol** ⚡:
  1. Scan for parent Repeater
  2. Get Repeater's BLE address from scan result
  3. Connect to Repeater via BLE
  4. Write 0x01 to wake-up characteristic
  5. Disconnect BLE
  6. Wait 3 seconds for WiFi AP to start
  7. Connect to Repeater via WiFi

### Integration Points

1. **Root Loop** (`loopOperationalMode()`)
   - Starts BLE beacon on first loop iteration
   - Keeps beacon active continuously

2. **Repeater Loop** (`loopOperationalMode()`)
   - Starts BLE beacon when awake
   - Scans for parent before uplink
   - Stops beacon before sleep

3. **Collector Uplink** (`STATE_MESH_APPOINTMENT`)
   - Scans for parent at start of uplink window
   - Connects to discovered parent
   - Proceeds with data upload

4. **Deep Sleep** (`goToDeepSleep()`)
   - Stops BLE beacon before entering deep sleep
   - Reduces power consumption during sleep

## Testing

### Test BLE Beacon Advertising

1. Configure a node as Root or Repeater
2. Set `bleBeaconEnabled = true`
3. Power on the node
4. Use a BLE scanner app (e.g., nRF Connect) to verify:
   - Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
   - Manufacturer data contains role and name

### Test BLE Parent Discovery

1. Set up a Root node with BLE beacon enabled
2. Configure a Collector node with BLE enabled
3. Monitor serial output during mesh appointment:
   ```
   [BLE-MESH] Scanning for parent node...
   [BLE-SCAN] Found mesh node: RootNode, RSSI: -45
   [BLE-MESH] Found parent: RootNode (Role: Root, RSSI: -45 dBm)
   ```

### Test Complete Wake-Up Flow

1. Configure Root → Repeater → Collector chain
2. Enable BLE on all nodes
3. Verify:
   - Collector wakes up and scans for Repeater
   - Collector connects to Repeater via WiFi
   - Repeater wakes up and scans for Root
   - Repeater connects to Root via WiFi
   - Data flows from Collector → Repeater → Root

## Troubleshooting

### BLE beacon not advertising

- Check `config.bleBeaconEnabled = true`
- Verify node role is Root or Repeater
- Check serial output for `[BLE-BEACON] Started advertising`
- ESP32 must support BLE (ESP32-S3, ESP32-C3, etc.)

### BLE scan finds no parent

- Verify parent node is powered on and advertising
- Check BLE range (~50-100m line of sight)
- Increase `bleScanDurationSec` (default: 5s)
- Verify Service UUID matches on both sides
- Check for WiFi/BLE interference

### Collector connects to wrong parent

- System selects parent with strongest RSSI
- Position nodes to optimize signal strength
- Or disable BLE and use fixed uplink configuration

## Performance Characteristics

| Metric | Value |
|--------|-------|
| BLE scan duration | 5 seconds (configurable) |
| BLE scan power | ~15-20 mA |
| BLE beacon power | ~10-15 mA |
| Discovery range | 50-100 meters (line of sight) |
| Parent selection | Best RSSI (strongest signal) |

## Future Enhancements

1. **Multi-parent support**: Connect to multiple parents for redundancy
2. **Parent ranking**: Consider factors beyond RSSI (load, battery level)
3. **BLE mesh routing**: Use BLE for actual data transfer (not just discovery)
4. **Wake-on-BLE**: Parent wakes child via BLE notification
5. **Dynamic parent switching**: Switch parents based on signal quality
6. **BLE security**: Add encryption/authentication to BLE beacon

## Credits

This BLE mesh wake-up system implements the architecture discussed in PR #5, where BLE is used for efficient parent discovery in the mesh network to minimize power consumption while maintaining reliable connectivity.
