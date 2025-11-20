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
void sjm_cleanupStaleDoneFiles();
