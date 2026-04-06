# ShipRepeaterNode — Project Plan & Energy Optimization

## 1. System Overview

### 1.1 Purpose
Battery-powered WiFi mesh infrastructure for relaying sensor data from industrial HAT vibration sensors to a shore-based server (`sensorsdaemon.py`). Three device roles:

| Role | Power Source | Function |
|------|-------------|----------|
| **Collector** | Battery (50,000 mAh) | Captures sensor data via WiFi AP, relays upstream |
| **Repeater** | Battery (50,000 mAh) | Relays data between collector and root |
| **Root** | Mains / large battery | Gateway to server, always available |

### 1.2 Target
**1–1.5 years** on a 50,000 mAh Li-ion battery (≈185 Wh at 3.7V nominal).

### 1.3 Power Budget

| Target | Daily Budget | Active Time @ 100mA avg |
|--------|-------------|------------------------|
| 1.5 years (548 days) | **91 mAh/day** | **55 minutes/day** |
| 1.0 year (365 days) | **137 mAh/day** | **82 minutes/day** |

> ESP32-C6 deep sleep: ~5 µA. Over 24h = 0.12 mAh — negligible.
> All power budget goes to active WiFi/BLE time.

---

## 2. Current Architecture (as-built)

### 2.1 Data Flow
```
Sensor (HAT)
    │  WiFi HTTP (heartbeat + measure data)
    ▼
Collector (ESP32-C6, SoftAP :3000)
    │  Saves to SD card (/queue/*.bin)
    │  WiFi STA upload during uplink window
    ▼
Repeater (ESP32-C6, SoftAP :8080 + BLE beacon)
    │  Saves to SD card (/queue/*.bin)
    │  WiFi STA upload to root
    ▼
Root (ESP32-C6, SoftAP :8080, mains powered)
    │  Saves to SD card (/received/*.bin)
    ▼
sensorsdaemon.py (server)
    │  Processes .bin → CSV, stores in DB
```

### 2.2 Current Timing Parameters

| Parameter | Collector | Repeater | Root |
|-----------|-----------|----------|------|
| AP Window | 20 min every 2 min cycle | Always on | Always on |
| Uplink Window | 60s every 15 min | Queue check every 60s | N/A |
| Sleep Type | Deep sleep | Light sleep (2s) | No sleep |
| BLE | Scan only (10s during uplink) | Beacon 100ms interval | Off |
| WiFi Mode | AP_STA | AP_STA | AP |

### 2.3 Current Power Consumption Estimates

| Role | Estimated Daily | Battery Life (50Ah) | Status |
|------|----------------|--------------------:|--------|
| Collector | ~100 mAh/day | ~500 days (1.4 yr) | ⚠️ Close to target |
| Repeater | ~960 mAh/day | ~52 days | ❌ Far from target |
| Root | ~1200 mAh/day | ~42 days | Needs mains power |

**Key Problem**: The repeater is essentially always on (WiFi AP + BLE beacon), consuming ~40 mA continuously. This makes battery operation impossible for 1+ year.

---

## 3. Sensor Communication Protocol

### 3.1 Sensor Behavior (from sensorsdaemon.py analysis)
- **Scheduled wake**: Every 6 hours (configurable via `wakeup_every_min` in minutes, e.g., 360)
- **Triggered wake**: Vibration threshold or temperature threshold
- **Communication**: WiFi HTTP to collector's AP
- **Data sizes**:
  - Heartbeat: ~50 bytes JSON
  - Status: ~256 bytes
  - Measurement: 7 bytes/sample × 26,667 Hz = **186 KB/second**
  - Typical 10s measurement: **~1.87 MB**
  - Typical 60s measurement: **~11.2 MB**

