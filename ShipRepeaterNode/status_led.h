#ifndef STATUS_LED_H
#define STATUS_LED_H
#include "config.h"

void setupStatusLed();
void setStatusLed(Status newStatus);
void loopStatusLed();

#endif