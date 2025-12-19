# Testing Guide: Dynamic WiFi AP Control

This guide provides step-by-step instructions for testing the new dynamic WiFi AP control feature with light sleep and queue-based transfer.

---

## Test Environment Setup

### Hardware Required
- 1x ESP32 board (Root node)
- 1x ESP32 board (Repeater node)
- 1x ESP32 board (Collector node)
- 3x SD cards
- 1x Multimeter (for power measurement)
- Serial cables for all boards

### Software Required
- Arduino IDE with ESP32 support
- Serial monitor (Arduino IDE or PuTTY)
- nRF Connect app (for BLE monitoring - optional)

---

## Test 1: BLE Wake-up Signal

**Objective:** Verify Collector can wake Repeater via BLE

### Setup
1. Flash Repeater with new firmware
2. Configure as ROLE_REPEATER with BLE enabled
3. Verify configuration:
   ```cpp
   config.role = ROLE_REPEATER;
   config.bleBeaconEnabled = true;
   ```

### Test Steps

#### 1.1 Verify Repeater Default State
```
Expected Serial Output (Repeater):
[BLE-MESH] Repeater BLE beacon active (WiFi AP OFF by default)
[REPEATER] No clients connected, WiFi AP remains OFF
```

**Verify:**
- ✅ BLE beacon active
- ✅ WiFi AP is OFF (not broadcasting SSID)
- ✅ Power consumption: ~10-15 mA

#### 1.2 Collector Sends Wake-up
```
Flash Collector with new firmware
Configure as ROLE_COLLECTOR with BLE enabled
Trigger uplink cycle
```

Expected Serial Output (Collector):
```
[BLE-MESH] Scanning for parent node...
[BLE-SCAN] Found mesh node AP: Repeater_AP, RSSI: -45
[BLE-WAKEUP] Parent is Repeater, sending wake-up signal...
[BLE-WAKEUP] Connected to device
[BLE-WAKEUP] Wake-up signal sent!
[BLE-WAKEUP] Wake-up signal sent successfully, waiting for WiFi AP...
```

Expected Serial Output (Repeater):
```
[REPEATER-WAKEUP] Wake-up request received via BLE
[REPEATER] WiFi AP started: Repeater_AP | IP=192.168.20.1
[REPEATER] HTTP server ready on :8080 (/time, /jobs, /firmware, /ingest)
```

**Verify:**
- ✅ BLE wake-up signal received
- ✅ WiFi AP starts within 3 seconds
- ✅ HTTP server active
- ✅ Collector can see Repeater_AP SSID

#### 1.3 WiFi Connection
```
Expected Serial Output (Collector):
[UPLINK] Connecting STA to Repeater_AP...
[UPLINK] Connected to Repeater
```

**Verify:**
- ✅ Collector connects to WiFi AP
- ✅ Collector gets IP (usually 192.168.20.2)

**Pass Criteria:**
- All serial messages appear in correct order
- WiFi AP starts within 3-5 seconds
- Collector successfully connects

---

## Test 2: Queue-Based Upload

**Objective:** Verify ALL files in queue are uploaded

### Setup
1. Create test files in Collector's `/queue` directory:
   ```
   /queue/entry_00000001.bin (1KB test data)
   /queue/entry_00000002.bin (1KB test data)
   /queue/entry_00000003.bin (1KB test data)
   ```

### Test Steps

#### 2.1 Start Upload
```
Trigger Collector uplink cycle
Monitor serial output
```

Expected Serial Output (Collector):
```
[QUEUE-UPLOAD] Starting queue-based file upload...
[QUEUE-UPLOAD] Uploading file 1: entry_00000001.bin
[HTTP UP] Uploaded entry_00000001.bin (1024 bytes)
[QUEUE-UPLOAD] Successfully uploaded and removed: entry_00000001.bin
[QUEUE-UPLOAD] Uploading file 2: entry_00000002.bin
[HTTP UP] Uploaded entry_00000002.bin (1024 bytes)
[QUEUE-UPLOAD] Successfully uploaded and removed: entry_00000002.bin
[QUEUE-UPLOAD] Uploading file 3: entry_00000003.bin
[HTTP UP] Uploaded entry_00000003.bin (1024 bytes)
[QUEUE-UPLOAD] Successfully uploaded and removed: entry_00000003.bin
[QUEUE-UPLOAD] Queue upload complete. Files uploaded: 3, skipped: 0
```

