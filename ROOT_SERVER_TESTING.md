# Root Node: Architecture & Server Communication Guide

## Αρχιτεκτονική

```
[Server / Laptop]
  │  WiFi (συνδέεται στο Root_AP)
  │  sensorsdaemon.py + rootdaemon.py
  │
  └──→ [Root ESP32]  ←  SoftAP "Root_AP" (192.168.10.1:8080)
          │
          └──→ [Repeater ESP32]  ←  SoftAP
                    │
                    └──→ [Collector ESP32]  ←  SoftAP
                                │
                                └──→ [Sensors (HAT)]
```

**Ροή δεδομένων:**
- Sensors → Collector → (Repeater) → Root (`POST /ingest`)
- Root → Server: `GET /list`, `GET /download`
- Server → Root: `POST /upload` (config/firmware jobs)
- Root → Collector: `GET /jobs/config_jobs.json`, `GET /jobs/firmware_jobs.json`

---

## Ρύθμιση Root ESP32

1. Σύνδεσε το ESP32 στο PC μέσω USB
2. Μπές στη configuration page (σύνδεσε σε `Repeater_Setup_XXXX`, βγαίνει `192.168.4.1`)
3. Επίλεξε Role: **Root**
4. Συμπλήρωσε:
   - **Root AP SSID**: `Root_AP` (ή ό,τι θες)
   - **Root AP Password**: (min 8 chars ή άδειο για open)
   - **HTTP Port**: `8080`
5. Save & Reboot

Στο Serial Monitor θα δεις:
```
[ROOT] SoftAP Root_AP: OK | IP=192.168.10.1
[ROOT] HTTP server started on :8080 (/health /time /ingest /upload /list /download /jobs /firmware)
```

---

## Ρύθμιση Server (sensorsdaemon)

### 1. Σύνδεσε το Server στο Root_AP

Στο Linux/Mac:
```bash
# Βρες το SSID
nmcli dev wifi list

# Σύνδεσε
nmcli dev wifi connect "Root_AP" password "mypassword"

# Επαλήθευσε
ping 192.168.10.1
curl http://192.168.10.1:8080/health   # → {"ok":true}
```

### 2. Ρύθμιση config.ini

Στο αρχείο `config.<hostname>.ini` (π.χ. `config.hat.testble.ini`):

```ini
[General]
# ... υπάρχουσες ρυθμίσεις ...

# Root-node polling
root_node_host=192.168.10.1
root_node_port=8080
root_poll_interval_seconds=30
```

### 3. Εκκίνηση sensorsdaemon

```bash
cd ShipRepeaterNode/oninemonitoribg/sensorsdaemon
python3 sensorsdaemon.py -v
```

Θα δεις:
```
HAT Sensors Daemon started
Root-node polling started → http://192.168.10.1:8080
[rootdaemon] Root node is reachable.
[rootdaemon] [ROOT] 0 new file(s) in /received
```

---

## Root HTTP API (port 8080)

| Method | Endpoint | Περιγραφή |
|--------|----------|-----------|
| GET | `/health` | `{"ok":true}` |
| GET | `/time` | `{"epoch":1234567890}` |
| POST | `/ingest` | Upload αρχείου από collector (multipart) |
| POST | `/upload?path=/jobs/config_jobs.json` | Server → Root: ανέβασε jobs/firmware |
| GET | `/list?dir=/received` | Λίστα αρχείων που έχει λάβει ο root |
| GET | `/download?path=/received/foo.bin` | Κατέβασε αρχείο |
| GET | `/jobs/config_jobs.json` | Collector κατεβάζει config jobs |
| GET | `/jobs/firmware_jobs.json` | Collector κατεβάζει firmware jobs |
| GET | `/firmware/*` | Collector κατεβάζει hex αρχεία |

---

## rootdaemon.py – Λογική

### PULL (κάθε `root_poll_interval_seconds` δευτερόλεπτα)

1. `GET /list?dir=/received` → λίστα αρχείων
2. Για κάθε νέο αρχείο:
   - `.bin` → αποθηκεύεται στο `measurements/hat_sensors/` → `processMeasurements()` το επεξεργάζεται
   - `heartbeat*.csv` → ενημερώνει `latest_heartbeat_on` στη DB για κάθε SN
   - `*_SRSP.csv` / `status_*` → ενημερώνει sensor status στη DB

### PUSH (κάθε polling cycle)

1. Sensors με `flag_update_configuration=1` → generates `config_jobs.json` → `POST /upload?path=/jobs/config_jobs.json`
2. Sensors με εκκρεμές firmware update → generates `firmware_jobs.json` + ανεβάζει hex → `POST /upload`

---

## Job File Formats

### config_jobs.json
```json
{
  "jobs": [
    {
      "sn": "324269",
      "params": {
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
    }
  ]
}
```

### firmware_jobs.json
```json
{
  "jobs": [
    {
      "sn": "324269",
      "hex_path": "/firmware/fw_1_17.hex",
      "timeout_ms": 480000
    }
  ]
}
```

---

## Manual Testing (χωρίς sensorsdaemon)

```bash
# Βεβαιώσου ότι είσαι στο Root_AP
ping 192.168.10.1

# Health check
curl http://192.168.10.1:8080/health

# Στείλε config jobs
curl -X POST "http://192.168.10.1:8080/upload?path=/jobs/config_jobs.json" \
  -H "Content-Type: application/json" \
  --data-binary @config_jobs.json

# Δες τι έχει λάβει ο root από collectors
curl "http://192.168.10.1:8080/list?dir=/received"

# Κατέβασε ένα αρχείο
curl "http://192.168.10.1:8080/download?path=/received/heartbeat_api.csv"
```

---

## Checklist Testing

- [ ] Root ESP32: Flash με role=Root, AP SSID=Root_AP
- [ ] Root: Serial log → `HTTP server started on :8080`
- [ ] Server: WiFi συνδέεται στο Root_AP (`nmcli dev wifi connect Root_AP`)
- [ ] Server: `curl http://192.168.10.1:8080/health` → `{"ok":true}`
- [ ] Config ini: `root_node_host=192.168.10.1` uncommented
- [ ] sensorsdaemon: ξεκινάει → `Root-node polling started`
- [ ] Collector: Flash με role=Collector, uplinkSSID=Root_AP
- [ ] Sensor: Ξυπνάει, συνδέεται σε Collector, ανεβάζει δεδομένα
- [ ] Collector: Ανεβάζει στο Root (`POST /ingest`)
- [ ] Server: rootdaemon κατεβάζει τα αρχεία → processMeasurements()
- [ ] DB: Sensor status/heartbeat ενημερωμένα
