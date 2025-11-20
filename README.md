# ESP32 Multi-Role Node (Ship Repeater/Collector/Root)

Advanced ESP32 firmware for a multi-role wireless sensor network supporting three operational modes: Root, Repeater, and Collector.

## Features

- **Multi-Role Support**: Root, Repeater, or Collector modes configurable at runtime
- **Sensor Communication**: HTTP-based sensor status, configuration, and firmware updates
- **Job System**: JSON-based job queue for firmware and configuration updates
- **Heartbeat Management**: Real-time sensor heartbeat tracking with automatic context cleanup
- **Data Collection**: Collector AP mode for sensor connections and data gathering
- **HTTP Utilities**: Unified HTTP client with retry logic and timeout handling
- **Runtime Tuning**: Adjustable parameters via Preferences (no recompile needed)
- **SD Card Storage**: Job files, sensor data, and queued uploads
- **Logging System**: Multi-level logging (ERROR, WARN, INFO, DEBUG)
- **URL Encoding**: Safe parameter encoding for HTTP requests

## Architecture

### Node Roles

#### Root
- Acts as the network gateway
- Provides `/time` endpoint for network time synchronization
- Accepts file uploads via `/ingest` endpoint
- Serves as the ultimate data collection point

#### Repeater
- Extends network coverage
- Relays time synchronization from Root
- Forwards sensor data upstream
- Maintains mesh connectivity

#### Collector
- Opens AP for direct sensor connections
- Executes firmware and configuration jobs
- Collects sensor data
- Uploads data during uplink windows

## New Modules (Refactoring)

### HTTP Utilities (`http_utils.h/cpp`)
Unified HTTP communication module that replaces duplicated code across the project:

- `httpGet()`: HTTP GET with configurable retries and timeout
- `httpMultipartPostFile()`: Multipart/form-data file uploads
- `urlEncode()`: Safe URL parameter encoding

**Benefits:**
- Single point of maintenance for HTTP logic
- Consistent retry behavior across all requests
- Better error handling and logging
- Reduced code duplication

### Logging System (`logging.h`)
Simple macro-based logging with multiple levels:

```cpp
LOG_ERROR("TAG", "Error message: %d", errorCode);
LOG_WARN("TAG", "Warning message");
LOG_INFO("TAG", "Info message");
LOG_DEBUG("TAG", "Debug message");  // Compiled only if ENABLE_DEBUG_LOG is set
```

### Runtime Tuning (`tuning.h`)
Configurable parameters stored in ESP32 Preferences:

- HTTP timeouts and retries
- Firmware update delays
- Job cleanup intervals
- Sensor context timeouts

See [docs/TUNING.md](docs/TUNING.md) for complete documentation.

## Building and Flashing

This is an Arduino-based project for ESP32. It can be built using:
- Arduino IDE with ESP32 board support
- PlatformIO
- arduino-cli

### Required Libraries
- painlessMesh
- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson
- SdFat
- Adafruit_NeoPixel
- DNSServer (built-in)
- WiFi (built-in)
- Preferences (built-in)

### Hardware Requirements
- ESP32 board (tested on ESP32-S3)
- SD card module (CS pin 27)
- NeoPixel LED (pin 0, power pin 2)
- Boot button (pin 38)

## Configuration

### Initial Setup
1. Flash firmware to ESP32
2. Hold boot button during power-on to enter configuration mode
3. Connect to `Repeater_Setup_XXXXXX` AP
4. Configure via web interface:
   - Node role (Root/Repeater/Collector)
   - Network credentials
   - AP settings
   - Schedule parameters

### Runtime Tuning
Adjust operational parameters without reflashing. See [docs/TUNING.md](docs/TUNING.md).

## Job System

### Firmware Jobs (`/jobs/firmware_jobs.json`)
```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/sensor_v1.17.hex",
      "max_lines": 0,
      "timeout_ms": 480000,
      "line_rate_limit_ms": 10
    }
  ]
}
```

**Schema Validation:**
- `sn` (required): Sensor serial number
- `hex_path` (required): Path to Intel HEX file on SD card
- `max_lines` (optional): Limit lines (0 = all)
- `timeout_ms` (optional): Total timeout (default: 8 minutes)
- `line_rate_limit_ms` (optional): Per-line delay override

**Features:**
- Progress logging every N lines (configurable)
- Per-line response validation (optional)
- Cumulative duration tracking
- Predictive timeout detection
- Automatic retry with exponential backoff

### Configuration Jobs (`/jobs/config_jobs.json`)
```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
        "sleep_duration": "300",
        "sample_rate": "1000",
        "sensor_name": "Vibration Sensor 01"
      }
    }
  ]
}
```

**Features:**
- URL encoding of all parameters (handles spaces, special chars)
- Support for numeric and string values
- Automatic job removal after execution

