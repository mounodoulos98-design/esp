"""
Central numeric codes shared across DB, daemon, and GUI.
Aligned with the current MariaDB schema.

Usage:
    from daemon.codes import (
        EVT_BATCH, ORIGIN_SENSOR, MODE_APP, CMD_QUEUED, FW_INPROG, ...
    )
"""

# --- Event types (hat_sensor_events.event_type) ---
EVT_UNKNOWN    = 0
EVT_STATUS     = 1
EVT_BATCH      = 2
EVT_HEARTBEAT  = 3

# --- Origins (hat_sensor_events.origin) ---
ORIGIN_UNKNOWN = 0
ORIGIN_SENSOR  = 1
ORIGIN_DAEMON  = 2

# --- Source (hat_sensor_events.source) ---
SOURCE_UNKNOWN          = 0
SOURCE_STATUS_ENDPOINT  = 1
SOURCE_BATCH_ENDPOINT   = 2
SOURCE_DAEMON           = 3

# --- Modes (status tables & payloads) ---
MODE_UNKNOWN   = 0
MODE_APP       = 1
MODE_BL        = 2

# --- Command visibility policy (by mode) ---
VIS_UNKNOWN    = 0
VIS_APP        = 1
VIS_BL         = 2
VIS_BOTH       = 3

# --- Command queue statuses (hat_sensor_commands.status) ---
CMD_QUEUED     = 1
CMD_SENDING    = 2
CMD_ACKED      = 3
CMD_FAILED     = 4
CMD_SUPERSEDED = 5

# --- Firmware job statuses (hat_firmware_jobs.status) ---
FW_QUEUED      = 1
FW_INPROG      = 2
FW_VERIFY      = 3
FW_OK          = 4
FW_FAIL        = 5
FW_ABORT       = 6

# --- Audit event types (hat_audit_events.type) ---
AUDIT_WORKER   = 1
AUDIT_STATUS   = 2
AUDIT_COMMAND  = 3
AUDIT_OTA      = 4
AUDIT_ERROR    = 5

# --- Trigger sources (hat_sensor_awake_sessions.trigger_source) ---
TRIG_SRC_BATCH = 1
TRIG_SRC_HEARTBEAT = 2

# ---- Optional: small name maps useful in logs/debug ----
EVENT_TYPE_NAME = {
    EVT_UNKNOWN: "unknown",
    EVT_STATUS: "status",
    EVT_BATCH: "batch",
    EVT_HEARTBEAT: "heartbeat",
}

ORIGIN_NAME = {
    ORIGIN_UNKNOWN: "unknown",
    ORIGIN_SENSOR: "sensor",
    ORIGIN_DAEMON: "daemon",
}

SOURCE_NAME = {
    SOURCE_UNKNOWN: "unknown",
    SOURCE_STATUS_ENDPOINT: "status_endpoint",
    SOURCE_BATCH_ENDPOINT: "batch_endpoint",
    SOURCE_DAEMON: "daemon_probe",
}

MODE_NAME = {
    MODE_UNKNOWN: "unknown",
    MODE_APP: "app",
    MODE_BL: "bootloader",
}

VIS_NAME = {
    VIS_UNKNOWN: "unknown",
    VIS_APP: "app",
    VIS_BL: "bootloader",
    VIS_BOTH: "both",
}

CMD_STATUS_NAME = {
    CMD_QUEUED: "queued",
    CMD_SENDING: "sending",
    CMD_ACKED: "acked",
    CMD_FAILED: "failed",
    CMD_SUPERSEDED: "superseded",
}

FW_STATUS_NAME = {
    FW_QUEUED: "queued",
    FW_INPROG: "in_progress",
    FW_VERIFY: "verifying",
    FW_OK: "succeeded",
    FW_FAIL: "failed",
    FW_ABORT: "aborted",
}

AUDIT_NAME = {
    AUDIT_WORKER: "worker",
    AUDIT_STATUS: "status",
    AUDIT_COMMAND: "command",
    AUDIT_OTA: "ota",
    AUDIT_ERROR: "error",
}

TRIG_SRC_NAME = {
    TRIG_SRC_BATCH: "batch",
    TRIG_SRC_HEARTBEAT: "heartbeat",
}

# Common filter sets
TRIGGER_EVENT_TYPES = (EVT_BATCH, EVT_HEARTBEAT)

__all__ = [
    # events/origins/sources
    "EVT_STATUS", "EVT_BATCH", "EVT_HEARTBEAT",
    "ORIGIN_SENSOR", "ORIGIN_DAEMON",
    "SOURCE_UNKNOWN", "SOURCE_STATUS_ENDPOINT", "SOURCE_BATCH_ENDPOINT", "SOURCE_DAEMON",
    # modes/visibility
    "MODE_UNKNOWN", "MODE_APP", "MODE_BL",
    "VIS_UNKNOWN", "VIS_APP", "VIS_BL", "VIS_BOTH",
    # command & firmware statuses
    "CMD_QUEUED", "CMD_SENDING", "CMD_ACKED", "CMD_FAILED", "CMD_SUPERSEDED",
    "FW_QUEUED", "FW_INPROG", "FW_VERIFY", "FW_OK", "FW_FAIL", "FW_ABORT",
    # audit events
    "AUDIT_WORKER", "AUDIT_STATUS", "AUDIT_COMMAND", "AUDIT_OTA", "AUDIT_ERROR",
    # trigger sources
    "TRIG_SRC_BATCH", "TRIG_SRC_HEARTBEAT",
    # names & filters
    "EVENT_TYPE_NAME", "ORIGIN_NAME", "SOURCE_NAME",
    "MODE_NAME", "VIS_NAME", "CMD_STATUS_NAME", "FW_STATUS_NAME",
    "AUDIT_NAME", "TRIG_SRC_NAME",
    "TRIGGER_EVENT_TYPES",
]
