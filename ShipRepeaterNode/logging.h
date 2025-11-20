#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>

// Simple logging macros with levels
#define LOG_ERROR(tag, fmt, ...) Serial.printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  Serial.printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__)

#ifdef ENABLE_DEBUG_LOG
#define LOG_DEBUG(tag, fmt, ...) Serial.printf("[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#define LOG_DEBUG(tag, fmt, ...) do {} while(0)
#endif

#endif // LOGGING_H