Expected Serial Output (Repeater):
```
[REPEATER] Receiving file from Collector: /received/1234_entry_00000001.bin
[REPEATER] Saved file from Collector: /received/1234_entry_00000001.bin
[REPEATER] Receiving file from Collector: /received/1234_entry_00000002.bin
[REPEATER] Saved file from Collector: /received/1234_entry_00000002.bin
[REPEATER] Receiving file from Collector: /received/1234_entry_00000003.bin
[REPEATER] Saved file from Collector: /received/1234_entry_00000003.bin
```

#### 2.2 Verify Queue Empty
```
Expected Serial Output (Collector):
[UPLINK] Queue empty and jobs synced → sleeping early.
```

**Verify:**
- ✅ All 3 files uploaded
- ✅ Queue directory empty
- ✅ Files exist on Repeater SD card
- ✅ Collector goes to sleep

#### 2.3 Test Upload Failure Retry
```
Simulate network error (disconnect WiFi during upload)
Verify retry logic
```

Expected Serial Output (Collector):
```
[QUEUE-UPLOAD] Uploading file 1: entry_00000001.bin
[HTTP UP] Connect failed
[QUEUE-UPLOAD] Retry attempt 1/2 for: entry_00000001.bin
[HTTP UP] Connect failed
[QUEUE-UPLOAD] Retry attempt 2/2 for: entry_00000001.bin
[HTTP UP] Uploaded entry_00000001.bin (1024 bytes)
[QUEUE-UPLOAD] Successfully uploaded and removed: entry_00000001.bin
```

**Pass Criteria:**
- All files uploaded successfully
- Queue directory empty after upload
- Retry logic works (3 attempts max)
- Failed files skipped, not blocking queue

---

## Test 3: Job Download

**Objective:** Verify Collector downloads jobs from Repeater

### Setup
1. Create job files on Repeater SD card:
   ```
   /jobs/config_jobs.json
   /jobs/firmware_jobs.json
   /firmware/test.hex
   ```

### Test Steps

#### 3.1 Upload Complete, Start Job Sync
```
Expected Serial Output (Collector):
[QUEUE-UPLOAD] Queue upload complete. Files uploaded: 3, skipped: 0
[SYNC] Syncing jobs from root...
[DOWNLOAD] Fetching http://192.168.20.1:8080/jobs/config_jobs.json...
[DOWNLOAD] Downloaded /jobs/config_jobs.json (256 bytes) -> /jobs/config_jobs.json
[SYNC] Config jobs updated
[DOWNLOAD] Fetching http://192.168.20.1:8080/jobs/firmware_jobs.json...
[DOWNLOAD] Downloaded /jobs/firmware_jobs.json (128 bytes) -> /jobs/firmware_jobs.json
[SYNC] Firmware jobs updated
```

Expected Serial Output (Repeater):
```
[REPEATER] Served /jobs/config_jobs.json to Collector
[REPEATER] Served /jobs/firmware_jobs.json to Collector
```

**Verify:**
- ✅ Job files downloaded to Collector
- ✅ Files match Repeater versions
- ✅ Job cache reset

**Pass Criteria:**
- All job files downloaded
- File contents match source
- Jobs available for sensor processing

---

## Test 4: WiFi AP Auto-Shutdown

**Objective:** Verify WiFi AP stops after transfers complete

### Test Steps

#### 4.1 Complete Transfer
```
Wait for Collector to finish upload and job download
Collector disconnects from WiFi
```

Expected Serial Output (Collector):
```
[UPLINK] Queue empty and jobs synced → sleeping early.
[SLEEP] Entering deep sleep for X seconds.
```

#### 4.2 Monitor Repeater Timeout
```
Wait 30 seconds with no clients
```

Expected Serial Output (Repeater):
```
[REPEATER] No clients connected, stopping WiFi AP
[REPEATER] WiFi AP stopped, entering light sleep mode
```

**Verify:**
- ✅ WiFi AP stops after 30s with no clients
- ✅ BLE beacon remains active
- ✅ Power drops to ~2-5 mA (light sleep + BLE)

