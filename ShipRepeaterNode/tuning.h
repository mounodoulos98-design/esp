#ifndef TUNING_H
#define TUNING_H

#include <Arduino.h>
#include <Preferences.h>

// Default tuning constants
#define HTTP_DEFAULT_TIMEOUT_MS       5000
#define HTTP_DEFAULT_RETRIES          3
#define STATUS_DELAY_MS_DEFAULT       2000
#define CONFIGURE_DELAY_MS_DEFAULT    2000
#define FW_LINE_DELAY_MS_DEFAULT      10
#define FW_PROGRESS_LOG_INTERVAL      50
#define JOB_CLEANUP_AGE_HOURS         24
#define SENSOR_CONTEXT_TIMEOUT_MS     1800000  // 30 minutes

// Firmware update validation
#ifndef FW_REQUIRE_OK_PER_LINE
#define FW_REQUIRE_OK_PER_LINE false
#endif

// Runtime tuning parameters (can be overridden via Preferences)
struct RuntimeTuning {
    unsigned long statusDelayMs;
    unsigned long configureDelayMs;
    unsigned long firmwareLineDelayMs;
    unsigned long httpTimeoutMs;
    int httpRetries;
    int fwProgressLogInterval;
    unsigned long jobCleanupAgeHours;
    unsigned long sensorContextTimeoutMs;
    bool fwRequireOkPerLine;

    RuntimeTuning() {
        statusDelayMs = STATUS_DELAY_MS_DEFAULT;
        configureDelayMs = CONFIGURE_DELAY_MS_DEFAULT;
        firmwareLineDelayMs = FW_LINE_DELAY_MS_DEFAULT;
        httpTimeoutMs = HTTP_DEFAULT_TIMEOUT_MS;
        httpRetries = HTTP_DEFAULT_RETRIES;
        fwProgressLogInterval = FW_PROGRESS_LOG_INTERVAL;
        jobCleanupAgeHours = JOB_CLEANUP_AGE_HOURS;
        sensorContextTimeoutMs = SENSOR_CONTEXT_TIMEOUT_MS;
        fwRequireOkPerLine = FW_REQUIRE_OK_PER_LINE;
    }
};

// Load runtime tuning from Preferences (namespace "tuning")
inline RuntimeTuning loadRuntimeTuning() {
    RuntimeTuning tuning;
    Preferences prefs;
    
    if (prefs.begin("tuning", true)) {  // read-only
        tuning.statusDelayMs = prefs.getULong("statusDelay", STATUS_DELAY_MS_DEFAULT);
        tuning.configureDelayMs = prefs.getULong("configDelay", CONFIGURE_DELAY_MS_DEFAULT);
        tuning.firmwareLineDelayMs = prefs.getULong("fwLineDelay", FW_LINE_DELAY_MS_DEFAULT);
        tuning.httpTimeoutMs = prefs.getULong("httpTimeout", HTTP_DEFAULT_TIMEOUT_MS);
        tuning.httpRetries = prefs.getInt("httpRetries", HTTP_DEFAULT_RETRIES);
        tuning.fwProgressLogInterval = prefs.getInt("fwProgressInt", FW_PROGRESS_LOG_INTERVAL);
        tuning.jobCleanupAgeHours = prefs.getULong("jobCleanAge", JOB_CLEANUP_AGE_HOURS);
        tuning.sensorContextTimeoutMs = prefs.getULong("sensorTimeout", SENSOR_CONTEXT_TIMEOUT_MS);
        tuning.fwRequireOkPerLine = prefs.getBool("fwRequireOk", FW_REQUIRE_OK_PER_LINE);
        prefs.end();
    }
    
    return tuning;
}

// Global tuning instance (to be initialized in main)
extern RuntimeTuning g_tuning;

#endif // TUNING_H
