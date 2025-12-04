# ESP32 ShipRepeaterNode with Async Sensor Communication

This repository contains the ESP32 firmware for a Ship Repeater Node that acts as a Collector for wireless vibration sensors. The implementation uses async communication patterns to handle multiple sensors simultaneously.

## Features

- **Async Sensor Communication**: Handle multiple sensors concurrently using FreeRTOS tasks
- **Heartbeat-Driven Architecture**: Event-driven processing based on sensor heartbeats
- **BLE Mesh Wake-Up**: Bluetooth Low Energy beacons for efficient parent discovery and wake-up
- **Job Execution System**: 
  - Firmware updates via Intel HEX over HTTP
  - Configuration updates via HTTP API
  - Job matching by sensor serial number (S/N)
- **Data Management**:
  - Heartbeat logging to CSV
  - Status file storage
  - Queue system for upload to root node
- **Sleep/Wake Cycles**: Deep sleep between AP windows for power efficiency
- **Multi-Role Support**: ROOT, REPEATER, and COLLECTOR modes

## Quick Start

### Hardware Requirements
- ESP32 development board (e.g., ESP32-S3)
- SD card module and card
- NeoPixel LED (optional, for status indication)

### Software Requirements
- Arduino IDE with ESP32 board support
- Required libraries:
  - painlessMesh
  - ESPAsyncWebServer
  - ArduinoJson
  - SdFat
  - Adafruit_NeoPixel

### Installation
1. Clone this repository
2. Open `ShipRepeaterNode/ShipRepeaterNode.ino` in Arduino IDE
3. Install required libraries via Library Manager
4. Select your ESP32 board in Tools → Board
5. Upload to your ESP32

### Configuration
First boot will enter Configuration Mode. Set up the node role:
- **ROOT**: Acts as central hub, no sleep
- **REPEATER**: Relays time sync and data, sleeps between windows
- **COLLECTOR**: Collects sensor data, executes jobs, sleeps between cycles

## Documentation

- **[ASYNC_SENSOR_FLOW.md](ASYNC_SENSOR_FLOW.md)** - Complete async communication flow with diagrams
- **[BLE_MESH_WAKEUP.md](BLE_MESH_WAKEUP.md)** - BLE mesh wake-up system for parent discovery
- **[IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)** - Detailed implementation and design decisions  
- **[EXAMPLE_JOB_FILES.md](EXAMPLE_JOB_FILES.md)** - Example job files and testing guide
- **[README_FIXES.txt](ShipRepeaterNode/README_FIXES.txt)** - Build and timing fixes

## How It Works

### Collector Mode Flow

```
1. Collector opens AP (e.g., "SensorAP")
2. Starts heartbeat server on port 3000
3. Sensors connect and get IP via DHCP
4. Sensors send heartbeat POST to /event/heartbeat
   {
     "sensor_sn": "324269",
     "heartbeats_after_measurement": 1
   }
5. Collector processes heartbeat:
   - If value=1: Send STATUS command, save status
   - If value>1: Check for jobs, execute if found
6. All processing done in async FreeRTOS tasks
7. After timeout: Stop AP, enter deep sleep
8. Wake up: Connect to root, upload queued files
9. Repeat cycle
```

### Job Execution

Jobs are stored in JSON files on SD card:

**Firmware Update** (`/jobs/firmware_jobs.json`):
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

**Configuration Update** (`/jobs/config_jobs.json`):
```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
        "temp_threshold": 45,
        "vibration_threshold_in_mg": 800
      }
    }
  ]
}
```

Jobs are matched by sensor serial number and executed when the sensor sends a heartbeat with `heartbeats_after_measurement > 1`.

## File Structure

```
esp/
├── ShipRepeaterNode/
│   ├── ShipRepeaterNode.ino      # Main Arduino sketch
│   ├── config.h                   # Configuration and definitions
│   ├── op_mode.cpp                # Operational mode (ROOT/REPEATER/COLLECTOR)
│   ├── config_mode.cpp            # Configuration mode
│   ├── station_job_manager.*      # Job execution logic
│   ├── firmware_updater.*         # Firmware update over HTTP
│   ├── config_updater.*           # Configuration update over HTTP
│   ├── sensor_heartbeat_manager.h # Heartbeat event handling
│   ├── status_led.*               # Status LED control
│   └── storage.cpp                # SD card and preferences
├── ASYNC_SENSOR_FLOW.md           # Flow documentation
├── IMPLEMENTATION_SUMMARY.md      # Implementation details
├── EXAMPLE_JOB_FILES.md          # Job file examples
└── README.md                      # This file
```

## SD Card Structure

```
/jobs/
├── firmware_jobs.json    # Pending firmware updates
└── config_jobs.json      # Pending config updates

/firmware/
└── *.hex                 # Intel HEX firmware files

/received/
├── heartbeat_api.csv     # All heartbeat logs
└── status_*.txt          # Status files per sensor

/queue/
└── entry_*.bin           # Files queued for upload
```

## Testing

1. **Single Sensor Test**:
   - Configure node as COLLECTOR
   - Create a config job in `/jobs/config_jobs.json`
   - Connect a sensor
   - Sensor sends heartbeat with value > 1
   - Verify job execution in serial monitor

2. **Multiple Sensors Test**:
   - Create jobs for 3 sensors
   - All sensors connect simultaneously
   - Send heartbeats with different timing
   - Verify concurrent execution

See [EXAMPLE_JOB_FILES.md](EXAMPLE_JOB_FILES.md) for detailed testing instructions.

## Troubleshooting

### Heartbeats not received
- Verify sensor connects to correct AP SSID
- Check AsyncWebServer started on port 3000
- Verify sensor IP is not 0.0.0.0

### Jobs not executing
- Check S/N matches exactly in job file
- Verify `heartbeats_after_measurement > 1`
- Check SD card initialized: `[SD] Card initialized successfully`
- Validate JSON syntax

### Firmware update fails
- Verify hex file exists at specified path
- Check hex file is valid Intel HEX format
- Ensure timeout is sufficient for file size
- Verify sensor in bootloader mode

See [ASYNC_SENSOR_FLOW.md](ASYNC_SENSOR_FLOW.md) for complete troubleshooting guide.

## Performance

- **Concurrent sensors**: 10-15 (limited by available heap)
- **Heartbeat response**: <10ms
- **STATUS command**: ~2.5 seconds (including 2s delay)
- **Config update**: ~2.2 seconds
- **Firmware update**: 30s - 5min (depends on hex file size)

## Contributing

This is a project for ship vibration monitoring. Contributions welcome!

## License

[Your license here]

## Credits

Inspired by the Python sensordaemon implementation, adapted for ESP32 with FreeRTOS async architecture.