# Root Node: Server Communication & WiFi Testing Guide

## Περίληψη

Αυτό το document εξηγεί:
1. **Πώς επικοινωνεί ο Root με τον server** (sensorsdaemon / backend)
2. **Πώς να κάνεις testing χωρίς PoE** χρησιμοποιώντας WiFi boards

---

## Αρχιτεκτονική - Root ↔ Server

### Κανονική λειτουργία (PoE / Ethernet)

```
[Server/Laptop]
   │  (Ethernet / PoE)
   │
[Root ESP32] ←── SoftAP ──── [Repeater / Collector]
   port 8080
```

Ο Root συνδέεται απευθείας στον server μέσω Ethernet/PoE. Ο server γνωρίζει σταθερά την IP του Root.

### Testing χωρίς PoE (WiFi)

```
[Server/Laptop]
   │  WiFi (home router / hotspot)
   │
[Root ESP32] ←── WiFi STA ──── [Home Router]
   │
   └──── SoftAP (Root_AP) ──── [Repeater / Collector]
```

Ο Root συνδέεται **ταυτόχρονα** σε δύο δίκτυα:
- **SoftAP** (`Root_AP` / `192.168.10.1`): Για collectors/repeaters να ανεβάζουν δεδομένα
- **WiFi STA** (home router): Ο server φτάνει τον root στο DHCP IP που παίρνει

---

## Ρύθμιση Root για WiFi Testing

### Στη Configuration page του Root:

1. Άνοιξε το config page (σύνδεσε σε `Repeater_Setup_XXXX`, βγαίνει στο `192.168.4.1`)
2. Επίλεξε Role: **Root**
3. Συμπλήρωσε:
   - **Root AP SSID**: `Root_AP` (αυτό βλέπουν collectors/repeaters)
   - **HTTP Port**: `8080`
   - **Server WiFi SSID**: το SSID του home WiFi/laptop hotspot
   - **Server WiFi Password**: ο κωδικός του

4. Save & Reboot

Στο Serial Monitor θα δεις:
```
[ROOT-STA] Connecting to server WiFi: MyHomeWiFi
[ROOT-STA] Connected! STA IP: 192.168.1.50  AP IP: 192.168.10.1
[ROOT-STA] Server can reach root at http://192.168.1.50:8080
```

---

## Root HTTP API (port 8080)

| Method | Endpoint | Περιγραφή |
|--------|----------|-----------|
| GET | `/health` | Health check: `{"ok":true}` |
| GET | `/time` | Επιστρέφει Unix epoch: `{"epoch":1234567890}` |
| POST | `/ingest` | Upload δεδομένου από collector (multipart) |
| **POST** | **`/upload?path=/jobs/config_jobs.json`** | **Server → Root: στείλε jobs** |
| **GET** | **`/list?dir=/received`** | **List αρχείων που έχει λάβει ο root** |
| **GET** | **`/download?path=/received/foo.csv`** | **Download αρχείου** |
| GET | `/jobs/config_jobs.json` | Collector κατεβάζει config jobs |
| GET | `/jobs/firmware_jobs.json` | Collector κατεβάζει firmware jobs |
| GET | `/firmware/*` | Collector κατεβάζει hex αρχεία |

### Πώς ο Server στέλνει jobs στον Root

```bash
# Στείλε config jobs (π.χ. από laptop)
curl -X POST \
  "http://192.168.1.50:8080/upload?path=/jobs/config_jobs.json" \
  --data-binary @config_jobs.json

# Στείλε firmware jobs
curl -X POST \
  "http://192.168.1.50:8080/upload?path=/jobs/firmware_jobs.json" \
  --data-binary @firmware_jobs.json

# Ανέβασε hex αρχείο για firmware update
curl -X POST \
  "http://192.168.1.50:8080/upload?path=/firmware/vibration_sensor_app_v1.17.hex" \
  --data-binary @vibration_sensor_app_v1.17.hex
```

### Πώς ο Server διαβάζει δεδομένα από τον Root

