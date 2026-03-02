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

        # Track files already downloaded (persisted in a small file)
        self._seen_file = os.path.join(sensor_status_folder, ".root_seen_files.json")
        self._seen: set = self._load_seen()

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

        new_files = [f for f in files if f not in self._seen]
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
            # Measurement binary file from sensor
            self._handle_measurement_bin(fname, data)
        elif "heartbeat" in fname_lower and fname_lower.endswith(".csv"):
            # Heartbeat log file uploaded by collector
            self._handle_heartbeat_csv(fname, data)
        elif fname_lower.endswith("_srsp.csv") or fname_lower.startswith("status_"):
            # Sensor status response file
            self._handle_status_csv(fname, data)
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
        import re
        m = re.match(r'^(\d+)_(.+)$', fname)
        original_name = m.group(2) if m else fname

        dest = os.path.join(self.measurements_folder, original_name)
        if os.path.exists(dest):
            self._pr(f"[ROOT] Measurement file already exists locally, skipping: {original_name}")
            return
        with open(dest, "wb") as f:
            f.write(data)
        self._pr(f"[ROOT] Saved measurement file: {original_name} ({len(data)} bytes)")

    def _handle_heartbeat_csv(self, fname: str, data: bytes):
        """
        Parse heartbeat CSV lines and update the DB for each sensor SN.

        Collector format: <timestamp>,<sensorSN>,<sensorIP>
        or legacy format: <timestamp>,<sensorSN>
        """
        text = data.decode("utf-8", errors="replace")
        updated_sns = set()
        for line in text.splitlines():
            parts = line.strip().split(",")
            if len(parts) < 2:
                continue
            sensor_sn = parts[1].strip()
            if not sensor_sn:
                continue
            if sensor_sn in updated_sns:
                continue
            sensor = self.dbm.getSensorBySerialNumber(sensor_sn)
            if sensor is None:
                self._pr(f"[ROOT] Heartbeat for unknown SN {sensor_sn}, skipping")
                continue
            self.dbm.updateSensorLatestHeartbeatOn(sensor.ID)
            self._pr(f"[ROOT] Updated heartbeat for SN={sensor_sn}")
            updated_sns.add(sensor_sn)

    def _handle_status_csv(self, fname: str, data: bytes):
        """
        Parse a sensor status response CSV and update the DB.

        Format: <sensorSN>,<batteryLevel>,<firmwareVersion>,<wifi_signal>,<ssid>,<mac>
        """
        text = data.decode("utf-8", errors="replace").strip()
        parts = text.split(",")
        if len(parts) < 6:
            self._pr(f"[ROOT] Status CSV too short, skipping: {fname}")
            return

        sensor_sn        = parts[0].strip()
        firmware_version = parts[2].strip()
        wifi_signal      = parts[3].strip()
        ssid             = parts[4].strip()
        mac_address      = parts[5].strip()

        sensor = self.dbm.getSensorBySerialNumber(sensor_sn)
        if sensor is None:
            self._pr(f"[ROOT] Status for unknown SN {sensor_sn}, skipping")
            return

        status_dict = {
            "FIRMWARE_VERSION": firmware_version,
            "SSID": ssid,
            "MAC_ADDRESS": mac_address,
            "WIFI_SIGNAL": wifi_signal,
        }
        self.dbm.updateSensorStatus(sensor.ID, status_dict)
        self._pr(f"[ROOT] Updated status for SN={sensor_sn} fw={firmware_version}")

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
            # Mark sensors as pending (flag stays 1 until sensor confirms via status response)
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
