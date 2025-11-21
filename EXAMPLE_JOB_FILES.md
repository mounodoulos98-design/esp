# Example Job Files

This document provides example job files that can be placed on the SD card to test the async sensor communication system.

## Directory Structure on SD Card

Create these directories on the SD card:
```
/jobs/           # Job files
/firmware/       # Firmware hex files
/received/       # Status and heartbeat logs (auto-created)
/queue/          # Upload queue (auto-created)
```

## 1. Firmware Update Job

**File**: `/jobs/firmware_jobs.json`

```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/vibration_sensor_app_v1.17.hex",
      "max_lines": 0,
      "timeout_ms": 480000
    },
    {
      "sn": "324270",
      "hex_path": "/firmware/vibration_sensor_app_v1.18.hex",
      "max_lines": 0,
      "timeout_ms": 600000
    }
  ]
}
```

**Fields**:
- `sn`: Sensor serial number (e.g., "324269")
- `hex_path`: Path to Intel HEX firmware file on SD card
- `max_lines`: Maximum lines to send (0 = all lines)
- `timeout_ms`: Total timeout in milliseconds (default: 8 minutes)

**Notes**:
- Job will be removed from file after execution
- Firmware file must exist at specified path
- Sensor must be in bootloader mode (or will be put into it)

## 2. Configuration Update Job

**File**: `/jobs/config_jobs.json`

```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
        "name": "Sensor-Bridge-01",
        "sleep_time_after_sec_Station_mode": 120,
        "wakeup_every_min": 1,
        "temp_threshold": 45,
        "send_heartbeat_every_sec": 10,
        "start_time_for_accel_data_sec": 0,
        "send_accel_data_every_min": 5,
        "vibration_threshold_in_mg": 800,
        "vibration_threshold_frequency_in_Hz": 25,
        "acc_data_measure_time": 5000,
        "accel_full_scale": 16
      }
    },
    {
      "sn": "324270",
      "params": {
        "name": "Sensor-Engine-02",
        "sleep_time_after_sec_Station_mode": 60,
        "wakeup_every_min": 5,
        "temp_threshold": 50,
        "send_heartbeat_every_sec": 15,
        "vibration_threshold_in_mg": 1000,
        "acc_data_measure_time": 10000
      }
    }
  ]
}
```

**Fields**:
- `sn`: Sensor serial number
- `params`: Object containing configuration parameters
  - `name`: Sensor hostname (optional)
  - `sleep_time_after_sec_Station_mode`: Sleep duration in station mode (seconds)
  - `wakeup_every_min`: Wake interval (minutes)
  - `temp_threshold`: Temperature threshold (°C)
  - `send_heartbeat_every_sec`: Heartbeat interval (seconds)
  - `start_time_for_accel_data_sec`: Delay before acceleration data (seconds)
  - `send_accel_data_every_min`: Acceleration data transmission interval (minutes)
  - `vibration_threshold_in_mg`: Vibration threshold (milligravity)
  - `vibration_threshold_frequency_in_Hz`: Vibration frequency threshold (Hz)
  - `acc_data_measure_time`: Acceleration measurement duration (milliseconds)
  - `accel_full_scale`: Accelerometer full scale (g)

**Notes**:
- All parameters are optional (only specified ones will be updated)
- Job will be removed from file after execution
- Sensor receives HTTP GET with all params as query string

## 3. Combined Jobs (Multiple Sensors)

You can have jobs for multiple sensors in the same file:

**File**: `/jobs/firmware_jobs.json`
```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/vibration_sensor_app_v1.17.hex",
      "max_lines": 0,
      "timeout_ms": 480000
    },
    {
      "sn": "324270", 
      "hex_path": "/firmware/vibration_sensor_app_v1.17.hex",
      "max_lines": 0,
      "timeout_ms": 480000
    },
    {
      "sn": "324271",
      "hex_path": "/firmware/vibration_sensor_app_v1.17.hex",
      "max_lines": 0,
      "timeout_ms": 480000
    }
  ]
}
```

**File**: `/jobs/config_jobs.json`
```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
        "temp_threshold": 45,
        "vibration_threshold_in_mg": 800
      }
    },
    {
      "sn": "324270",
      "params": {
        "temp_threshold": 50,
        "vibration_threshold_in_mg": 1000
      }
    }
  ]
}
```

## 4. Job Execution Priority

Jobs are executed in this order:
1. **Firmware updates** (from `firmware_jobs.json`) - highest priority
2. **Configuration updates** (from `config_jobs.json`) - after firmware

If a sensor has both firmware and config jobs pending:
- Firmware job will execute first
- Config job will execute in next heartbeat cycle (after firmware completes)

## 5. How Jobs Are Triggered

Jobs execute based on sensor heartbeats:

### Heartbeat Value = 1 (STATUS)
```json
{
  "sensor_sn": "324269",
  "heartbeats_after_measurement": 1
}
```
- Collector sends STATUS command
- No job execution
- Status saved to `/received/status_324269_timestamp.txt`

### Heartbeat Value > 1 (OTHER)
```json
{
  "sensor_sn": "324269",
  "heartbeats_after_measurement": 2
}
```
- Collector checks for jobs matching S/N "324269"
- If found, executes the job
- Job removed from JSON file after execution

