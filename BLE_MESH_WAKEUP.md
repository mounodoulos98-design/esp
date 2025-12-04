# BLE Mesh Wake-Up System

## Overview

The BLE (Bluetooth Low Energy) mesh wake-up system enables efficient power management in the mesh network by allowing child nodes (Collectors and Repeaters) to discover and wake up their parent nodes before establishing WiFi connections.

## Architecture

### Roles

1. **Root Node**
   - Always powered on
   - Acts as BLE beacon (advertises continuously)
   - Receives data from Repeaters and Collectors
   - Provides jobs and firmware updates

2. **Repeater Node**
   - Sleeps between scheduled windows
   - Acts as BLE beacon when awake (advertises itself)
   - Scans for parent (Root or another Repeater) before connecting
   - Forwards data from Collectors to Root

3. **Collector Node**
   - Sleeps between sensor collection and uplink windows
   - Scans for parent (Repeater or Root) before connecting
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
│  - BLE Beacon advertising (Role: 1)                          │
└─────────────────────────────────────────────────────────────┘
                              ▲
                              │ BLE scan discovers Root
                              │ WiFi connection established
                              │
┌─────────────────────────────────────────────────────────────┐
│                       REPEATER NODE                          │
│  Cycle:                                                      │
│  1. Wake up on schedule                                      │
│  2. Start WiFi AP                                            │
│  3. Start BLE Beacon advertising (Role: 0)                   │
│  4. Scan for parent (Root) via BLE                           │
│  5. Connect to parent via WiFi                               │
│  6. Forward collected data                                   │
│  7. Stop BLE beacon                                          │
│  8. Go to deep sleep                                         │
└─────────────────────────────────────────────────────────────┘
                              ▲
                              │ BLE scan discovers Repeater
                              │ WiFi connection established
                              │
┌─────────────────────────────────────────────────────────────┐
│                      COLLECTOR NODE                          │
│  Cycle:                                                      │
│  1. Wake up for sensor collection                            │
│  2. Start WiFi AP for sensors                                │
│  3. Collect sensor data                                      │
│  4. Store data to SD card                                    │
│  5. Go to deep sleep                                         │
│                                                              │
│  Uplink Cycle:                                               │
│  1. Wake up on schedule                                      │
│  2. Scan for parent (Repeater/Root) via BLE                  │
│  3. Connect to parent via WiFi                               │
│  4. Upload queued data                                       │
│  5. Receive jobs/firmware                                    │
│  6. Go to deep sleep                                         │
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

| Role      | Advertises BLE Beacon | Scans for Parent |
|-----------|----------------------|------------------|
| Root      | ✅ Always            | ❌ No            |
| Repeater  | ✅ When awake        | ✅ Yes           |
| Collector | ❌ No                | ✅ Yes           |

## Implementation Details

### BLE Beacon Manager (`ble_mesh_beacon.h`)

- **Class**: `BLEBeaconManager`
- **Purpose**: Advertise node presence for discovery
- **Used by**: Root and Repeater nodes
- **Methods**:
  - `begin(nodeName, nodeRole)` - Initialize BLE beacon
  - `startAdvertising()` - Start advertising
  - `stopAdvertising()` - Stop advertising
  - `stop()` - Deinitialize BLE

### BLE Scanner Manager (`ble_mesh_beacon.h`)

- **Class**: `BLEScannerManager`
- **Purpose**: Discover parent nodes
- **Used by**: Collector and Repeater nodes
- **Methods**:
  - `begin()` - Initialize BLE scanner
  - `scanForParent(duration)` - Scan and return best parent
  - `stop()` - Deinitialize BLE

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
