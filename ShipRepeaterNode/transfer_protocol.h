#ifndef TRANSFER_PROTOCOL_H
#define TRANSFER_PROTOCOL_H

#include <Arduino.h>

// Flag-based transfer protocol between Collector and Repeater
// Similar to Collector-Sensor protocol for consistency

// Transfer states
enum TransferState {
    TRANSFER_IDLE,
    TRANSFER_COLLECTOR_SENDING,    // Collector uploading files to Repeater
    TRANSFER_REPEATER_SENDING,     // Repeater sending jobs to Collector
    TRANSFER_COMPLETE
};

// Protocol flags (sent as HTTP headers or query parameters)
struct TransferFlags {
    int filesToUpload = 0;          // Collector: "I have N files"
    bool readyToReceive = false;     // Repeater: "Ready to receive"
    bool uploadComplete = false;     // Collector: "All files uploaded"
    
    int jobsToSend = 0;             // Repeater: "I have M jobs"
    bool readyToDownload = false;    // Collector: "Ready to download"
    bool downloadComplete = false;   // Repeater: "All jobs sent"
    
    bool transferDone = false;       // Both: "All done"
};

// Helper functions for protocol implementation

// Count files in queue directory
static inline int countQueueFiles() {
    extern SdFat sd;
    extern bool initSdCard();
    
    if (!initSdCard()) return 0;
    
    const char* QUEUE_DIR = "/queue";
    FsFile dir = sd.open(QUEUE_DIR);
    if (!dir) return 0;
    
    int count = 0;
    while (true) {
        FsFile f = dir.openNextFile();
        if (!f) break;
        if (!f.isDir() && String(f.getName()).endsWith(".bin")) {
            count++;
        }
        f.close();
    }
    dir.close();
    
    return count;
}

// Check if jobs exist for collector
static inline int countJobsForCollector() {
    extern SdFat sd;
    extern bool initSdCard();
    
    if (!initSdCard()) return 0;
    
    int jobCount = 0;
    
    // Check for config jobs
    if (sd.exists("/jobs/config_jobs.json")) {
        jobCount++;
    }
    
    // Check for firmware jobs
    if (sd.exists("/jobs/firmware_jobs.json")) {
        jobCount++;
    }
    
    return jobCount;
}

#endif // TRANSFER_PROTOCOL_H
