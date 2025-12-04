# BLE Mesh Wake-Up Configuration Guide

## Overview

This guide explains how to configure the BLE mesh wake-up system through the web interface for proper mesh communication between Collectors, Repeaters, and Root nodes.

## Configuration Changes

### What's New

Added BLE-specific configuration options to the web interface that allow you to:
1. Enable/disable BLE beacon advertising (Repeaters)
2. Enable/disable BLE scanning (Collectors)
3. Adjust BLE scan duration for power/reliability trade-off
4. Properly configure mesh communication hierarchy

### Why These Changes

The previous configuration didn't expose BLE settings, making it impossible to:
- Control BLE power consumption
- Tune scanning behavior
- Disable BLE if not needed
- Understand the role of BLE in the system

## Configuration by Role

### Collector Configuration

**Purpose**: Collect sensor data and send to Repeater/Root

**BLE Settings**:
- ✅ **Enable BLE scanning** (checkbox)
  - Checked (default): Collector scans for Repeater before WiFi connection
  - Unchecked: Collector connects directly via configured WiFi SSID
  
- ⚙️ **BLE Scan Duration** (1-30 seconds, default: 5)
  - Lower (1-3s): Faster wake-up, may miss beacon, lower power
  - Default (5s): Balanced reliability and power
  - Higher (10-30s): More reliable, higher power consumption

**When to enable BLE**:
- ✅ Repeater sleeps/wakes (needs discovery)
- ✅ Want power-efficient parent discovery
- ✅ Dynamic mesh where parents may move
- ❌ Repeater is always on (not sleeping)
- ❌ Fixed WiFi infrastructure (no mesh)

**Configuration Steps**:
1. Select Role: **Collector**
2. Set **Sensor AP SSID** (for sensors to connect)
3. Set **Cycle/Window** timing for sensor collection
4. Check **Enable BLE scanning** ✓
5. Set **BLE Scan Duration** (5 seconds recommended)
6. Set **Uplink SSID** (Repeater's AP or Root's AP)
7. Set **Uplink Password**
8. Set **Uplink Host/Port** for data transfer
9. Save and Reboot

**Example Configuration**:
```
Node Name: Collector_01
Role: Collector
Sensor AP SSID: SensorAP
Cycle: 120 sec
AP Window: 15 sec
BLE Scanning: ✓ Enabled
BLE Scan Duration: 5 seconds
Uplink SSID: Repeater_AP
Uplink Password: [password]
Uplink Host: 192.168.20.1
Uplink Port: 8080
```

---

### Repeater Configuration

**Purpose**: Forward data from Collectors to Root, act as mesh node

**BLE Settings**:
- ✅ **Enable BLE beacon** (checkbox)
  - Checked (default): Repeater advertises BLE beacon continuously
  - Unchecked: Repeater accessible only via WiFi (no BLE)

**BLE Beacon Behavior**:
- **Always advertising** (24/7) when enabled
- **Light sleep mode**: ~20-30 mA power consumption
- **Instant wake-up**: When Collector needs to connect
- **No deep sleep**: Stays responsive for child nodes

**When to enable BLE**:
- ✅ Collectors need to discover this Repeater
- ✅ Want low-power operation with light sleep
- ✅ Repeater location may change
- ❌ Repeater is always accessible via fixed WiFi
- ❌ No child nodes (Collectors) connect to this Repeater