## State Machine

### Collector State Flow
1. `STATE_INITIAL`: Boot and initialization
2. `STATE_COLLECTOR_AP`: Open AP for sensor connections
   - Cleanup stale .done files
   - Process heartbeats
   - Execute jobs
   - Purge old sensor contexts
3. `STATE_MESH_APPOINTMENT`: Uplink window
   - Upload queued data
   - Sync time
   - Sleep on empty queue

## Sensor Integration

### Heartbeat Protocol
Sensors POST to `/event/heartbeat` (port 3000):
```json
{
  "sensor_sn": "324269",
  "heartbeats_after_measurement": 1,
  "firmware_version": "1.17"
}
```

**Response:**
- `heartbeats_after_measurement == 1`: Triggers STATUS request
- `heartbeats_after_measurement > 1`: Triggers CONFIG/FW jobs

**Context Tracking:**
- Sensor S/N and IP address
- Last heartbeat timestamp
- Last action (STATUS/OTHER)
- Firmware version (optional)
- Automatic purging of inactive sensors

## Safety Features

### SD Card Access
- Mutex protection for concurrent access
- Large file warnings (>14KB JSON)
- Automatic cleanup of stale job files

### HTTP Communication
- Configurable timeouts and retries
- Exponential backoff
- Connection validation
- Response body validation (optional)

### Firmware Updates
- Line-by-line validation
- Progress tracking
- Predictive timeout detection
- Watchdog timer resets
- Detailed statistics logging

### Sleep Management
- RTC state persistence
- Early sleep on empty queue
- Configurable timeouts
- Safe AP teardown

## Logging

The system uses a structured logging approach:

```
[ERROR][TAG] Critical error message
[WARN][TAG] Warning message
[INFO][TAG] Informational message
[DEBUG][TAG] Debug details (if ENABLE_DEBUG_LOG defined)
```

To enable debug logs, define `ENABLE_DEBUG_LOG` before including `logging.h`.

## File Structure

```
ShipRepeaterNode/
├── ShipRepeaterNode.ino       # Main entry point
├── config.h                    # Hardware and network configuration
├── config_mode.cpp             # Web-based configuration interface
├── op_mode.cpp                 # Operational mode state machine
├── firmware_updater.h/cpp      # Firmware update logic
├── config_updater.h/cpp        # Configuration update logic
├── station_job_manager.h/cpp   # Job processing and station management
├── sensor_heartbeat_manager.h  # Heartbeat handling
├── http_utils.h/cpp            # HTTP communication utilities (NEW)
├── logging.h                   # Logging macros (NEW)
├── tuning.h                    # Runtime tuning parameters (NEW)
├── status_led.h/cpp            # NeoPixel status indication
└── storage.cpp                 # SD card and configuration storage
```

## API Endpoints

### Root/Repeater Endpoints (Port 8080)
- `GET /health`: Health check
- `GET /time`: Time synchronization (returns epoch)
- `POST /ingest`: File upload (multipart/form-data)

### Collector Sensor Endpoint (Port 3000)
- `POST /event/heartbeat`: Sensor heartbeat with JSON body

### Sensor Communication (Port 80 on sensor)
- `GET /api?command=STATUS&datetime=<ms>`: Request sensor status
- `GET /api?command=CONFIGURE&datetime=<ms>&param1=value1&...`: Configure sensor
- `GET /api?command=FIRMWARE_UPDATE&hex=<line>&d=0`: Push firmware line

## Troubleshooting

### HTTP Requests Failing
1. Check network connectivity
2. Increase timeout in tuning parameters
3. Enable DEBUG logging for detailed info
4. Verify sensor responds to simple requests

### Firmware Updates Incomplete
1. Check progress logs for failure point
2. Verify HEX file format (Intel HEX)
3. Increase `firmwareLineDelayMs` if sensor is slow
4. Enable `fwRequireOkPerLine` validation
5. Check SD card for corrupted HEX file

### Sensors Not Connecting
1. Verify AP is active (check LED status)
2. Check SSID and password configuration
3. Review station connection events in logs
4. Verify sensor is in range

### Jobs Not Executing
1. Check job file format (valid JSON)
2. Verify sensor S/N matches
3. Check SD card mounted correctly
4. Review job validation errors in logs

## Future Enhancements

- NTP time synchronization fallback
- Job priority and concurrency control
- Cryptographic signature validation for firmware
- TLS/encryption support
- Asynchronous HTTP client
- Web-based tuning interface
- Real-time job status dashboard

## Contributing

When making changes:
1. Follow existing code style
2. Use the logging macros
3. Add appropriate error handling
4. Update documentation
5. Test all three node roles
6. Run builds before committing

## License

[Add license information here]

## Authors

[Add author information here]