#### 4.3 Test Max Runtime Timeout
```
Keep Collector connected for 5+ minutes
```

Expected Serial Output (Repeater):
```
[REPEATER] Max AP time exceeded, stopping WiFi AP
[REPEATER] WiFi AP stopped, entering light sleep mode
```

**Pass Criteria:**
- WiFi AP stops after 30s idle
- WiFi AP stops after 5 min max
- BLE beacon stays active
- Power returns to light sleep level

---

## Test 5: Concurrent Collectors

**Objective:** Verify multiple Collectors can connect simultaneously

### Setup
1. Configure 2-3 Collectors with same Repeater parent
2. Create queue files on all Collectors
3. Set Collectors to wake at similar time

### Test Steps

#### 5.1 Multiple Wake-up Attempts
```
Collector 1 wakes at T=0
Collector 2 wakes at T=2
Collector 3 wakes at T=5
```

Expected Behavior:
```
T=0: Collector 1 sends wake-up, WiFi AP starts
T=2: Collector 2 finds WiFi AP already active, connects
T=5: Collector 3 finds WiFi AP already active, connects
```

Expected Serial Output (Repeater):
```
[REPEATER-WAKEUP] Wake-up request received via BLE
[REPEATER] WiFi AP started
[REPEATER] Receiving file from Collector: /received/1234_collector1_file.bin
[REPEATER] Receiving file from Collector: /received/1235_collector2_file.bin
[REPEATER] Receiving file from Collector: /received/1236_collector3_file.bin
```

**Verify:**
- ✅ All Collectors upload successfully
- ✅ No conflicts or errors
- ✅ AsyncWebServer handles concurrent requests
- ✅ WiFi AP stays active until last Collector disconnects

**Pass Criteria:**
- Multiple Collectors can connect
- Uploads don't interfere
- Last Collector disconnect triggers shutdown

---

## Test 6: Power Consumption

**Objective:** Verify power savings

### Setup
1. Connect multimeter in series with VCC
2. Configure Repeater with BLE enabled
3. Disable serial output (reduces noise)

### Test Steps

#### 6.1 Light Sleep Measurement
```
State: WiFi AP OFF, BLE beacon ON
Duration: 60 seconds
```

**Expected (ESP32 Feather V2 or similar low-power design):**
- ESP32 base light sleep: 1.2 mA
- BLE beacon overhead: ~1-3 mA
- **Total current: 2-5 mA**
- Voltage: 3.3V
- Power: ~7-17 mW

**Note:** Generic ESP32 DevKit boards may consume 10-20 mA due to:
- Less efficient voltage regulators
- Always-on power LEDs
- USB-to-serial chip power draw

#### 6.2 Active Measurement (1 Client)
```
State: WiFi AP ON, 1 Collector connected, uploading
Duration: Upload time (~10 seconds)
```

**Expected:**
- Current: 80-150 mA (typical ESP32 WiFi)
- Voltage: 3.3V
- Power: ~260-500 mW

#### 6.3 Daily Power Calculation

**For ESP32 Feather V2 (optimized design):**
```
Assumptions:
- 23 hours light sleep (BLE only)
- 1 hour active (WiFi + BLE, worst case)

Previous (WiFi always-on):
150 mA × 24h = 3600 mAh/day

New (dynamic WiFi AP):
(3 mA × 23h) + (130 mA × 1h) = 199 mAh/day

Savings: (3600 - 199) / 3600 = 94.5%
```

**For Generic ESP32 DevKit (less optimized):**
```
Previous (WiFi always-on):
150 mA × 24h = 3600 mAh/day

New (dynamic WiFi AP):
(15 mA × 23h) + (130 mA × 1h) = 475 mAh/day

Savings: (3600 - 475) / 3600 = 87%
```

**Pass Criteria:**
- Light sleep (Feather V2): ≤5 mA ⭐
- Light sleep (Generic): ≤20 mA
- Active: ≤150 mA
- Overall savings: ≥70%

---

## Test 7: BLE Range Test

**Objective:** Determine maximum wake-up distance

### Test Steps

#### 7.1 Distance Testing
```
Start at 1m distance
Move Collector away in 5m increments
Test wake-up at each distance
```

