#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Configuration job description (per sensor SN)
struct ConfigJob {
    String sensorSn;   // e.g. "324269" (for logging)
    String sensorIp;   // resolved runtime via STATUS / station list
    JsonObject params; // reference to JSON "params" object
};

// Send CONFIGURE command to a sensor, using Diagnonic HTTP GET
bool cu_sendConfiguration(const ConfigJob& job);
