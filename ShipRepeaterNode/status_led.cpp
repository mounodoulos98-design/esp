#include "config.h"

// ── Status LED (serial-only — no NeoPixel on the Waveshare ESP32-C6) ──

static Status currentStatus = STATUS_BOOTING;

void setupStatusLed() {
  // Nothing to initialise — status is reported via Serial.
  setStatusLed(STATUS_BOOTING);
}

void setStatusLed(Status newStatus) {
  if (currentStatus == newStatus && newStatus != STATUS_BOOTING) return;
  currentStatus = newStatus;

  const char* label;
  switch (newStatus) {
    case STATUS_BOOTING:          label = "BOOTING";          break;
    case STATUS_CONFIG_MODE:      label = "CONFIG_MODE";      break;
    case STATUS_OPERATIONAL_IDLE: label = "OPERATIONAL_IDLE"; break;
    case STATUS_WIFI_ACTIVITY:    label = "WIFI_ACTIVITY";    break;
    case STATUS_RECEIVING_DATA:   label = "RECEIVING_DATA";   break;
    case STATUS_SENDING_DATA:     label = "SENDING_DATA";     break;
    case STATUS_ERROR:            label = "ERROR";            break;
    case STATUS_SLEEPING:         label = "SLEEPING";         break;
    default:                      label = "UNKNOWN";          break;
  }
  Serial.printf("[STATUS] %s\n", label);
}

void loopStatusLed() {
  // No-op — no LED hardware to drive.
}