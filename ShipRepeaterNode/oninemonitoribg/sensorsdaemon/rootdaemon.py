#!/usr/bin/env python3
"""
rootdaemon.py
=============
Handles communication between the sensorsdaemon server and the Root ESP32 node.

Architecture:
  Server (sensorsdaemon) ← WiFi → Root AP (Root_AP / 192.168.10.1:8080)
                                       ↑
                                 Collectors / Repeaters

The server's WiFi adapter must be connected to Root's AP (Root_AP) before
this daemon starts. The Root IP is typically 192.168.10.1.

Responsibilities:
  1. PULL: Periodically list and download new files from Root (/received dir).
           - Measurement .bin files  → copy to hatsensors_measurements_folder
           - Heartbeat CSV files     → update sensor latest_heartbeat_on in DB
           - Status SRSP CSV files   → parse and update sensor status in DB
  2. PUSH: Periodically generate and upload job files to Root:
           - /jobs/config_jobs.json  for sensors with flag_update_configuration=1
           - /jobs/firmware_jobs.json for sensors with pending firmware updates
           - /firmware/<hex_file>    actual firmware hex when needed
"""

import os
import re
import json
import time
import logging
import requests
from datetime import datetime, timezone
from threading import Event

logger = logging.getLogger("rootdaemon")

# ──────────────────────────────────────────────────────────────────────────────
# RootNodeClient  –  thin wrapper around the Root HTTP API
# ──────────────────────────────────────────────────────────────────────────────

class RootNodeClient:
    """HTTP client for the Root ESP32 HTTP API."""

    def __init__(self, host: str, port: int, timeout: int = 10):
        self.base_url = f"http://{host}:{port}"
        self.timeout = timeout

    def health(self) -> bool:
        """Return True if root responds to /health."""
        try:
            r = requests.get(f"{self.base_url}/health", timeout=self.timeout)
            return r.status_code == 200
        except Exception:
            return False

    def list_dir(self, dir_path: str) -> list:
        """Return list of filenames in dir_path on Root's SD card."""
        try:
            r = requests.get(f"{self.base_url}/list",
                             params={"dir": dir_path},
                             timeout=self.timeout)
            if r.status_code == 200:
                return r.json()
        except Exception as e:
            logger.warning(f"[ROOT] list_dir({dir_path}) failed: {e}")
        return []

    def download_file(self, remote_path: str) -> bytes:
        """Download a file from Root. Returns bytes or None on error."""
        try:
            r = requests.get(f"{self.base_url}/download",
                             params={"path": remote_path},
                             timeout=self.timeout)
            if r.status_code == 200:
                return r.content
        except Exception as e:
            logger.warning(f"[ROOT] download_file({remote_path}) failed: {e}")
        return None

    def upload_file(self, remote_path: str, data: bytes) -> bool:
        """Upload raw bytes to remote_path on Root's SD card via POST /upload."""
        try:
            r = requests.post(f"{self.base_url}/upload",
                              params={"path": remote_path},
                              data=data,
                              headers={"Content-Type": "application/octet-stream"},
                              timeout=self.timeout)
            return r.status_code == 200
        except Exception as e:
            logger.warning(f"[ROOT] upload_file({remote_path}) failed: {e}")
        return False

    def upload_json(self, remote_path: str, obj: dict) -> bool:
        """Upload a JSON-serialised dict to remote_path on Root's SD card."""
        data = json.dumps(obj, indent=2).encode()
        return self.upload_file(remote_path, data)


# ──────────────────────────────────────────────────────────────────────────────
# RootDaemon  –  main polling loop
# ──────────────────────────────────────────────────────────────────────────────