### 3.2 Server Commands (sensorsdaemon.py → sensor)
| Command | Direction | Size | Frequency |
|---------|-----------|------|-----------|
| STATUS | Server→Sensor | ~200B response | Every heartbeat |
| CONFIGURE | Server→Sensor | ~500B | On profile change |
| BOOT_LOADER | Server→Sensor | ~100B | Before FW update |
| FIRMWARE_UPDATE | Server→Sensor | ~100KB total (HEX lines) | Rare |

### 3.3 Data Flow Timing
```
Sensor wakes → Heartbeat → Collector sends STATUS
                         → If jobs pending: CONFIG/FW_UPDATE
                         → Sensor sends measurement data
                         → Collector saves to SD queue
                         → [Next uplink window] → Upload to Repeater
                         → [Repeater forwards] → Upload to Root
```

---

## 4. Energy Optimization Plan

### 4.1 Strategy: Synchronized Time-Slot Architecture

**Core Insight**: If we know when sensors wake (every 6 hours), we can synchronize the entire chain to only be active during predictable windows.

#### Proposed Schedule (per 6-hour sensor cycle)

```
Hour 0:00 — Sensor wakes, connects to Collector AP
             Collector AP active: 5-20 minutes (adaptive)
             
Hour 0:20 — Collector uploads to Repeater
             Both active: 5 minutes
             
Hour 0:25 — Repeater uploads to Root
             Both active: 5 minutes
             
Hour 0:30 — All devices deep sleep until next cycle
             
Hour 6:00 — Next sensor wake cycle
```

**Total active time per cycle**: ~30 minutes
**Cycles per day**: 4 (every 6 hours)
**Total active time per day**: ~120 minutes worst case, ~40 minutes typical

### 4.2 Collector Optimization

#### Current Issues
- AP stays on for 20 minutes even when no sensor connects
- Uplink window every 15 minutes (too frequent for 6-hour sensor cycles)
- WiFi AP mode draws ~30 mA idle

#### Proposed Changes

| Parameter | Current | Proposed | Impact |
|-----------|---------|----------|--------|
| `collectorApCycleSec` | 120s (2 min) | **21600s (6 hours)** | Align with sensor schedule |
| `collectorApWindowSec` | 1200s (20 min) | **600s (10 min)** adaptive | Reduce if no sensor seen |
| `meshIntervalMin` | 15 min | **360 min (6 hours)** | Uplink after each sensor visit |
| `meshWindowSec` | 60s | **300s (5 min)** | Enough for queue upload |
| Early AP close | 20 min fixed | **Close 60s after last sensor activity** | Huge power savings |

**Adaptive AP Window Logic**:
```
1. Open AP, start 10-minute max timer
2. If no sensor connects in 2 minutes → close AP, deep sleep
3. If sensor connects → extend timer to sensor_activity + 60s
4. After AP closes → immediately do uplink to repeater
5. Deep sleep until next scheduled window
```

**Estimated Power**:
- Active: 100 mA × 15 min typical = 25 mAh per cycle
- Deep sleep: 0.005 mA × 5.75 hours = 0.03 mAh
- **Daily (4 cycles): ~100 mAh** → same as current but aligned better
- **With adaptive early-close: ~40 mAh/day** → **1250+ days (3.4 years)**

### 4.3 Repeater Optimization (Critical Path)

#### Current Issues
- **Always-on WiFi AP** is the #1 power drain (~40 mA continuous)
- Light sleep only saves ~3% (2s sleep / 62s cycle)
- BLE beacon runs continuously even when no collectors nearby

#### Proposed: Deep-Sleep Repeater with Scheduled Wake

**Architecture Change**: Repeater switches from always-on to **scheduled deep sleep** with time-synchronized wake windows.

| Parameter | Current | Proposed | Impact |
|-----------|---------|----------|--------|
| Sleep mode | Light sleep (2s) | **Deep sleep (hours)** | 100x power reduction |
| WiFi AP | Always on | **On during scheduled windows only** | Eliminates idle drain |
| BLE beacon | Always on (100ms) | **Off** (not needed if time-synced) | Saves ~10 mA |
| Wake schedule | Timer + BLE + WiFi | **Timer only (RTC)** | Simplest, most reliable |