**Configuration Steps**:
1. Select Role: **Repeater**
2. Set **Repeater AP SSID** (for Collectors to connect)
3. Set **Repeater AP Password**
4. Check **Enable BLE beacon** ✓
5. Set **Uplink SSID** (Root's AP)
6. Set **Uplink Password**
7. Set **Uplink Host/Port** for forwarding data
8. Save and Reboot

**Example Configuration**:
```
Node Name: Repeater_01
Role: Repeater
Repeater AP SSID: Repeater_AP
Repeater AP Password: [password]
BLE Beacon: ✓ Enabled
Uplink SSID: Root_AP
Uplink Password: [password]
Uplink Host: 192.168.10.1
Uplink Port: 8080
```

---

### Root Configuration

**Purpose**: Final destination for all data, always accessible

**BLE Settings**:
- ❌ **Enable BLE beacon** (checkbox, unchecked by default)
  - Unchecked (default): Root accessible via WiFi only
  - Checked: Root also advertises BLE beacon (rarely needed)

**Why BLE is disabled for Root**:
- Root is **always on** (never sleeps)
- Root is **always accessible via WiFi**
- BLE adds no benefit (waste of power)
- Fixed IP address (192.168.10.1 typically)

**When to enable BLE** (rare cases):
- ✅ Root location changes dynamically
- ✅ Multiple Roots and need discovery
- ❌ Most deployments (leave disabled)

**Configuration Steps**:
1. Select Role: **Root**
2. Set **Root AP SSID** (for Repeaters/Collectors)
3. Set **Root AP Password**
4. Leave **BLE beacon** ❌ Unchecked (default)
5. Set **HTTP Port** (8080 default)
6. Save and Reboot

**Example Configuration**:
```
Node Name: Root
Role: Root
Root AP SSID: Root_AP
Root AP Password: [password]
BLE Beacon: ❌ Disabled
HTTP Port: 8080
```

---

## Configuration Access

### Entering Configuration Mode

1. **Hold BOOT button** while powering on ESP32
2. Connect to WiFi AP: `Repeater_Setup_XXXXXX` or `Config_XXXXXX`
3. Open browser to: `http://192.168.4.1`
4. Configure settings
5. Click **Save and Reboot**

### Configuration Persistence

- All settings saved to **flash memory** (NVS)
- Survives power cycles and reboots
- Factory reset: Clear via `/factory_reset` endpoint or reflash firmware

---

## Mesh Topology Examples

### Example 1: Simple Chain (Root → Repeater → Collector)

```
┌──────────┐
│   ROOT   │ BLE: Disabled
│  Always  │ WiFi: Root_AP (192.168.10.1)
│    ON    │ Port: 8080
└─────┬────┘
      │ WiFi
      ↓
┌──────────┐
│ REPEATER │ BLE: Enabled (advertising)
│  Light   │ WiFi: Repeater_AP (192.168.20.1)
│  Sleep   │ Uplink: Root_AP
└─────┬────┘
      │ BLE scan + WiFi
      ↓
┌──────────┐
│COLLECTOR │ BLE: Enabled (scanning, 5s)
│Deep Sleep│ WiFi: SensorAP (for sensors)
│  Cycles  │ Uplink: Repeater_AP
└──────────┘
```

**Configuration**:
- **Root**: BLE disabled, WiFi always on
- **Repeater**: BLE enabled, light sleep, uplink to Root
- **Collector**: BLE scanning enabled, uplink to Repeater

---

### Example 2: Multi-Collector (Root → Repeater → Multiple Collectors)

```
┌──────────┐
│   ROOT   │ BLE: Disabled
│  Always  │
│    ON    │
└─────┬────┘
      │
      ↓
┌──────────┐
│ REPEATER │ BLE: Enabled
│  Light   │
│  Sleep   │
└─────┬────┘
      │
      ├─────→ Collector_01 (BLE scan: 5s)
      ├─────→ Collector_02 (BLE scan: 5s)
      └─────→ Collector_03 (BLE scan: 5s)
```

**Configuration**:
- **Root**: BLE disabled
- **Repeater**: BLE enabled (all Collectors discover it)
- **Collectors**: BLE scanning enabled, all find same Repeater

---

### Example 3: No BLE (Fixed WiFi Infrastructure)

```
┌──────────┐
│   ROOT   │ BLE: Disabled
│  Always  │ WiFi: Root_AP (fixed)
│    ON    │
└─────┬────┘
      │
      ↓
┌──────────┐
│ REPEATER │ BLE: Disabled
│  Always  │ WiFi: Repeater_AP (fixed)
│    ON    │
└─────┬────┘
      │
      ↓
┌──────────┐
│COLLECTOR │ BLE: Disabled
│  Connects│ WiFi: Direct to Repeater_AP (fixed SSID)
│  via WiFi│
└──────────┘
```

**Configuration**:
- **All nodes**: BLE disabled
- **All nodes**: Fixed WiFi SSIDs
- **Use case**: When all nodes are always on, no sleep

---

## Power Consumption Guide

### BLE Impact by Role

| Role      | BLE Status | Power (Typical) | Notes |
|-----------|------------|-----------------|-------|
| Root      | Disabled   | 80-120 mA       | WiFi only, always on |
| Root      | Enabled    | 90-130 mA       | Adds ~10 mA (unnecessary) |
| Repeater  | Disabled   | 80-120 mA       | WiFi only, always on |
| Repeater  | Enabled    | 20-30 mA        | **Light sleep + BLE** ✅ |
| Collector | Disabled   | <1 mA (sleep)   | Deep sleep, WiFi on wake |
| Collector | Enabled    | <1 mA (sleep)   | **+15-20 mA for 5s scan** ✅ |

### Power Optimization Tips

**Collector**:
- Lower `bleScanDurationSec` (3s) for faster scans
- Increase `collectorApCycleSec` (longer sleep between collections)
- BLE scan (5s @ 20mA) = 28 µAh vs WiFi scan (30s @ 100mA) = 833 µAh
- **BLE saves ~97% power** for parent discovery

**Repeater**:
- Enable BLE for instant wake-up (light sleep)
- Without BLE: Must stay awake (80-120 mA)
- With BLE: Light sleep (20-30 mA) = **75% power savings**

---

## Troubleshooting Configuration

### Collector doesn't find Repeater

**Check**:
1. ✅ Collector: BLE scanning enabled
2. ✅ Repeater: BLE beacon enabled
3. ✅ Both powered on
4. ✅ Distance <50m
5. ✅ Scan duration ≥ 5 seconds

**Fix**:
- Increase `bleScanDurationSec` to 10 seconds
- Move nodes closer together
- Check serial output: `[BLE-SCAN] Found mesh node...`

### Repeater not advertising

**Check**:
1. ✅ Repeater: BLE beacon checkbox enabled
2. ✅ Configuration saved and rebooted
3. ✅ Serial output shows: `[BLE-BEACON] Started advertising`

**Fix**:
- Re-enter config mode and verify BLE beacon is checked
- Use BLE scanner app (nRF Connect) to verify beacon visible

### Root wasting power

**Check**:
1. ❌ Root: BLE beacon should be disabled
2. Check serial: Should NOT show `[BLE-BEACON]` messages

**Fix**:
- Disable BLE beacon on Root (unchecked)
- Root is always accessible via WiFi

---

## Migration from Previous Version

If upgrading from version without BLE configuration:

1. **Enter configuration mode** on each node
2. **Re-save configuration** to persist BLE settings
3. **Defaults applied**:
   - Collector: BLE scanning enabled (5s)
   - Repeater: BLE beacon enabled
   - Root: BLE beacon disabled

No code changes needed - just re-save configuration once.

---

## Summary

### Key Configuration Points

✅ **Collector**: Enable BLE scanning + set scan duration  
✅ **Repeater**: Enable BLE beacon (continuous light sleep)  
❌ **Root**: Disable BLE beacon (always accessible via WiFi)

### Configuration Flow

1. Access web interface (hold BOOT on power-up)
2. Select node role
3. Configure BLE settings (checkboxes + scan duration)
4. Configure WiFi settings (SSIDs, passwords)
5. Save and reboot
6. Verify serial output shows BLE initialization

### Verification

**Repeater**:
```
[BLE-BEACON] Initializing BLE Beacon...
[BLE-BEACON] BLE Beacon initialized
[BLE-BEACON] Started advertising
```

**Collector**:
```
[BLE-MESH] Scanning for parent node...
[BLE-SCAN] Found mesh node: Repeater_01, RSSI: -45
[BLE-MESH] Found parent: Repeater_01 (Role: Repeater, RSSI: -45 dBm)
```

---

## Additional Resources

- **[BLE_TESTING_GUIDE.md](BLE_TESTING_GUIDE.md)** - Hardware testing procedures
- **[BLE_MESH_WAKEUP.md](BLE_MESH_WAKEUP.md)** - Architecture details
- **[BLE_IMPLEMENTATION_NOTES.md](BLE_IMPLEMENTATION_NOTES.md)** - Technical implementation

For questions or issues, refer to the troubleshooting sections in the testing guide.
