#ifndef CONFIG_H
#define CONFIG_H

// **ΑΦΑΙΡΕΣΑΜΕ ΤΟ ΛΑΘΟΣ #include "config.h" ΑΠΟ ΕΔΩ**

#include <Arduino.h>
#include <painlessMesh.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <queue>
#include <list>
#include <SPI.h>
#include <SdFat.h>
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include <sys/time.h>
#include <ArduinoJson.h> // **ΤΟ ΒΑΖΟΥΜΕ ΕΔΩ ΓΙΑ ΝΑ ΕΙΝΑΙ ΔΙΑΘΕΣΙΜΟ ΠΑΝΤΟΥ**

// Hardware
// GPIO9 is the physical BOOT button on the Waveshare ESP32-C6 board.
// (ESP32-C6 GPIOs are 0-30; GPIO38 does not exist on this chip.)
#define BOOT_BUTTON_PIN          9
// Waveshare ESP32-C6-DEV: WS2812B RGB LED is on GPIO8.
// GPIO0 is the Flash CS0 pin and must not be used as GPIO.
#define NEOPIXEL_PIN             8
// No separate power-enable pin on the Waveshare ESP32-C6-DEV board.
// Set to -1 to disable the power-pin init in setupStatusLed().
#define NEOPIXEL_POWER_PIN      -1
#define NEOPIXEL_BRIGHTNESS     20

// SD card SPI pins – Waveshare ESP32-C6 (16 MB Flash)
// Breakout wiring: CLK→GPIO19, DO(MISO)→GPIO20, DI(MOSI)→GPIO21, CS→GPIO18
#define SD_SCK_PIN              19   // FSPICLK  / SDIO_CLK
#define SD_MISO_PIN             20   // FSPID    / SDIO_DATA0  (SD DO)
#define SD_MOSI_PIN             21   // FSPIQ    / SDIO_DATA1  (SD DI)
#define SD_CS_PIN               18   // FSPICS0  / SDIO_CMD

// Network Defaults
#define ROOT_AP_SSID            "Root_AP"
#define ROOT_AP_PASSWORD        "rootpassword"
#define REPEATER_AP_SSID        "Repeater_AP"
#define REPEATER_AP_PASSWORD    "repeaterpassword"
#define UPLINK_HOST_DEFAULT     "192.168.10.1"   // Root IP (or Repeater gateway)
#define UPLINK_PORT_DEFAULT     8080                // Root AsyncWebServer port for /ingest

#define MESH_SSID               "ShipBackboneMesh"
#define MESH_PASSWORD           "aVerySecurePassword"
#define MESH_PORT               5555
#define CONFIG_AP_SSID_PREFIX   "Repeater_Setup_"
#define CONFIG_AP_PASSWORD      "repeaterconfig"
#define SENSOR_AP_PASSWORD      "sensorpassword"
#define TCP_SERVER_PORT         3000

// Timing Defaults
#define MESH_APPOINTMENT_INTERVAL_M 15
#define MESH_APPOINTMENT_WINDOW_S   60
#define COLLECTOR_AP_CYCLE_S        120
#define COLLECTOR_AP_WINDOW_S       1200
#define COLLECTOR_DATA_TIMEOUT_S    1200
#define INITIAL_SYNC_TIMEOUT_MS     180000

// BOOT button sampling windows (milliseconds).
// Cold boot / hardware reset: give the user a comfortable 2-second window to
// press BOOT before setup() commits to operational mode.
// Scheduled (timer/BLE/WiFi) wakeup: 50 ms — fast resume is more important.
#define BOOT_WINDOW_COLD_MS   2000UL
#define BOOT_WINDOW_SCHED_MS    50UL
// Loop-based BOOT hold thresholds:
//   2 s → restart into Config Mode (settings preserved via RTC flag)
//   5 s → factory reset + Config Mode
#define BOOT_HOLD_CONFIG_MS   2000UL
#define BOOT_HOLD_RESET_MS    5000UL

// Buffer/File Defaults
#define SD_CHUNK_SIZE           4096
#define MESH_CHUNK_SIZE         1024
#define MESSAGE_CACHE_SIZE      10
#define SENSOR_DATA_FILENAME    "/sensordata.bin"

// Enums
enum UplinkRoute { UPLINK_DIRECT, UPLINK_VIA_REPEATER };
enum NodeRole { ROLE_REPEATER, ROLE_COLLECTOR, ROLE_ROOT };
enum Status { STATUS_BOOTING, STATUS_CONFIG_MODE, STATUS_OPERATIONAL_IDLE, STATUS_WIFI_ACTIVITY, STATUS_RECEIVING_DATA, STATUS_SENDING_DATA, STATUS_ERROR, STATUS_SLEEPING };
enum State { STATE_INITIAL, STATE_COLLECTOR_AP, STATE_MESH_APPOINTMENT };

// Configuration Structure
struct NodeConfig {
  String apIP   = ""; // AP IP (if empty: Root 192.168.10.1, Repeater 192.168.20.1)

  String apPASS = ""; // AP PASS (if <8 => open AP)

  String apSSID = ""; // AP SSID for ROOT/REPEATER

  // Uplink configuration
  String uplinkSSID = ROOT_AP_SSID;
  String uplinkPASS = ROOT_AP_PASSWORD;
  String uplinkHost = UPLINK_HOST_DEFAULT;
  int uplinkPort = UPLINK_PORT_DEFAULT;
  int uplinkRoute = UPLINK_DIRECT; // UPLINK_DIRECT or UPLINK_VIA_REPEATER
  String nodeName = "Unconfigured";
  String sensorAP_SSID = "SensorAP";
  int role = ROLE_REPEATER;
  int collectorApCycleSec = COLLECTOR_AP_CYCLE_S;
  int collectorApWindowSec = COLLECTOR_AP_WINDOW_S;
  int collectorDataTimeoutSec = COLLECTOR_DATA_TIMEOUT_S;
  int meshIntervalMin = MESH_APPOINTMENT_INTERVAL_M;
  int meshWindowSec = MESH_APPOINTMENT_WINDOW_S;
  bool isConfigured = false;
  
  // BLE Mesh Wake-up Configuration
  bool bleBeaconEnabled = true;  // Enable BLE beacon for parent discovery (Repeater/Root)
  int bleScanDurationSec = 5;    // Duration to scan for parent nodes (Collector/Repeater)
};

// Extern declarations
extern painlessMesh mesh;
extern AsyncWebServer server;
extern DNSServer dnsServer;
extern Preferences preferences;
extern Scheduler userScheduler;
extern Adafruit_NeoPixel pixel;
extern SdFat sd;
extern NodeConfig config;
extern SemaphoreHandle_t sdCardMutex;
extern bool isOperationalMode;

// Prototypes
void loadConfiguration();
void saveConfiguration();
void factoryReset();
void startConfigurationMode();
void loopConfigurationMode();
void startOperationalMode();
void loopOperationalMode();
void setupStatusLed();
void setStatusLed(Status newStatus);
void loopStatusLed();
bool initSdCard();
void persistRtcTime(time_t epoch);
time_t restoreRtcTime();

#endif // CONFIG_H