**Proposed Repeater Cycle**:
```
1. Wake from deep sleep (RTC timer)
2. Start WiFi AP (SoftAP mode)
3. Wait for collectors to connect (5-minute window)
4. Receive data via /ingest
5. Connect STA to Root/parent
6. Forward queued files
7. Sync time from Root
8. Deep sleep until next window
```

**Wake Schedule Options**:

| Option | Description | Active/Day | Power/Day |
|--------|-------------|-----------|-----------|
| A: Match sensor | Every 6 hours, 10 min window | 40 min | **~67 mAh** |
| B: Hourly check | Every hour, 5 min window | 120 min | **~200 mAh** |
| C: Every 30 min | Every 30 min, 3 min window | 144 min | **~240 mAh** |
| D: Adaptive | BLE-triggered wake (keep BLE only) | Variable | **~100 mAh** |

**Recommended: Option A (sensor-aligned) with Option D fallback**

- Primary: Time-synced with sensor schedule (every 6 hours)
- Fallback: Extra wake window every 2 hours for ad-hoc uploads
- **Estimated daily: ~80 mAh → 625 days (1.7 years) ✅**

#### Time Synchronization for Repeater
- All nodes sync to Root's clock via `GET /time`
- Persist epoch in NVS (already implemented)
- RTC timer maintains ~1s/day drift → resync every wake
- Schedule: Wake at minute 0 of hours 0, 6, 12, 18 (±2 min tolerance)

### 4.4 Collector-Repeater Rendezvous Protocol

**Problem**: If both collector and repeater deep-sleep, they must wake at the same time.

**Solution**: Time-slot rendezvous
```
Shared schedule (all nodes):
  - Sensor window:  T+0:00 to T+0:20  (collector AP on)
  - Uplink window:  T+0:20 to T+0:30  (repeater AP on, collector STA)
  - Forward window: T+0:30 to T+0:35  (repeater STA to root)
  
Where T = {00:00, 06:00, 12:00, 18:00} UTC
```

**Tolerance**: ±2 minutes (ESP32-C6 RTC drift is ~1s/day; 6-hour resync keeps error < 10s)

**Collector Logic**:
```
1. Wake at T (sensor scheduled time)
2. Run AP for sensor collection (10-20 min)
3. At T+20: switch to STA mode, scan for repeater AP
4. Upload queued files to repeater
5. Deep sleep until next T
```

**Repeater Logic**:
```
1. Wake at T+18 (2 min before collector expects to uplink)
2. Start AP, wait for collector connections
3. Accept uploads via /ingest (5 min window)
4. Connect STA to Root
5. Forward all queued files
6. Deep sleep until next T
```

### 4.5 Handling Triggered (Ad-hoc) Sensor Wake-ups

**Challenge**: Sensors can wake randomly (vibration/temperature triggers). The collector must be available.

**Options**:

1. **Always-listening collector** (current approach) — Too expensive
2. **Periodic short AP windows** — Collector opens AP for 30s every 15 min
3. **BLE wake-up from sensor** — Sensor sends BLE advertisement, collector wakes
4. **Accept latency** — Triggered data waits until next scheduled window (up to 6 hours)

**Recommended: Option 4 (accept latency) + Option 2 (periodic check-ins)**

Rationale:
- Vibration/temperature events are logged locally on the sensor
- Data freshness requirement is typically hours, not seconds
- Sensor can store data and upload at next scheduled window
- Add optional "fast check" every 30 minutes: collector opens AP for 60s to catch triggered sensors

**Fast Check Power Cost**:
- 60s active × 48 times/day × 100 mA = **80 mAh/day additional**
- With fast check: total ~120 mAh/day → still 416 days (1.1 years)
- Without fast check: ~40 mAh/day → 1250 days (3.4 years)