Expected Results:
```
1m:   RSSI -30, wake-up ✅
5m:   RSSI -40, wake-up ✅
10m:  RSSI -50, wake-up ✅
20m:  RSSI -60, wake-up ✅
30m:  RSSI -70, wake-up ✅
50m:  RSSI -80, wake-up ⚠️ (may fail)
100m: RSSI -90, wake-up ❌
```

**Pass Criteria:**
- Reliable wake-up up to 50m line-of-sight
- Graceful degradation beyond range
- Collector falls back to configured uplink if BLE fails

---

## Troubleshooting

### Issue: WiFi AP doesn't start

**Symptoms:**
```
[BLE-WAKEUP] Wake-up signal sent successfully
(No response from Repeater)
```

**Debug Steps:**
1. Check Repeater serial for `[REPEATER-WAKEUP]` message
2. Verify BLE characteristic exists (nRF Connect app)
3. Increase REPEATER_AP_STARTUP_DELAY_MS (currently 3000)
4. Check for BLE connection errors

**Solutions:**
- Reduce distance (<20m for testing)
- Check Repeater power supply
- Verify BLE not disabled in config

---

### Issue: Queue doesn't empty

**Symptoms:**
```
[QUEUE-UPLOAD] Queue upload complete. Files uploaded: 2, skipped: 1
[UPLINK] Warning: Queue still has files after upload attempt
```

**Debug Steps:**
1. Check skipped file count
2. Check WiFi connection stability
3. Check SD card errors
4. Check Repeater /ingest endpoint

**Solutions:**
- Files are retried next cycle (3 attempts each time)
- Check WiFi signal strength
- Verify SD card not full
- Check for corrupt files

---

### Issue: High power consumption

**Symptoms:**
```
Multimeter shows 150 mA in light sleep
```

**Debug Steps:**
1. Verify WiFi AP is OFF (check SSID broadcast)
2. Check for stuck tasks/threads
3. Verify BLE only (not WiFi)
4. Disable serial output

**Solutions:**
- Ensure stopRepeaterWiFiAP() called
- Check for WiFi.mode(WIFI_OFF)
- Reboot Repeater
- Check for other active peripherals

---

## Performance Benchmarks

| Test | Metric | Target | Result |
|------|--------|--------|--------|
| BLE Wake-up | Latency | <5s | _____ |
| WiFi AP Start | Time | <3s | _____ |
| Queue Upload | Files/min | >10 | _____ |
| Job Download | Time | <5s | _____ |
| AP Shutdown | Timeout | 30s | _____ |
| Light Sleep | Current | ≤15 mA | _____ |
| Active | Current | ≤150 mA | _____ |
| Power Savings | Reduction | ≥50% | _____ |

---

## Test Results Summary

### Test 1: BLE Wake-up
- [ ] PASS
- [ ] FAIL
- Notes: _________________

### Test 2: Queue Upload
- [ ] PASS
- [ ] FAIL
- Notes: _________________

### Test 3: Job Download
- [ ] PASS
- [ ] FAIL
- Notes: _________________

### Test 4: AP Auto-Shutdown
- [ ] PASS
- [ ] FAIL
- Notes: _________________

### Test 5: Concurrent Collectors
- [ ] PASS
- [ ] FAIL
- Notes: _________________

### Test 6: Power Consumption
- [ ] PASS
- [ ] FAIL
- Notes: _________________

### Test 7: BLE Range
- [ ] PASS
- [ ] FAIL
- Notes: _________________

---

## Final Checklist

- [ ] All tests completed
- [ ] Power savings verified (≥50%)
- [ ] No security vulnerabilities
- [ ] Documentation complete
- [ ] Code review addressed
- [ ] Ready for production deployment

---

## Next Steps

After successful testing:
1. Deploy to staging environment
2. Monitor for 24 hours
3. Measure actual power savings
4. Collect performance metrics
5. Roll out to production

For issues or questions, see:
- [DYNAMIC_WIFI_AP_IMPLEMENTATION.md](DYNAMIC_WIFI_AP_IMPLEMENTATION.md)
- [IMPLEMENTATION_CHANGES_SUMMARY.md](IMPLEMENTATION_CHANGES_SUMMARY.md)
