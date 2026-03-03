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
