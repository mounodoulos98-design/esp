#pragma once

#include "config.h"
#include <Arduino.h>

// Περιγραφή ενός firmware job που έρχεται από JSON
struct FirmwareJob {
  String sensorIp;        // π.χ. "192.168.4.2"
  String sensorSN;        // π.χ. "324269" (μόνο για logging)
  String hexPath;         // π.χ. "/firmware/vibration_sensor_app_v1.17.hex"
  uint32_t maxLines;      // 0 => όλες οι γραμμές
  uint32_t totalTimeoutMs; // συνολικό timeout (π.χ. 8 λεπτά)
  unsigned long lineRateLimitMs; // optional per-line delay override (0 = use default)
};

// Εκτελεί ολόκληρο το firmware update βάσει FirmwareJob.
// Γυρίζει true/false ανάλογα με το αν ολοκληρώθηκε χωρίς error.
bool executeFirmwareJob(const FirmwareJob& job);
