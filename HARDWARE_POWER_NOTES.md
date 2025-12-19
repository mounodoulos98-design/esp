# Hardware Power Consumption Notes

## ESP32 Power Consumption by Board Type

The dynamic WiFi AP control implementation achieves different power savings depending on the ESP32 board used. This document explains the differences and recommendations.

---

## Board Comparison

### ⭐ Adafruit ESP32 Feather V2 (Recommended)

**Specifications:**
- Chip: ESP32-PICO-V3-02
- Flash: 8 MB
- PSRAM: 2 MB
- USB: Type C
- Features: Optimized power design, STEMMA QT with controllable power

**Power Consumption (Verified by Adafruit with Nordic PPK):**
- **Deep sleep:** 70 µA (0.07 mA)
- **Light sleep:** 1.2 mA
- **BLE active:** +1-3 mA
- **WiFi active:** +80-120 mA

**Light Sleep with BLE Beacon:**
```
Base light sleep:     1.2 mA
BLE beacon overhead:  1-3 mA
─────────────────────────────
Total:               2-5 mA ⭐
```

**Daily Power (1 hour WiFi active, 23 hours light sleep):**
```
Light sleep: 3 mA × 23h = 69 mAh
WiFi active: 130 mA × 1h = 130 mAh
─────────────────────────────────
Total per day:        199 mAh
```

**Battery Life (1000 mAh LiPo):**
- ~5 days continuous operation
- Savings vs always-on: **94.5%**

---

### Generic ESP32 DevKit (NOT Optimized)

**Common Issues:**
- Inefficient voltage regulators (LDO with high quiescent current)
- Always-on power LEDs (5-10 mA each)
- USB-to-serial chip always powered (CH340, CP2102)
- No power management features
- Larger ESP32-WROOM module

**Power Consumption (Typical):**
- Deep sleep: 1-5 mA (due to regulator)
- Light sleep: 5-10 mA (base)
- BLE active: +5-10 mA (higher overhead)
- WiFi active: +100-150 mA

**Light Sleep with BLE Beacon:**
```
Base light sleep:     5-10 mA
BLE beacon overhead:  5-10 mA
Always-on LEDs:       5 mA
USB chip:            5 mA
─────────────────────────────
Total:               20-30 mA
```

**Note:** Can be improved by:
- Removing power LEDs (desolder)
- Cutting USB power trace
- Using external 3.3V supply
- With modifications: ~10-15 mA achievable

**Daily Power (1 hour WiFi active, 23 hours light sleep):**
```
Light sleep: 15 mA × 23h = 345 mAh
WiFi active: 130 mA × 1h = 130 mAh
─────────────────────────────────
Total per day:        475 mAh
```

**Battery Life (1000 mAh LiPo):**
- ~2 days continuous operation
- Savings vs always-on: **87%**

---

## Why Such Different Numbers?

### Voltage Regulator Efficiency

**Feather V2:**
- Uses AP2112K-3.3 LDO (quiescent current: 55 µA)
- Efficient at low currents
- Dedicated 3.3V rail for peripherals

**Generic DevKit:**
- Often uses AMS1117-3.3 LDO (quiescent current: 5 mA!)
- Wastes 5 mA just existing
- Single rail for everything

### Always-On Components

**Feather V2:**
- NeoPixel with controllable power pin (can turn off)
- No power LED (or very dim)
- USB chip powered via USB VBUS only

**Generic DevKit:**
- Power LED: 2-5 mA continuous
- Sometimes 2x LEDs (power + RX/TX)
- USB chip always powered

### BLE Implementation

**Optimized Design:**
- BLE shares WiFi radio efficiently
- Low advertising interval possible
- Good antenna design

**Generic Design:**
- BLE may have higher overhead
- Poor antenna placement
- More current draw for same range

---

## Recommendations by Use Case

### For Battery-Powered Repeater (Optimal Power)

**Hardware:**
- ✅ **Adafruit ESP32 Feather V2** (best choice)
- ✅ Sparkfun ESP32 Thing Plus (also good)
- ⚠️ Custom board with proper power design
- ❌ Generic DevKit (last resort)

**Why:**
- 2-5 mA in light sleep = 5+ days on 1000 mAh battery
- Worth the extra cost ($25 vs $10)
- Professional power management

### For Mains-Powered Repeater

**Hardware:**
- ✅ Any ESP32 board works fine
- ✅ Generic DevKit acceptable
- ✅ No need for premium power features

**Why:**
- Power consumption doesn't matter (always plugged in)
- Save money with generic boards
- Still get WiFi AP on-demand feature

### For Solar-Powered Repeater

**Hardware:**
- ✅ **Adafruit ESP32 Feather V2** (required)
- ✅ With 100mA solar panel + 500mAh LiPo
- ⚠️ Generic DevKit needs larger panel (250mA+)

**Why:**
- 3 mA average = 72 mAh/day
- Small panel (3.5V × 100mA = 350mW) sufficient
- Generic board needs 3x larger panel