**Recommendation**: Make fast-check configurable. Default OFF for maximum battery life, enable for time-sensitive installations.

---

## 5. Implementation Roadmap

### Phase 1: Collector Deep-Sleep Optimization (Priority: HIGH)
**Goal**: Reduce collector from ~100 mAh/day to ~40 mAh/day

- [ ] **P1.1** Add adaptive AP window — close AP 60s after last sensor activity instead of fixed 20 min
- [ ] **P1.2** Align `collectorApCycleSec` with sensor wake schedule (configurable, default 6h)
- [ ] **P1.3** Align `meshIntervalMin` with sensor schedule — uplink immediately after AP window closes
- [ ] **P1.4** Add early AP timeout (2 min with no sensor → abort and sleep)
- [ ] **P1.5** Disable WiFi radio between AP and uplink windows (full deep sleep)

### Phase 2: Repeater Deep-Sleep Conversion (Priority: CRITICAL)
**Goal**: Reduce repeater from ~960 mAh/day to ~80 mAh/day

- [ ] **P2.1** Convert repeater from light-sleep/always-on to deep-sleep with scheduled wake
- [ ] **P2.2** Remove always-on BLE beacon from repeater (replace with time-sync rendezvous)
- [ ] **P2.3** Implement time-slot rendezvous protocol (collector and repeater wake at coordinated times)
- [ ] **P2.4** Add repeater wake schedule: wake at T+18 min of each sensor cycle
- [ ] **P2.5** Add repeater AP window: 5-minute AP + STA forward window per cycle
- [ ] **P2.6** Ensure NVS epoch persistence survives deep sleep (already implemented)
- [ ] **P2.7** Handle RTC drift — resync from root on every wake

### Phase 3: Time Synchronization Hardening (Priority: MEDIUM)
**Goal**: Ensure reliable ±2 minute accuracy across all nodes

- [ ] **P3.1** Implement shared schedule definition (configurable cycle times)
- [ ] **P3.2** Add schedule broadcast: root includes next-window time in `/time` response
- [ ] **P3.3** Add drift compensation — track RTC drift rate and compensate
- [ ] **P3.4** Add fallback: if uplink fails, retry at T+60 minutes

### Phase 4: Triggered Event Support (Priority: LOW)
**Goal**: Support ad-hoc sensor wake-ups without destroying battery life

- [ ] **P4.1** Add configurable "fast check" mode — 60s AP every 30 minutes
- [ ] **P4.2** Sensor-side: buffer triggered measurements for next scheduled upload
- [ ] **P4.3** Add priority flag in queue files — urgent data uploaded first

### Phase 5: Root Integration (Priority: MEDIUM)
**Goal**: Connect root to sensorsdaemon.py

- [ ] **P5.1** Root forwards received .bin files to sensorsdaemon's measurements folder
- [ ] **P5.2** Root downloads job files (config_jobs.json, firmware_jobs.json) from daemon
- [ ] **P5.3** Root serves job files to collectors via `/jobs/*` endpoints
- [ ] **P5.4** Add HTTP bridge: root proxies sensor events to daemon API

---

## 6. Projected Battery Life After Optimization

| Role | Current | After Phase 1-2 | After Phase 3-4 | Target Met? |
|------|---------|-----------------|------------------|-------------|
| Collector | 500 days | **1250 days** | **1100 days** (with fast-check) | ✅ Yes |
| Repeater | 52 days | **625 days** | **580 days** | ✅ Yes |
| Root | 42 days | N/A (mains powered) | N/A | ✅ N/A |

### Detailed Power Breakdown (After Optimization)

#### Collector (Phase 1 complete)
| Activity | Duration/Day | Current (mA) | Energy (mAh) |
|----------|-------------|-------------|--------------|
| AP window (sensor intake) | 4 × 10 min = 40 min | 100 | 67 |
| Uplink window | 4 × 5 min = 20 min | 100 | 33 |
| Deep sleep | 23 hours | 0.005 | 0.12 |
| **Total** | | | **~100 mAh** |
| With adaptive early-close | 4 × 3 min avg | | **~40 mAh** |