```bash
# List αρχεία που έχουν ανέβει από collectors
curl "http://192.168.1.50:8080/list?dir=/received"
# → ["heartbeat_api.csv","status_324269_12345.txt","12345678_sensordata.bin"]

# Download ένα αρχείο
curl "http://192.168.1.50:8080/download?path=/received/heartbeat_api.csv" \
  -o heartbeat_api.csv
```

---

## Retry Logic (ίδιο με sensorsdaemon.py)

### Python sensorsdaemon.py

```python
# handleFirmwareUpdate:
MAX_TRIES = 3

# 1. Initial STATUS before firmware (3 retries, 4s delay)
while tries < MAX_TRIES:
    res = submitCommandStatus(sensor, wait_before_sending=False)
    if res: break
    time.sleep(4)

# 2. Each HEX line (3 retries, 4s delay)
for fw_line in fw_lines:
    while tries < MAX_TRIES:
        res = submitFirmwareUpdateCommand(sensor, fw_line)
        if res: break
        time.sleep(4)
```

### ESP32 (μετά τις αλλαγές)

```cpp
// processJobsForSN → FW job:

// 1. Initial STATUS (3 retries, 4s delay) - ΝΕΟ ✅
const int STATUS_MAX_TRIES = 3;
for (int t = 0; t < STATUS_MAX_TRIES; t++) {
    if (sjm_requestStatus(ip, statusSn)) { statusOk = true; break; }
    Serial.printf("[JOBS] STATUS try %d/%d failed, retrying in 4s...\n", t+1, STATUS_MAX_TRIES);
    if (t < STATUS_MAX_TRIES - 1) delay(4000);
}
if (!statusOk) return false; // Abort FW job

// 2. Each HEX line (3 retries, 4s delay) - Υπήρχε ήδη ✅
// (firmware_updater.cpp::executeFirmwareJob)
const int MAX_TRIES = 3;
for (int t = 0; t < MAX_TRIES; ++t) {
    if (httpGetSensor(ip, path, body, 5000UL)) { ok = true; break; }
    delay(4000);
}
```

### Σύγκριση

| Βήμα | sensorsdaemon.py | ESP32 (before) | ESP32 (after) |
|------|-----------------|----------------|---------------|
| STATUS πριν FW | 3 retries, 4s | ❌ none | ✅ 3 retries, 4s |
| Κάθε HEX line | 3 retries, 4s | ✅ 3 retries, 4s | ✅ 3 retries, 4s |
| CONFIG update | 1 attempt | 1 attempt | 1 attempt |
| STATUS heartbeat | 1 attempt | 1 attempt | 1 attempt |

---

## Root-Server: Μελλοντική Αρχιτεκτονική (PoE)

Για production deployment:

```
[Server]
  ├── sensorsdaemon.py (Linux service)
  │     └── POST http://root-ip:8080/upload?path=/jobs/...
  │     └── GET  http://root-ip:8080/list?dir=/received
  │     └── GET  http://root-ip:8080/download?path=...
  │
  └── [Root ESP32] via PoE/Ethernet
        ├── SoftAP: Root_AP (192.168.10.1) ← collectors
        └── Ethernet: 192.168.0.X ← server
```

Για να ενεργοποιηθεί το Ethernet support στο ESP32, θα χρειαστεί:
- ESP32 με Ethernet PHY (π.χ. WT32-ETH01 ή ESP32 + LAN8720)
- Αλλαγή στον κώδικα για `ETH.begin()` αντί WiFi STA

**Για τώρα (testing)**: Το WiFi STA mode είναι ισοδύναμο - ο server φτάνει τον root στο DHCP IP.

---

## Checklist Testing

- [ ] Root ESP32: Configure Server WiFi SSID/Pass
- [ ] Root booting → check Serial for `[ROOT-STA] Connected! STA IP: X.X.X.X`
- [ ] Server: `curl http://X.X.X.X:8080/health` → `{"ok":true}`
- [ ] Server: push config_jobs.json via `/upload`
- [ ] Collector boots → connects to Root_AP → downloads jobs
- [ ] Collector executes jobs (check Serial)
- [ ] Server: `curl "http://X.X.X.X:8080/list?dir=/received"` → αρχεία από collectors