class RootDaemon:
    """
    Background daemon that syncs between sensorsdaemon and the Root node.

    Parameters
    ----------
    dbm
        DBModels instance (shared with sensorsdaemon).
    config_general : dict
        The config.General dict from sensorsdaemon config.
    hatsensors_measurements_folder : str
        Local path where .bin measurement files are placed for processing.
    sensor_status_folder : str
        Local path for sensor status / heartbeat CSV files.
    sensor_responses_folder : str
        Local path for sensor status response CSV files.
    verbose : bool
        Enable verbose logging.
    """

    RECEIVED_DIR = "/received"
    JOBS_CFG_PATH = "/jobs/config_jobs.json"
    JOBS_FW_PATH  = "/jobs/firmware_jobs.json"
    # Sentinel filename for heartbeat_api.csv – never added to _seen so it is
    # re-downloaded every cycle and processed incrementally.
    _HEARTBEAT_CSV = "heartbeat_api.csv"

    def __init__(self,
                 dbm,
                 config_general: dict,
                 hatsensors_measurements_folder: str,
                 sensor_status_folder: str,
                 sensor_responses_folder: str,
                 verbose: bool = False):
        self.dbm = dbm
        self.cfg = config_general
        self.measurements_folder = hatsensors_measurements_folder
        self.sensor_status_folder = sensor_status_folder
        self.sensor_responses_folder = sensor_responses_folder
        self.verbose = verbose

        host = config_general.get("root_node_host", "192.168.10.1")
        port = int(config_general.get("root_node_port", 8080))
        self.root = RootNodeClient(host, port)

        # Track files already downloaded (persisted in a small JSON file)
        self._seen_file = os.path.join(sensor_status_folder, ".root_seen_files.json")
        self._seen: set = self._load_seen()
        # Track the last line count processed from heartbeat_api.csv (incremental)
        self._hb_csv_lines_seen: int = 0

        self._poll_interval = int(config_general.get("root_poll_interval_seconds", 30))
        self._stop_event = Event()

    # ── persistence of "seen" file set ────────────────────────────────────────

    def _load_seen(self) -> set:
        if os.path.isfile(self._seen_file):
            try:
                with open(self._seen_file) as f:
                    return set(json.load(f))
            except Exception:
                pass
        return set()

    def _save_seen(self):
        try:
            with open(self._seen_file, "w") as f:
                json.dump(list(self._seen), f)
        except Exception as e:
            logger.warning(f"[ROOT] Cannot save seen-files list: {e}")

    # ── logging helpers ────────────────────────────────────────────────────────

    def _pr(self, msg: str):
        logger.info(msg)
        if self.verbose:
            now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3] + " UTC"
            print(f"[{now}] (rootdaemon) {msg}", flush=True)

    # ── PULL: process received files ───────────────────────────────────────────

    def _pull_received_files(self):
        """Download and process any new files in Root's /received directory."""
        files = self.root.list_dir(self.RECEIVED_DIR)
        if not files:
            return

        # Handle heartbeat_api.csv incrementally (direct-to-Root mode)
        if self._HEARTBEAT_CSV in files:
            data = self.root.download_file(f"{self.RECEIVED_DIR}/{self._HEARTBEAT_CSV}")
            if data:
                self._handle_heartbeat_csv_incremental(data)

        new_files = [f for f in files if f not in self._seen and f != self._HEARTBEAT_CSV]
        if not new_files:
            return

        self._pr(f"[ROOT] {len(new_files)} new file(s) in /received")

        for fname in new_files:
            remote_path = f"{self.RECEIVED_DIR}/{fname}"
            data = self.root.download_file(remote_path)
            if data is None:
                self._pr(f"[ROOT] Failed to download {remote_path}, will retry")
                continue

            self._process_received_file(fname, data)
            self._seen.add(fname)

        self._save_seen()

    def _process_received_file(self, fname: str, data: bytes):
        """Route a downloaded file to the right handler based on its name."""
        fname_lower = fname.lower()

        if fname_lower.endswith(".bin"):
            # Measurement binary file from sensor (may have millis prefix from /ingest)
            self._handle_measurement_bin(fname, data)
        elif fname_lower.startswith("hb_") and fname_lower.endswith(".txt"):
            # Per-heartbeat notification file uploaded by Collector
            self._handle_heartbeat_line(fname, data)
        elif fname_lower.startswith("status_") and fname_lower.endswith(".txt"):
            # Sensor status file uploaded by Collector (KEY=VALUE format or JSON wrapper)
            self._handle_status_file(fname, data)
        elif "heartbeat" in fname_lower and fname_lower.endswith(".csv"):
            # Whole heartbeat CSV (direct-to-Root mode, processed via incremental path)
            # This branch handles legacy ingest-wrapped heartbeat CSVs
            self._handle_heartbeat_csv_incremental(data)
        elif fname_lower.endswith("_srsp.csv"):
            # Legacy SRSP file already in CSV format
            self._handle_status_file(fname, data)
        else:
            self._pr(f"[ROOT] Unknown file type, ignoring: {fname}")

    def _handle_measurement_bin(self, fname: str, data: bytes):
        """
        Save a .bin measurement file to the local hat_sensors measurements
        folder so that processMeasurements() can pick it up.

        Files uploaded by collectors are named:
            <millis>_<YYYYMMDD>_<HHMMSS>_<sensorSN>.bin
        We extract the original name (part after the first underscore) so the
        existing checkMeasurementFileValidity() regex matches.
        """
        # Strip leading millis-timestamp prefix added by /ingest handler.
        # Pattern: <digits>_<original_name>  e.g. "1234567890_20251027_101710_90000001.bin"
        m = re.match(r'^(\d+)_(.+)$', fname)
        original_name = m.group(2) if m else fname

        dest = os.path.join(self.measurements_folder, original_name)
        if os.path.exists(dest):
            self._pr(f"[ROOT] Measurement file already exists locally, skipping: {original_name}")
            return
        with open(dest, "wb") as f:
            f.write(data)
        self._pr(f"[ROOT] Saved measurement file: {original_name} ({len(data)} bytes)")

    def _handle_heartbeat_csv_incremental(self, data: bytes):
        """
        Process heartbeat_api.csv incrementally.
        Only processes lines that haven't been processed yet in previous cycles.
        Format per line: <timestamp>,<sensorSN>[,<ip>]
        """
        text = data.decode("utf-8", errors="replace")
        lines = [l.strip() for l in text.splitlines() if l.strip()]
        new_lines = lines[self._hb_csv_lines_seen:]
        if not new_lines:
            return
        self._pr(f"[ROOT] heartbeat_api.csv: processing {len(new_lines)} new line(s)")
        updated_sns = set()
        for line in new_lines:
            parts = line.split(",")
            if len(parts) < 2:
                continue
            sensor_sn = parts[1].strip()
            if not sensor_sn or sensor_sn in updated_sns:
                continue
            sensor = self.dbm.getSensorBySerialNumber(sensor_sn)
            if sensor is None:
                self._pr(f"[ROOT] Heartbeat for unknown SN {sensor_sn}, skipping")
                continue
            self.dbm.updateSensorLatestHeartbeatOn(sensor.ID)
            self._pr(f"[ROOT] Updated heartbeat (csv) for SN={sensor_sn}")
            updated_sns.add(sensor_sn)
        self._hb_csv_lines_seen = len(lines)

    def _handle_heartbeat_line(self, fname: str, data: bytes):
        """
        Process a single-line heartbeat notification file (hb_<SN>_<ts>.txt).
        Format: <timestamp>,<sensorSN>[,<ip>]
        Written by Collector via notifyRoot().
        """
        text = data.decode("utf-8", errors="replace").strip()
        parts = text.split(",")
        if len(parts) < 2:
            self._pr(f"[ROOT] Heartbeat line too short, skipping: {fname}")
            return
        sensor_sn = parts[1].strip()
        if not sensor_sn:
            self._pr(f"[ROOT] Heartbeat line missing SN, skipping: {fname}")
            return
        sensor = self.dbm.getSensorBySerialNumber(sensor_sn)
        if sensor is None:
            self._pr(f"[ROOT] Heartbeat for unknown SN {sensor_sn}, skipping")
            return
        self.dbm.updateSensorLatestHeartbeatOn(sensor.ID)
        self._pr(f"[ROOT] Updated heartbeat for SN={sensor_sn} (from {fname})")

    @staticmethod
    def _parse_status_keyvalue(raw: str) -> dict:
        """
        Parse a sensor STATUS string into a dict.

        Accepts both formats:
          - Raw key=value: ``MODE=STATION_Mode,FIRMWARE_VERSION=1.14,S/N=12345,...``
          - JSON wrapper:  ``{"res":"OK","data":"MODE=...,S/N=12345,..."}``

        Returns a dict like sensorsdaemon's ``status_dict`` (same key names).
        """
        # Try JSON wrapper first
        data_str = raw
        stripped = raw.strip()
        if stripped.startswith("{"):
            try:
                obj = json.loads(stripped)
                data_str = obj.get("data", raw)
            except json.JSONDecodeError:
                pass  # fall back to treating the whole string as KEY=VALUE

        result = {}
        for part in data_str.split(","):
            eq = part.find("=")
            if eq <= 0:
                continue
            k = part[:eq].strip()
            v = part[eq + 1:].strip()
            if k:
                result[k] = v
        return result

    def _handle_status_file(self, fname: str, data: bytes):
        """
        Process a sensor status file.

        Accepts:
          - Collector-uploaded status_<SN>_<ts>.txt  (KEY=VALUE or JSON-wrapped KEY=VALUE)
          - Legacy _SRSP.csv  (sensorSN,batteryLevel,firmwareVersion,wifi_signal,ssid,mac)

        Updates the DB and writes SRSP files for the PHP frontend.
        """
        raw = data.decode("utf-8", errors="replace").strip()

        # Determine SN: prefer parsing from content, fall back to filename
        fname_lower = fname.lower()

        if fname_lower.endswith("_srsp.csv"):
            # Legacy CSV format: SN,battery,fw,signal,ssid,mac
            parts = raw.split(",")
            if len(parts) < 6:
                self._pr(f"[ROOT] SRSP CSV too short, skipping: {fname}")
                return
            sensor_sn = parts[0].strip()
            status_dict = {
                "FIRMWARE_VERSION": parts[2].strip(),
                "WIFI_SIGNAL":      parts[3].strip(),
                "SSID":             parts[4].strip(),
                "MAC_ADDRESS":      parts[5].strip(),
            }
        else:
            # KEY=VALUE format (possibly JSON-wrapped)
            kv = self._parse_status_keyvalue(raw)
            if not kv:
                self._pr(f"[ROOT] Status file empty/unparseable: {fname}")
                return
            # S/N field in status string
            sensor_sn = kv.get("S/N", "").strip()
            if not sensor_sn:
                # Try to extract SN from filename: status_<SN>_<ts>.txt
                m = re.match(r'^status_(\d+)_', fname)
                sensor_sn = m.group(1) if m else ""
            if not sensor_sn:
                self._pr(f"[ROOT] Cannot determine sensor SN from status file: {fname}")
                return

            # Compute WIFI_SIGNAL from RSSI if not present
            wifi_signal = kv.get("WIFI_SIGNAL", "")
            if not wifi_signal and "RSSI" in kv:
                try:
                    rssi = int(kv["RSSI"])
                    RSSI_MIN, RSSI_MAX = -100, -50
                    if rssi <= RSSI_MIN:
                        wifi_signal = "0"
                    elif rssi >= RSSI_MAX:
                        wifi_signal = "100"
                    else:
                        wifi_signal = str(int((rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN)))
                except ValueError:
                    wifi_signal = "0"

            status_dict = {k: v for k, v in kv.items()
                           if k not in ("MODE", "S/N", "MASK", "GATEWAY", "RSSI")}
            status_dict["WIFI_SIGNAL"] = wifi_signal

        sensor = self.dbm.getSensorBySerialNumber(sensor_sn)
        if sensor is None:
            self._pr(f"[ROOT] Status for unknown SN {sensor_sn}, skipping")
            return

        self.dbm.updateSensorStatus(sensor.ID, status_dict)
        self._pr(f"[ROOT] Updated status for SN={sensor_sn} fw={status_dict.get('FIRMWARE_VERSION', '?')}")

        # Refresh sensor object for SRSP file generation
        sensor = self.dbm.getSensorByID(sensor.ID)
        self._write_srsp_file(sensor, status_dict)

    # ── SRSP file for PHP frontend ─────────────────────────────────────────────

    def _write_srsp_file(self, sensor, status_dict: dict):
        """
        Write a <timestamp>_<SN>_SRSP.csv file to sensor_responses_folder
        so the PHP frontend can display the sensor's latest status.

        Format: <sensorSN>,<batteryLevel>,<firmwareVersion>,<wifi_signal>,<ssid>,<mac>
        Mirrors appendToSensorStatusLog() in sensorsdaemon.py.
        """
        now_utc = datetime.now(timezone.utc)
        now_str = now_utc.strftime("%Y-%m-%d-%H%M%S-%f")[:-3]
        filename = f"{now_str}_{sensor.sensorSN}_SRSP.csv"
        filepath = os.path.join(self.sensor_responses_folder, filename)

        # Battery level from voltage
        try:
            bv = float(status_dict.get("BATTERY_VOLTAGE", 0))
        except (ValueError, TypeError):
            bv = 0.0
        mapping = [(3.00, 0), (3.26, 50), (3.30, 100)]
        if bv <= mapping[0][0]:
            battery_level = mapping[0][1]
        elif bv >= mapping[-1][0]:
            battery_level = mapping[-1][1]
        else:
            battery_level = 0
            for i in range(len(mapping) - 1):
                v1, l1 = mapping[i]
                v2, l2 = mapping[i + 1]
                if v1 <= bv <= v2:
                    battery_level = l1 + (bv - v1) * (l2 - l1) / (v2 - v1)
                    break
        battery_level = max(0, min(100, round(battery_level)))

        line = (f"{sensor.sensorSN},{battery_level},"
                f"{status_dict.get('FIRMWARE_VERSION', 'N/A')},"
                f"{status_dict.get('WIFI_SIGNAL', '0')},"
                f"{status_dict.get('SSID', 'N/A')},"
                f"{status_dict.get('MAC_ADDRESS', 'N/A')}")
        try:
            with open(filepath, "w") as f:
                f.write(line + "\n")
            self._pr(f"[ROOT] Wrote SRSP file: {filename}")
        except Exception as e:
            self._pr(f"[ROOT] Failed to write SRSP file {filename}: {e}")
            return

        # Housekeeping: keep only last 20 SRSP files per sensor
        try:
            sn_str = str(sensor.sensorSN)
            all_srsp = sorted([
                fn for fn in os.listdir(self.sensor_responses_folder)
                if fn.endswith("_SRSP.csv") and len(fn.split("_")) > 1 and fn.split("_")[1] == sn_str
            ])
            for old in all_srsp[:-20]:
                os.remove(os.path.join(self.sensor_responses_folder, old))
        except Exception:
            pass

    # ── PUSH: generate and upload job files ────────────────────────────────────

    def _push_jobs(self):
        """Generate config/firmware job files and upload them to Root."""
        self._push_config_jobs()
        self._push_firmware_jobs()

    def _push_config_jobs(self):
        """
        Build config_jobs.json from sensors with flag_update_configuration=1
        and upload to Root.  If no jobs exist, remove the file from Root.
        """
        try:
            sensors = self.dbm.dba.getDBObjects(
                "sensors",
                "`flag_update_configuration` = 1"
            )
        except Exception as e:
            logger.warning(f"[ROOT] DB query for config jobs failed: {e}")
            return

        if not sensors:
            return

        jobs = []
        for sensor in sensors:
            profile = self.dbm.getSensorProfileByID(sensor.profileID)
            if profile is None:
                continue

            params = {
                "sleep_time_after_sec_Station_mode": profile.sleep_time_after_sec,
                "wakeup_every_min":                  profile.wakeup_every_min,
                "temp_threshold":                    round(profile.temperature_threshold),
                "send_heartbeat_every_sec":          profile.send_heartbeat_every_sec,
                "start_time_for_accel_data_sec":     profile.start_time_for_accel_data_sec,
                "send_accel_data_every_min":         profile.send_accel_data_every_min,
                "vibration_threshold_in_mg":         profile.vibration_threshold,
                "vibration_threshold_frequency_in_Hz": profile.vibration_threshold_frequency,
                "acc_data_measure_time":             profile.acc_data_measure_time,
                "accel_full_scale":                  profile.accel_full_scale,
            }
            if sensor.name:
                params["name"] = sensor.name

            jobs.append({"sn": str(sensor.sensorSN), "params": params})

        if not jobs:
            return

        payload = {"jobs": jobs}
        ok = self.root.upload_json(self.JOBS_CFG_PATH, payload)
        if ok:
            self._pr(f"[ROOT] Uploaded config_jobs.json ({len(jobs)} job(s))")
            # Clear the flag: the job has been delivered to Root; Collector will execute it.
            # This mirrors sensorsdaemon.clearSensorUpdateConfigurationFlag after sending CONFIGURE.
            for job in jobs:
                sn = job["sn"]
                sensor = self.dbm.getSensorBySerialNumber(sn)
                if sensor is not None:
                    self.dbm.clearSensorUpdateConfigurationFlag(sensor.ID)
                    self._pr(f"[ROOT] Cleared flag_update_configuration for SN={sn}")
        else:
            self._pr(f"[ROOT] Failed to upload config_jobs.json")

    def _push_firmware_jobs(self):
        """
        Build firmware_jobs.json from sensors where firmware_version != 0 and
        stat_firmware_version does not match the target version.
        Also uploads the hex file to Root if not already present.
        """
        try:
            sensors = self.dbm.dba.getDBObjects(
                "sensors",
                "`firmware_version` != 0"
            )
        except Exception as e:
            logger.warning(f"[ROOT] DB query for firmware jobs failed: {e}")
            return

        if not sensors:
            return

        jobs = []
        for sensor in sensors:
            fw = self.dbm.getFirmwareByID(sensor.firmware_version)
            if fw is None:
                continue

            # Compute target firmware version string
            fw_major = sensor.firmware_version // 10000
            fw_minor = sensor.firmware_version % 10000
            fw_str = f"{fw_major}.{fw_minor:02d}"

            current_fw = getattr(sensor, "stat_firmware_version", "") or ""
            if current_fw == fw_str:
                continue  # already up to date

            # Determine hex file path on Root's SD
            hex_filename = f"fw_{fw_str.replace('.', '_')}.hex"
            hex_remote   = f"/firmware/{hex_filename}"

            # Upload hex content to Root if needed
            if fw.source:
                # fw.source may be str (Intel HEX text) or bytes; handle both
                if isinstance(fw.source, bytes):
                    hex_data = fw.source
                else:
                    hex_data = fw.source.encode("ascii", errors="replace")
                ok = self.root.upload_file(hex_remote, hex_data)
                if ok:
                    self._pr(f"[ROOT] Uploaded firmware hex: {hex_remote}")
                else:
                    self._pr(f"[ROOT] Failed to upload firmware hex: {hex_remote}")
                    continue

            jobs.append({
                "sn":       str(sensor.sensorSN),
                "hex_path": hex_remote,
                "timeout_ms": 8 * 60 * 1000,
            })

        if not jobs:
            return

        payload = {"jobs": jobs}
        ok = self.root.upload_json(self.JOBS_FW_PATH, payload)
        if ok:
            self._pr(f"[ROOT] Uploaded firmware_jobs.json ({len(jobs)} job(s))")
        else:
            self._pr(f"[ROOT] Failed to upload firmware_jobs.json")

    # ── main loop ──────────────────────────────────────────────────────────────

    def run(self, stop_event: Event):
        """
        Main polling loop.  Call from a Thread; stops when stop_event is set.
        """
        self._pr(f"[ROOT] rootdaemon started (host={self.root.base_url}, "
                 f"poll={self._poll_interval}s)")

        # Wait for Root to become reachable (max 60 s at start)
        deadline = time.time() + 60
        while time.time() < deadline and not stop_event.is_set():
            if self.root.health():
                self._pr("[ROOT] Root node is reachable.")
                break
            self._pr("[ROOT] Waiting for Root node to come online...")
            stop_event.wait(5)

        while not stop_event.is_set():
            try:
                self._pull_received_files()
                self._push_jobs()
            except Exception as e:
                logger.exception(f"[ROOT] Unexpected error in polling loop: {e}")

            stop_event.wait(self._poll_interval)

        self._pr("[ROOT] rootdaemon stopped.")
