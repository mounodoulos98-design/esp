# Async Sensor Communication Flow

This document describes the async sensor communication flow implemented in the ESP32 Collector node, following the pattern of the Python sensordaemon.

## Overview

The ESP32 Collector operates in an async mode where it handles multiple sensors simultaneously through event-driven heartbeat processing. **No active polling** - sensors must send POST requests to the collector's heartbeat endpoint.

**Important:** Sensors must have firmware that sends heartbeat POST requests to `http://192.168.4.1:3000/event/heartbeat`. The collector does not actively poll sensors.

## Flow Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 COLLECTOR NODE                      │
│                                                              │
│  1. Opens AP (e.g., "SensorAP")                             │
│  2. Starts Heartbeat Server (port 3000)                     │
│  3. Waits for sensor connections                            │
│                                                              │
│  ┌───────────────────────────────────────────┐              │
│  │  Sensor connects to AP                    │              │
│  │  DHCP assigns IP (e.g., 192.168.4.2)      │              │
│  └───────────────────────────────────────────┘              │
│                     │                                        │
│                     ▼                                        │
│  ┌───────────────────────────────────────────┐              │
│  │  Sensor sends heartbeat POST              │              │
│  │  to /event/heartbeat                      │              │
│  │  Body: {                                  │              │
│  │    "sensor_sn": "324269",                 │              │
│  │    "heartbeats_after_measurement": 1      │              │
│  │  }                                        │              │
│  └───────────────────────────────────────────┘              │
│                     │                                        │
│                     ▼                                        │
│  ┌───────────────────────────────────────────┐              │
│  │  Heartbeat Manager processes event        │              │
│  │  - Extracts sensor_sn and IP              │              │
│  │  - Determines action based on value       │              │
│  └───────────────────────────────────────────┘              │
│         │                            │                       │
│         │ value = 1                  │ value > 1             │
│         ▼                            ▼                       │
│  ┌─────────────┐              ┌─────────────┐               │
│  │   STATUS    │              │    OTHER    │               │
│  │  Callback   │              │  Callback   │               │
│  └─────────────┘              └─────────────┘               │
│         │                            │                       │
│         ▼                            ▼                       │
│  Create FreeRTOS Task         Create FreeRTOS Task          │
│  ┌─────────────────┐          ┌─────────────────┐           │
│  │ StatusTask      │          │ JobTask         │           │
│  │ - Wait 2s       │          │ - Check jobs    │           │
│  │ - Send STATUS   │          │ - Execute FW    │           │
│  │ - Store result  │          │ - Execute CFG   │           │
│  └─────────────────┘          └─────────────────┘           │
│                                                              │
│  After timeout: Stop AP, go to deep sleep                   │
│                                                              │
│  Next wake: Connect to Root, upload status/data files       │
│             Receive new jobs from server                    │
└─────────────────────────────────────────────────────────────┘
```

## Implementation Details

### 1. AP Mode Setup (Collector State)

When the collector enters `STATE_COLLECTOR_AP`:
- Opens AP with configured SSID (e.g., "SensorAP")
- IP: 192.168.4.1
- Initializes SD card for job/data storage
- Starts AsyncWebServer on port 3000 for heartbeat reception
- Sets up station connection event handlers

### 2. Heartbeat Reception

Sensors send POST requests to `/event/heartbeat` with JSON body:
```json
{
  "sensor_sn": "324269",
  "heartbeats_after_measurement": 1
}
```

The `SensorHeartbeatManager` receives these asynchronously and:
- Extracts sensor S/N from JSON
- Gets sensor IP from HTTP request (client remote IP)
- Calls appropriate callback based on `heartbeats_after_measurement` value

### 3. STATUS Heartbeat Processing (value = 1)

When `heartbeats_after_measurement = 1`:

**Callback Flow:**
```cpp
heartbeatManager.onStatus([](const SensorHeartbeatContext& ctx) {
  // Create async task to avoid blocking
  xTaskCreate(StatusTask, ...);
});
```

**StatusTask does:**
1. Waits 2 seconds (like sensordaemon)
2. Sends HTTP GET: `http://{ip}/api?command=STATUS&datetime={ms}&`
3. Parses response to extract full status:
   - S/N verification
   - Battery voltage
   - Firmware version
   - RSSI/WiFi signal
   - Temperature
4. Stores status to SD card: `/received/status_{sn}_{timestamp}.txt`
5. Deletes task when done

### 4. OTHER Heartbeat Processing (value > 1)

When `heartbeats_after_measurement > 1`:

**Callback Flow:**
```cpp
heartbeatManager.onOther([](const SensorHeartbeatContext& ctx) {
  // Create async task for job execution
  xTaskCreate(JobTask, ...);
});
```

**JobTask does:**
1. Looks for jobs in SD card JSON files:
   - `/jobs/firmware_jobs.json` (checked first)
   - `/jobs/config_jobs.json` (checked second)
2. Searches for matching sensor S/N
3. If firmware job found:
   - Executes firmware update via `executeFirmwareJob()`
   - Sends bootloader command if needed
   - Uploads hex file line by line
   - Removes job from JSON file when done
