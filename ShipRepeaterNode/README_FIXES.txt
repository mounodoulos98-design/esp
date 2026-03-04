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

Serial / USB settings
---------------------
BAUD RATE: 115200  <-- set this in your serial monitor (Arduino IDE, PuTTY, etc.)
                       Using 9600 or any other rate will show only garbage.

The Waveshare ESP32-C6 dev kit has two USB-C ports:
  - The port labelled "UART" uses a CH340 USB-to-UART chip → always use 115200 baud.
  - The port labelled "USB" exposes the ESP32-C6's native USB-CDC.
    The native USB port does not enforce a baud rate over USB, but many serial
    monitors still need to be configured to 115200.
Always select 115200 baud in your terminal before opening the port.

