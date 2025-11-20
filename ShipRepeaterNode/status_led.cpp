#include "config.h"

static Status currentStatus = STATUS_BOOTING;
static unsigned long lastBlink = 0;
static bool ledState = false;

void setupStatusLed() {
  pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
  digitalWrite(NEOPIXEL_POWER_PIN, HIGH);
  delay(10);
  pixel.begin();
  pixel.setBrightness(NEOPIXEL_BRIGHTNESS);
  pixel.clear();
  pixel.show();
  setStatusLed(STATUS_BOOTING);
}

void setStatusLed(Status newStatus) {
  if (currentStatus == newStatus && newStatus != STATUS_BOOTING) return;
  currentStatus = newStatus;
  lastBlink = 0; 
  ledState = true; 
}

void loopStatusLed() {
  unsigned long interval = 1000;
  bool shouldBlink = false;
  uint32_t color = 0;

  switch (currentStatus) {
    case STATUS_BOOTING:           color = pixel.Color(255, 100, 0); break;
    case STATUS_CONFIG_MODE:       color = pixel.Color(0, 0, 255);   interval = 500; shouldBlink = true; break;
    case STATUS_OPERATIONAL_IDLE:  color = pixel.Color(0, 255, 0);   break;
    case STATUS_RECEIVING_DATA:    color = pixel.Color(0, 255, 255); interval = 100; shouldBlink = true; break;
    case STATUS_SENDING_DATA:      color = pixel.Color(255, 0, 255); interval = 100; shouldBlink = true; break;
    case STATUS_ERROR:             color = pixel.Color(255, 0, 0);   interval = 250; shouldBlink = true; break;
    case STATUS_SLEEPING:          color = 0; break;
    default:                       color = 0; break;
  }

  if (shouldBlink) {
    if (millis() - lastBlink > interval) {
      lastBlink = millis();
      ledState = !ledState;
      pixel.setPixelColor(0, ledState ? color : 0);
      pixel.show();
    }
  } else {
    if (pixel.getPixelColor(0) != color) {
      pixel.setPixelColor(0, color);
      pixel.show();
    }
  }
}