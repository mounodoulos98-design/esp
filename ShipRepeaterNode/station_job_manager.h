#pragma once

#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>

struct PendingStation {
    String mac;
    String ip;
    uint32_t connectedAtMillis;
};

void sjm_init();
void sjm_addStation(const String& mac);
void sjm_processStations();

// Helper functions for async heartbeat-driven job execution
bool sjm_requestStatus(const String& ip, String& snOut);
bool processJobsForSN(const String& sn, const String& ip);
