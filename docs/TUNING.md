# Runtime Tuning Parameters

The ESP32 multi-role node supports runtime tuning of various timing and behavior parameters through the ESP32 Preferences library. This allows you to adjust system behavior without recompiling the firmware.

## Overview

All tuning parameters are stored in the Preferences namespace `"tuning"` and are loaded at boot time. If no custom values are found, the system uses sensible defaults.

## Available Parameters

### HTTP Communication

| Parameter | Preferences Key | Default | Description |
|-----------|----------------|---------|-------------|
| `statusDelayMs` | `statusDelay` | 2000 ms | Delay before sending STATUS request to sensor |
| `configureDelayMs` | `configDelay` | 2000 ms | Delay before sending CONFIGURE request to sensor |
| `httpTimeoutMs` | `httpTimeout` | 5000 ms | HTTP request timeout |
| `httpRetries` | `httpRetries` | 3 | Number of retry attempts for HTTP requests |

### Firmware Updates

| Parameter | Preferences Key | Default | Description |
|-----------|----------------|---------|-------------|
| `firmwareLineDelayMs` | `fwLineDelay` | 10 ms | Delay between firmware hex lines |
| `fwProgressLogInterval` | `fwProgressInt` | 50 | Log progress every N lines |
| `fwRequireOkPerLine` | `fwRequireOk` | false | Validate "OK" response per line |

### Job Management

| Parameter | Preferences Key | Default | Description |
|-----------|----------------|---------|-------------|
| `jobCleanupAgeHours` | `jobCleanAge` | 24 hours | Age threshold for cleaning up .done files |

### Sensor Contexts

| Parameter | Preferences Key | Default | Description |
|-----------|----------------|---------|-------------|
| `sensorContextTimeoutMs` | `sensorTimeout` | 1800000 ms (30 min) | Timeout for purging inactive sensor contexts |

## How to Modify Parameters

### Method 1: Using Arduino Serial Monitor

You can create a simple sketch or add code to the existing firmware to write tuning parameters:

```cpp
#include <Preferences.h>

void setup() {
  Serial.begin(115200);
  Preferences prefs;
  
  if (prefs.begin("tuning", false)) {  // false = read-write mode
    // Example: Set HTTP timeout to 8 seconds
    prefs.putULong("httpTimeout", 8000);
    
    // Example: Enable firmware line validation
    prefs.putBool("fwRequireOk", true);
    
    // Example: Set firmware progress logging every 25 lines
    prefs.putInt("fwProgressInt", 25);
    
    prefs.end();
    Serial.println("Tuning parameters updated!");
  }
}

void loop() {
  // Nothing here
}
```

### Method 2: Using Web Interface (Future Enhancement)

A future enhancement could add a web interface to the configuration mode that allows setting these parameters through a browser.

### Method 3: Programmatically in Code

You can also set these at runtime within your application:

```cpp
extern RuntimeTuning g_tuning;

void adjustTuning() {
  // Override at runtime (not persisted)
  g_tuning.httpTimeoutMs = 10000;
  g_tuning.httpRetries = 5;
}
```

## Reading Current Values

To read the current values from Preferences:

```cpp
#include <Preferences.h>

void readTuning() {
  Preferences prefs;
  if (prefs.begin("tuning", true)) {  // true = read-only
    unsigned long timeout = prefs.getULong("httpTimeout", 5000);
    int retries = prefs.getInt("httpRetries", 3);
    
    Serial.printf("HTTP Timeout: %lu ms\n", timeout);
    Serial.printf("HTTP Retries: %d\n", retries);
    
    prefs.end();
  }
}
```

## Clearing All Tuning Parameters

To reset to defaults:

```cpp
#include <Preferences.h>

void resetTuning() {
  Preferences prefs;
  if (prefs.begin("tuning", false)) {
    prefs.clear();  // Clear all keys in the "tuning" namespace
    prefs.end();
    Serial.println("Tuning parameters reset to defaults!");
  }
}
```

## Best Practices

1. **Start with Defaults**: The default values are carefully chosen for typical deployments. Only adjust if you have specific requirements.

2. **Test Incrementally**: When adjusting parameters, change one at a time and test thoroughly before making additional changes.

3. **Document Changes**: Keep a record of any custom tuning parameters you set and why you set them.

4. **Monitor Performance**: After changing parameters, monitor system behavior (especially HTTP timeouts, firmware update success rates) to ensure the changes have the desired effect.

5. **Network Conditions**: Consider your network latency and reliability when setting timeouts and retries. Poor network conditions may require higher timeouts and more retries.

## Troubleshooting

### HTTP Requests Timing Out
- Increase `httpTimeoutMs` to 8000 or 10000 ms
- Increase `httpRetries` to 5

### Firmware Updates Failing
- Increase `firmwareLineDelayMs` to 50-100 ms if sensor can't keep up
- Enable `fwRequireOkPerLine` to validate each line
- Check `fwProgressLogInterval` logs to see where failures occur

### Too Many Sensor Contexts in Memory
- Reduce `sensorContextTimeoutMs` to purge inactive sensors sooner

### Config/Status Requests Failing
- Increase `statusDelayMs` and `configureDelayMs` if sensor needs more time to respond

## Example Tuning Scenarios

### Slow Network Environment
```cpp
prefs.putULong("httpTimeout", 10000);  // 10 seconds
prefs.putInt("httpRetries", 5);
prefs.putULong("statusDelay", 3000);
prefs.putULong("configDelay", 3000);
```

### Fast, Reliable Network
```cpp
prefs.putULong("httpTimeout", 3000);   // 3 seconds
prefs.putInt("httpRetries", 2);
prefs.putULong("fwLineDelay", 5);      // Faster firmware updates
```

### High-Reliability Firmware Updates
```cpp
prefs.putBool("fwRequireOk", true);
prefs.putInt("fwProgressInt", 10);     // More frequent progress logs
prefs.putULong("fwLineDelay", 20);     // Give sensor more time per line
```

## Notes

- All timing values are in milliseconds unless otherwise specified
- Changes to Preferences are persisted across reboots
- The system must be restarted for Preferences changes to take effect
- Invalid values in Preferences will cause the system to use defaults
