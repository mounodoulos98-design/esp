#include "http_utils.h"
#include "logging.h"
#include "config.h"
#include <WiFiClient.h>

extern SdFat sd;
extern bool initSdCard();

// URL encode implementation
String urlEncode(const String& str) {
    String encoded = "";
    char c;
    char code0;
    char code1;
    
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        if (c == ' ') {
            encoded += '+';
        } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            code1 = (c & 0xf) + '0';
            if ((c & 0xf) > 9) {
                code1 = (c & 0xf) - 10 + 'A';
            }
            c = (c >> 4) & 0xf;
            code0 = c + '0';
            if (c > 9) {
                code0 = c - 10 + 'A';
            }
            encoded += '%';
            encoded += code0;
            encoded += code1;
        }
    }
    return encoded;
}

// Unified HTTP GET with retry logic
bool httpGet(const String& host, 
             const String& path, 
             String& bodyOut, 
             unsigned long timeoutMs,
             int maxRetries,
             bool requireNonEmptyBody) {
    
    bodyOut = "";
    
    for (int attempt = 0; attempt < maxRetries; attempt++) {
        WiFiClient client;
        
        LOG_DEBUG("HTTP", "GET http://%s%s (attempt %d/%d)", 
                  host.c_str(), path.c_str(), attempt + 1, maxRetries);
        
        if (!client.connect(host.c_str(), 80)) {
            LOG_WARN("HTTP", "Connect failed to %s (attempt %d/%d)", 
                     host.c_str(), attempt + 1, maxRetries);
            if (attempt < maxRetries - 1) {
                delay(1000 * (attempt + 1));  // Exponential backoff
                continue;
            }
            return false;
        }
        
        String request = String("GET ") + path + " HTTP/1.1\r\n" +
                        "Host: " + host + "\r\n" +
                        "Connection: close\r\n\r\n";
        
        client.print(request);
        
        unsigned long start = millis();
        String response = "";
        
        while (millis() - start < timeoutMs) {
            while (client.available()) {
                char c = client.read();
                response += c;
            }
            if (!client.connected()) break;
            delay(1);
        }
        client.stop();
        
        // Extract body from response
        int headerEnd = response.indexOf("\r\n\r\n");
        if (headerEnd >= 0) {
            bodyOut = response.substring(headerEnd + 4);
        } else {
            bodyOut = response;
        }
        
        // Check if we got a valid response
        if (!requireNonEmptyBody || bodyOut.length() > 0) {
            LOG_DEBUG("HTTP", "Success, body length: %d", bodyOut.length());
            return true;
        }
        
        LOG_WARN("HTTP", "Empty body received (attempt %d/%d)", attempt + 1, maxRetries);
        if (attempt < maxRetries - 1) {
            delay(1000 * (attempt + 1));
        }
    }
    
    LOG_ERROR("HTTP", "All %d attempts failed for %s%s", maxRetries, host.c_str(), path.c_str());
    return false;
}

// HTTP multipart/form-data file upload
bool httpMultipartPostFile(const String& host, 
                           uint16_t port, 
                           const String& urlPath,
                           const String& fieldName, 
                           const String& filePath,
                           const String& contentType) {
    
    if (!initSdCard()) {
        LOG_ERROR("HTTP", "SD card init failed for upload");
        return false;
    }
    
    FsFile f = sd.open(filePath.c_str(), O_RDONLY);
    if (!f) {
        LOG_ERROR("HTTP", "Cannot open file: %s", filePath.c_str());
        return false;
    }
    
    f.seekEnd(0);
    uint32_t fsize = f.curPosition();
    f.rewind();
    
    WiFiClient client;
    LOG_INFO("HTTP", "Uploading %s (%lu bytes) to %s:%d%s", 
             filePath.c_str(), (unsigned long)fsize, host.c_str(), port, urlPath.c_str());
    
    if (!client.connect(host.c_str(), port)) {
        LOG_ERROR("HTTP", "Connect failed to %s:%d", host.c_str(), port);
        f.close();
        return false;
    }
    
    // Extract filename from path
    String filename = filePath;
    int lastSlash = filePath.lastIndexOf('/');
    if (lastSlash >= 0) {
        filename = filePath.substring(lastSlash + 1);
    }
    
    String boundary = "----esp32bound" + String(millis());
    String header = "POST " + urlPath + " HTTP/1.1\r\n";
    header += "Host: " + host + "\r\n";
    header += "Connection: close\r\n";
    header += "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n";
    
    String prePart = "--" + boundary + "\r\n";
    prePart += "Content-Disposition: form-data; name=\"" + fieldName + "\"; filename=\"" + filename + "\"\r\n";
    prePart += "Content-Type: " + contentType + "\r\n\r\n";
    
    String postPart = "\r\n--" + boundary + "--\r\n";
    
    uint32_t contentLength = prePart.length() + fsize + postPart.length();
    header += "Content-Length: " + String(contentLength) + "\r\n\r\n";
    
    client.print(header);
    client.print(prePart);
    
    // Upload file in chunks
    uint8_t buf[SD_CHUNK_SIZE];
    while (f.available()) {
        int rd = f.read(buf, sizeof(buf));
        if (rd > 0) {
            client.write(buf, rd);
        }
        delay(0);  // Yield to other tasks
    }
    
    client.print(postPart);
    f.close();
    
    // Wait for response
    unsigned long t0 = millis();
    while (client.connected() && millis() - t0 < 10000) {
        while (client.available()) {
            client.read();
            t0 = millis();
        }
        delay(10);
    }
    client.stop();
    
    LOG_INFO("HTTP", "Upload completed: %s", filename.c_str());
    return true;
}