---

## Measuring Your Board

### Equipment Needed
- Nordic Power Profiler Kit (PPK2) - recommended
- OR INA219 current sensor module
- OR Multimeter in series with VCC

### Test Procedure

#### 1. Baseline (WiFi OFF, BLE OFF)
```
1. Flash firmware
2. Disable BLE: config.bleBeaconEnabled = false;
3. Ensure WiFi OFF
4. Measure for 60 seconds
```

**Expected:**
- Feather V2: ~1.2 mA
- DevKit: 5-15 mA

#### 2. BLE Beacon Only
```
1. Enable BLE: config.bleBeaconEnabled = true;
2. Keep WiFi OFF
3. Measure for 60 seconds
```

**Expected:**
- Feather V2: 2-5 mA
- DevKit: 10-25 mA

#### 3. WiFi AP Active
```
1. Connect Collector
2. Measure during transfer
```

**Expected:**
- All boards: 80-150 mA (ESP32 WiFi is power-hungry)

### Interpreting Results

| Baseline (no BLE) | BLE Active | Board Quality |
|-------------------|------------|---------------|
| < 2 mA | < 5 mA | ⭐⭐⭐ Excellent |
| 2-5 mA | 5-10 mA | ⭐⭐ Good |
| 5-10 mA | 10-20 mA | ⭐ Acceptable |
| > 10 mA | > 20 mA | ⚠️ Poor (needs mods) |

---

## Improving Generic DevKit Power

### Hardware Modifications

#### 1. Remove Power LED
```
Effect: -2 to -5 mA
Difficulty: Easy (desolder LED)
```

#### 2. Cut USB Power Trace
```
Effect: -5 to -10 mA
Difficulty: Medium (find and cut trace)
Warning: USB won't work after
```

#### 3. Replace Voltage Regulator
```
Effect: -5 mA
Difficulty: Hard (SMD soldering)
Replace: AMS1117 → AP2112K
```

#### 4. External 3.3V Supply
```
Effect: Best possible (bypasses regulator)
Difficulty: Easy (wire to 3.3V pin)
Warning: Don't use USB simultaneously
```

### Software Optimizations

```cpp
// Reduce BLE advertising interval (already in code)
pAdvertising->setInterval(160);  // 100ms intervals

// Disable unnecessary features
WiFi.setSleep(WIFI_PS_MAX_MODEM);  // Max sleep between packets

// Reduce CPU frequency (if possible)
setCpuFrequencyMhz(80);  // From 240 MHz
```

**Expected Improvement:** 2-5 mA reduction

---

## Power Budget Examples

### Example 1: Feather V2 - Solar Powered

**Requirements:**
- Light sleep: 3 mA average
- WiFi active: 1 hour/day
- Location: Full sun 6 hours/day

**Power Budget:**
```
Daily consumption:
- Light sleep: 3 mA × 23h = 69 mAh
- WiFi active: 130 mA × 1h = 130 mAh
- Total: 199 mAh/day

Solar generation (100mA panel, 6h sun):
- Generated: 100 mA × 6h = 600 mAh/day
- Efficiency: 70% = 420 mAh/day usable

Margin: 420 - 199 = 221 mAh/day surplus ✅
```

**Battery sizing:**
- 3 days backup: 199 × 3 = 600 mAh minimum
- Recommended: 1000 mAh (5 days backup)

### Example 2: DevKit - Battery Powered

**Requirements:**
- Light sleep: 15 mA average
- WiFi active: 1 hour/day
- Battery: 2500 mAh

**Power Budget:**
```
Daily consumption:
- Light sleep: 15 mA × 23h = 345 mAh
- WiFi active: 130 mA × 1h = 130 mAh
- Total: 475 mAh/day

Battery life:
- 2500 mAh / 475 mAh/day = 5.3 days

With modifications (10 mA light sleep):
- 230 + 130 = 360 mAh/day
- 2500 / 360 = 6.9 days ✅
```

---

## Conclusion

**Best Practice:**
- Use **Adafruit ESP32 Feather V2** for battery/solar operation
- Achieve **2-5 mA** light sleep with BLE active
- Get **94.5% power savings** vs always-on WiFi
- 5+ days on 1000 mAh battery

**Budget Option:**
- Generic DevKit can work but needs modifications
- Expect **10-20 mA** light sleep (2-3x higher)
- Still get **87% savings** vs always-on
- 2-3 days on 1000 mAh battery

**The Hardware Matters:**
- Power optimization is 80% hardware, 20% software
- Good board design cannot be fixed in code
- Premium boards pay for themselves in battery/solar costs

---

## References

- [Adafruit ESP32 Feather V2 Datasheet](https://learn.adafruit.com/adafruit-esp32-feather-v2)
- [ESP32 Power Consumption Analysis](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [Nordic PPK2 Power Profiler](https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2)
