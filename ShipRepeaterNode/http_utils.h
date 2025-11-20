#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include "tuning.h"

// URL encode a string for safe use in query parameters
String urlEncode(const String& str);

// Unified HTTP GET with retry logic and timeout
// Returns true if successful, bodyOut contains the response body
bool httpGet(const String& host, 
             const String& path, 
             String& bodyOut, 
             unsigned long timeoutMs = HTTP_DEFAULT_TIMEOUT_MS,
             int maxRetries = HTTP_DEFAULT_RETRIES,
             bool requireNonEmptyBody = true);

// HTTP multipart/form-data file upload
// Used for uploading files to root/repeater endpoints
bool httpMultipartPostFile(const String& host, 
                           uint16_t port, 
                           const String& urlPath,
                           const String& fieldName, 
                           const String& filePath,
                           const String& contentType = "application/octet-stream");

#endif // HTTP_UTILS_H