#### Repeater (Phase 2 complete)
| Activity | Duration/Day | Current (mA) | Energy (mAh) |
|----------|-------------|-------------|--------------|
| AP window (receive) | 4 × 5 min = 20 min | 100 | 33 |
| STA forward to root | 4 × 5 min = 20 min | 120 | 40 |
| Deep sleep | 23.3 hours | 0.005 | 0.12 |
| **Total** | | | **~73 mAh** |

---

## 7. Configuration Parameters (New/Changed)

```cpp
// ---- Schedule-based wake (Phase 1-2) ----
#define SENSOR_CYCLE_HOURS        6       // Sensor wake interval
#define COLLECTOR_AP_MAX_S        600     // Max AP window (10 min)
#define COLLECTOR_AP_IDLE_TIMEOUT_S 120   // Close AP if no sensor in 2 min
#define COLLECTOR_AP_ACTIVITY_TAIL_S 60   // Keep AP open 60s after last activity
#define UPLINK_WINDOW_S           300     // 5 min uplink window
#define REPEATER_WAKE_BEFORE_S    120     // Wake 2 min before expected collector uplink
#define REPEATER_AP_WINDOW_S      300     // 5 min AP window for collectors
#define REPEATER_FORWARD_WINDOW_S 300     // 5 min STA forward to root

// ---- Optional fast-check (Phase 4) ----
#define FAST_CHECK_ENABLED        false   // Enable periodic short AP windows
#define FAST_CHECK_INTERVAL_M     30      // Check every 30 minutes
#define FAST_CHECK_DURATION_S     60      // 60 second AP window
```

---

## 8. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|-----------|
| RTC drift causes missed rendezvous | Collector can't upload to repeater | ±2 min tolerance window; resync from root every wake |
| Sensor wakes between scheduled windows | Data delayed up to 6 hours | Accept latency or enable fast-check mode |
| Root unavailable during forward window | Repeater queue grows on SD | Retry next cycle; SD has ample capacity |
| SD card corruption after deep sleep | Lost queue data | Already handled: markSdStale() + resetSdCard() |
| GPIO8 strapping pin (hardware) | Intermittent boot failures | Requires external 4.7KΩ pull-up resistor |

---

## 9. Hardware Requirements

| Item | Required For | Priority |
|------|-------------|----------|
| External 4.7KΩ pull-up on GPIO8 | All roles (boot reliability) | **P1** |
| 50,000 mAh Li-ion battery | Collector + Repeater | **P0** |
| Mains power / solar panel | Root | **P0** |
| MicroSD card (FAT16, 8.3 filenames) | All roles with SD | **P0** |

---

## 10. Completed Stabilization Items

The following bugs from the initial analysis have been fixed:

- [x] **SD filename bug** — Queue filenames now use 8.3 format (`e0000001.bin`, `m0000001.bin`)
- [x] **BLE advertising interval** — Changed from 20ms to 100ms (0x00A0)
- [x] **Repeater awake window** — Reduced from 15 min to 60 seconds
- [x] **Epoch validation** — `persistRtcTime()` rejects epochs < 1,700,000,000
- [x] **ensureDir caching** — Results cached in `s_queueDirVerified`/`s_receivedDirVerified`
- [x] **Repeater deep-sleep guard** — `decideAndGoToSleep()` explicitly prevents repeater deep sleep
- [x] **BLE light-sleep bug** — `markAdvertisingStopped()` called before light sleep, unconditional restart after wake
- [x] **WiFi STA connect delay** — Changed from delay(200) to delay(50)
- [x] **WDT starvation** — delay(1) before all client.connect() calls
- [ ] **GPIO8 strapping pin** — Requires hardware fix (external pull-up resistor)