4. If config job found:
   - Executes configuration update via `cu_sendConfiguration()`
   - Sends CONFIGURE command with parameters
   - Removes job from JSON file when done
5. Deletes task when done

### 5. Job File Format

**Firmware Jobs** (`/jobs/firmware_jobs.json`):
```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/vibration_sensor_app_v1.17.hex",
      "max_lines": 0,
      "timeout_ms": 480000
    }
  ]
}
```

**Config Jobs** (`/jobs/config_jobs.json`):
```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
        "sleep_time_after_sec_Station_mode": 120,
        "wakeup_every_min": 1,
        "temp_threshold": 45,
        "send_heartbeat_every_sec": 10
      }
    }
  ]
}
```

### 6. Async Task Advantages

Using FreeRTOS tasks provides:
- **Non-blocking**: Main loop continues processing other sensors
- **Concurrent execution**: Multiple sensors can be handled simultaneously
- **Isolated context**: Each sensor gets its own task with dedicated stack
- **Clean termination**: Tasks delete themselves when done

### 7. Sleep and Wake Cycle

After the AP window or timeout:
1. Collector stops AP
2. Goes to deep sleep
3. Wakes at scheduled time for uplink window
4. Connects to Root AP
5. Uploads files from `/queue` directory:
   - Status files
   - Measurement data
   - Heartbeat logs
6. Root forwards to server
7. Server can add new job files via HTTP
8. Collector downloads new jobs
9. Cycle repeats

## Comparison with Python Sensordaemon

| Aspect | Python Sensordaemon | ESP32 Collector |
|--------|-------------------|-----------------|
| Threading | Python threads | FreeRTOS tasks |
| STATUS command | Thread per sensor | Task per heartbeat |
| Job execution | Thread-based | Task-based async |
| Multiple sensors | Sequential threads | Concurrent tasks |
| Delay before STATUS | 2 seconds | 2 seconds (same) |
| S/N to IP mapping | From STATUS response | From STATUS response |
| Job storage | Database | SD card JSON files |
| Data upload | HTTP to server | HTTP to Root node |

## Key Features

1. **Truly Async**: Uses FreeRTOS tasks for concurrent sensor handling
2. **Event-driven**: Reacts to sensor heartbeats rather than polling
3. **Scalable**: Can handle multiple sensors simultaneously
4. **Efficient**: Sensors drive the communication, collector responds
5. **Compatible**: Follows same protocol as Python sensordaemon
6. **Persistent**: Jobs stored on SD card survive power cycles
7. **Low power**: Deep sleep between AP windows

## Configuration Parameters

In `config.h` / NodeConfig:
- `collectorApCycleSec`: Period between AP openings (default: 120s)
- `collectorApWindowSec`: Max AP window if no sensors connect (default: 1200s)
- `collectorDataTimeoutSec`: Inactivity timeout after sensor connects (default: 1200s)
- `meshIntervalMin`: Uplink interval to Root (default: 15 min)
- `meshWindowSec`: Uplink window duration (default: 60s)

## Files Modified

1. `ShipRepeaterNode/op_mode.cpp`:
   - Modified STATUS callback to execute STATUS command via task
   - Modified OTHER callback to execute jobs via task
   - Added async task creation for non-blocking execution

2. `ShipRepeaterNode/station_job_manager.h`:
   - Exported `sjm_requestStatus()` for async STATUS requests
   - Exported `processJobsForSN()` for async job execution

3. `ShipRepeaterNode/station_job_manager.cpp`:
   - Removed `static` from exported functions
   - Functions now callable from async tasks

## Testing Recommendations

1. **Single Sensor Test**:
   - Connect one sensor to collector AP
   - Verify heartbeat reception
   - Check STATUS command execution
   - Verify status file creation

2. **Multiple Sensor Test**:
   - Connect 2-3 sensors simultaneously
   - Verify concurrent task creation
   - Check that all sensors get processed
   - Monitor task stack usage

3. **Job Execution Test**:
   - Create firmware/config job files
   - Send OTHER heartbeat (value > 1)
   - Verify job execution
   - Check job removal from JSON

4. **Sleep/Wake Cycle Test**:
   - Complete AP window
   - Verify deep sleep entry
   - Verify wake at correct time
   - Check uplink window execution

## Troubleshooting

**Heartbeats not received:**
- Check sensor connects to correct AP SSID
- Verify port 3000 is accessible
- Check AsyncWebServer is started

**STATUS command fails:**
- Verify sensor has valid IP (not 0.0.0.0)
- Check 2-second delay is applied
- Verify sensor responds to /api?command=STATUS

**Jobs not executing:**
- Check job JSON files exist in /jobs/
- Verify sensor S/N matches job S/N exactly
- Check SD card is initialized properly

**Tasks crash:**
- Increase task stack size (currently 4096 for STATUS, 8192 for jobs)
- Check for null pointer dereferences
- Monitor heap fragmentation

## Future Enhancements

1. Add job priority queue
2. Implement job retry mechanism
3. Add sensor status cache
4. Implement heartbeat log aggregation
5. Add job scheduling based on time windows
6. Implement job dependencies