## 6. Testing Sequence

### Test 1: Configuration Update
1. Create `/jobs/config_jobs.json` with single sensor job
2. Sensor connects to AP
3. Sensor sends heartbeat with value > 1
4. Collector executes config job
5. Verify config command sent to sensor
6. Verify job removed from JSON file

### Test 2: Firmware Update
1. Place firmware hex file in `/firmware/` directory
2. Create `/jobs/firmware_jobs.json` with firmware job
3. Sensor connects to AP
4. Sensor sends heartbeat with value > 1
5. Collector executes firmware update
6. Monitor serial output for progress
7. Verify job removed from JSON file

### Test 3: Multiple Sensors
1. Create jobs for 3 different sensors
2. All sensors connect to AP simultaneously
3. Sensors send heartbeats (staggered timing)
4. Collector processes all jobs concurrently
5. Verify all jobs complete successfully

## 7. Monitoring Job Execution

Serial output will show:
```
[HB] OTHER heartbeat received for SN=324269 IP=192.168.4.2
[HB] Logged heartbeat: 2025-01-15T10:30:45.000Z,324269
[HB-TASK] Checking jobs for SN=324269 IP=192.168.4.2
[JOBS] Found CONFIG job for SN=324269
[CONFIG] Starting configuration update for SN=324269 IP=192.168.4.2
[CONFIG] HTTP GET http://192.168.4.2/api?command=CONFIGURE&datetime=12345&...
[CONFIG] Response body:
OK
[CONFIG] SUCCESS (OK found in response)
[JOBS] CONFIG job result for SN=324269 -> OK
[HB-TASK] Jobs executed for SN=324269
```

## 8. Common Issues

### Job Not Executing
**Problem**: Heartbeat received but job doesn't execute
**Causes**:
- S/N mismatch (check exact spelling and case)
- heartbeats_after_measurement = 1 (should be > 1 for job execution)
- Job file syntax error (invalid JSON)
- SD card not properly initialized

**Solution**:
- Verify S/N matches exactly: `"sn": "324269"`
- Check heartbeat value: should be 2 or higher
- Validate JSON with online tool
- Check serial output: `[SD] Card initialized successfully`

### Firmware Update Fails
**Problem**: Firmware update starts but fails mid-way
**Causes**:
- Hex file not found at specified path
- Hex file corrupted or invalid format
- Sensor not in bootloader mode
- Timeout too short for large firmware

**Solution**:
- Verify hex file exists: check path exactly
- Ensure hex file is valid Intel HEX format (lines start with `:`)
- Send BOOT_LOADER command first if needed
- Increase timeout_ms for large files (>500 lines)

### Config Update Not Applied
**Problem**: Config command succeeds but sensor doesn't apply changes
**Causes**:
- Parameter names misspelled
- Parameter values out of range
- Sensor firmware doesn't support parameter

**Solution**:
- Check parameter names against sensor documentation
- Verify value ranges (e.g., temp_threshold > 0)
- Update sensor firmware if needed

## 9. Job File Management

### Adding Jobs Remotely
Jobs can be added by the server during uplink window:
1. Collector wakes for uplink
2. Connects to root node
3. Root can POST new job files
4. Collector downloads and stores in `/jobs/`
5. Jobs available for next AP window

### Removing Jobs Manually
To cancel pending jobs:
1. Remove SD card from collector
2. Edit or delete job files in `/jobs/`
3. Reinsert SD card
4. Jobs will not execute

### Backing Up Jobs
Important jobs should be backed up:
```bash
# Copy from SD card to computer
cp /media/sdcard/jobs/*.json ~/backup/

# Restore if needed
cp ~/backup/*.json /media/sdcard/jobs/
```

## 10. Advanced Examples

### Partial Firmware Update (Testing)
Update only first 100 lines for quick testing:
```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/test.hex",
      "max_lines": 100,
      "timeout_ms": 60000
    }
  ]
}
```

### Urgent Configuration Change
Quick config change with minimal parameters:
```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
        "temp_threshold": 55
      }
    }
  ]
}
```

### Multiple Firmware Versions
Different firmware per sensor:
```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/sensor_v1.17.hex"
    },
    {
      "sn": "324270",
      "hex_path": "/firmware/sensor_v1.18_beta.hex"
    },
    {
      "sn": "324271",
      "hex_path": "/firmware/sensor_v1.16_stable.hex"
    }
  ]
}
```

## 11. Job File Templates

Copy these templates to your SD card and modify as needed.

### Empty Template - Firmware
```json
{
  "jobs": []
}
```

### Empty Template - Config
```json
{
  "jobs": []
}
```

### Single Job Template - Firmware
```json
{
  "jobs": [
    {
      "sn": "SENSOR_SN_HERE",
      "hex_path": "/firmware/FIRMWARE_FILE.hex",
      "max_lines": 0,
      "timeout_ms": 480000
    }
  ]
}
```

### Single Job Template - Config
```json
{
  "jobs": [
    {
      "sn": "SENSOR_SN_HERE",
      "params": {
        "temp_threshold": 45
      }
    }
  ]
}
```

Replace `SENSOR_SN_HERE` with actual sensor serial number (e.g., "324269").
