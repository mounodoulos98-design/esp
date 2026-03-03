!!! IMPORTANT – ARDUINO IDE SETUP (read this first) !!!
=======================================================

The sketch is ~1.62 MB which exceeds the ESP32-C6 default app-partition
limit of 1.25 MB.  You MUST configure the larger partition scheme before
compiling, otherwise you will see:

  "text section exceeds available space in board"
  "Sketch uses 1616816 bytes (123%) of program storage space.
   Maximum is 1310720 bytes."

──────────────────────────────────────────────────────────────────────────
ARDUINO IDE (any version) – ONE-TIME MANUAL STEPS
──────────────────────────────────────────────────────────────────────────
1. Tools → Board → ESP32 Arduino → ESP32C6 Dev Module
   (or whichever ESP32-C6 entry matches your board)
2. Tools → Flash Size  → 16MB (128Mb)
3. Tools → Partition Scheme → Custom
   ↑ This is the key step.  It tells the IDE to use the partitions.csv
     file in this sketch folder, giving a 3 MB app slot instead of 1.25 MB.

If "Custom" is not listed, choose:
   Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)
   (This also gives 3 MB but uses a built-in table; you lose OTA updates.)

Arduino IDE remembers these settings per-board, so you only need to do
this once.

──────────────────────────────────────────────────────────────────────────
ARDUINO IDE 2.2+ – PROFILE METHOD
──────────────────────────────────────────────────────────────────────────
A sketch.yaml with a pre-configured profile is already in this folder.
When you open the sketch, Arduino IDE 2.2+ shows a profile dropdown in
the toolbar.  Select "waveshare_esp32c6_16mb" and click Compile – no
manual Tools settings are needed.

──────────────────────────────────────────────────────────────────────────
arduino-cli – AUTOMATIC
──────────────────────────────────────────────────────────────────────────
The sketch.yaml default_fqbn is already set correctly.  Just run:
  arduino-cli compile --sketch ShipRepeaterNode/
No extra flags are needed.

=======================================================

ShipRepeaterNode – Fixed build (2025-10-13T13:18:00)
=================================================

What changed:
1) Correct RTC restore after deep sleep:
   - Use persisted + rtc_accum_sleep to avoid jumping back in time
   - Set rtc_sync_millis = millis() on wake
   - Keep rtcNoteSync() ONLY for real sync events (e.g., mesh time update)

2) Safer deep sleep sequence:
   - Clean WiFi shutdown: WiFi.disconnect(true) + WiFi.mode(WIFI_OFF) + small delay
   - Removed esp_task_wdt_deinit() before esp_deep_sleep_start()

3) Debounce sleeps with small buffers:
   - Added ~1.2s delay before going to sleep after data/timeout/window ends
   - Helps WiFi stack settle and prevents race-to-sleep

4) Extra debug prints:
   - [DEBUG RTC] logs on wake, scheduler decisions remain intact

Parameters:
- collectorApCycleSec: period between collector AP openings and base sleep after cycles
- collectorApWindowSec: strict AP window if no station connects
- collectorDataTimeoutSec: inactivity timeout once a station has connected

Files modified:
- op_mode.cpp (significant changes)
- other files kept intact

How to use:
- Open in Arduino IDE as before. No changes in filenames.
- For initial tests, consider using a slightly wider meshWindowSec and collectorApWindowSec.
