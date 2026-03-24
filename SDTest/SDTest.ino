// SDTest.ino  ─  Standalone SD card diagnostic for Waveshare ESP32-C6
// =====================================================================
// Upload this sketch by itself (without the main ShipRepeaterNode code)
// to verify that the SD card and wiring are working correctly.
//
// Expected wiring (same as ShipRepeaterNode):
//   SD Card   ESP32-C6 GPIO
//   CLK   →   GPIO 19
//   DO    →   GPIO 20  (MISO)
//   DI    →   GPIO 21  (MOSI)
//   CS    →   GPIO 18
//   VCC   →   3.3 V
//   GND   →   GND
//
// How to enter config mode on the main firmware (works from ALL roles):
//   • Power-on / hardware reset: hold BOOT button (GPIO9) LOW within 2 s
//   • While running normally:    hold BOOT button for 2 s  → Config Mode (settings kept)
//                                hold BOOT button for 5 s  → Factory reset + Config Mode
//
//   COLLECTOR: hold during the AP window (device is awake and loop runs fast).
//   REPEATER:  press and HOLD — the device wakes from light sleep on the first
//              press; keep holding for 2 s and config mode will trigger.
//   ROOT:      hold for 2 s at any time (loop runs continuously, no sleep).
//
// Open Serial Monitor at 115200 baud to see results.
// =====================================================================

#include <SPI.h>
#include <SdFat.h>

// ── Pin definitions (match ShipRepeaterNode/config.h) ────────────────
#define SD_SCK_PIN   19
#define SD_MISO_PIN  20
#define SD_MOSI_PIN  21
#define SD_CS_PIN    18

SdFat sd;

// ── Helpers ──────────────────────────────────────────────────────────

// Send 80 SPI clock cycles with CS HIGH to switch the SD card from
// SD-bus power-up mode into SPI mode (SD Physical Layer Spec §6.4.1).
static void sendInitClocks() {
  SPI.beginTransaction(SPISettings(250000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) SPI.transfer(0xFF); // 10 bytes × 8 clk = 80 clk
  SPI.endTransaction();
}

static bool tryInit(uint32_t mhz) {
  SPI.end();
  delay(50);

  // CS must be HIGH (idle) before touching the bus.
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(10);

  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  delay(20);

  // 74+ init clocks with CS HIGH per SD spec.
  sendInitClocks();
  delay(10);

  SdSpiConfig cfg(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(mhz));
  return sd.begin(cfg);
}

// ── Main sketch ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n============================");
  Serial.println("SD Card Diagnostic  (SDTest)");
  Serial.println("============================");
  Serial.printf("Pins: CLK=%d  MISO=%d  MOSI=%d  CS=%d\n",
                SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  Serial.println();

  // --- Step 1: try progressively slower speeds ---
  const uint32_t speeds[] = { 10, 8, 4, 2, 1 };
  bool ok = false;
  uint32_t goodSpeed = 0;

  for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
    Serial.printf("[SD] Trying %u MHz ... ", speeds[i]);
    if (i > 0) {
      sd.end();
      delay(300);
    }
    if (tryInit(speeds[i])) {
      goodSpeed = speeds[i];
      ok = true;
      Serial.println("OK ✓");
      break;
    } else {
      Serial.println("FAIL ✗");
    }
  }

  if (!ok) {
    Serial.println();
    Serial.println("============================================================");
    Serial.println("SD card FAILED to initialize at all speeds (10/8/4/2/1 MHz).");
    Serial.println("Possible causes:");
    Serial.println("  1. Wiring mistake – double-check CLK/MISO/MOSI/CS vs above");
    Serial.println("  2. SD card power: VCC must be 3.3 V (NOT 5 V)");
    Serial.println("  3. SD card not inserted / loose contact");
    Serial.println("  4. Faulty SD breakout (level-shifter dead?)");
    Serial.println("  5. SD card needs formatting (FAT32, ≤32 GB)");
    Serial.println("  6. Add 100 nF decoupling cap on SD VCC close to the card");
    Serial.println("============================================================");
    return;
  }

  Serial.printf("[SD] Card initialized at %u MHz.\n\n", goodSpeed);

  // --- Step 2: card info ---
  uint8_t cardType = sd.card()->type();
  const char* typeName =
      (cardType == SD_CARD_TYPE_SD1)  ? "SD1"  :
      (cardType == SD_CARD_TYPE_SD2)  ? "SD2"  :
      (cardType == SD_CARD_TYPE_SDHC) ? "SDHC/SDXC" : "UNKNOWN";
  Serial.printf("[SD] Card type: %s\n", typeName);

  uint64_t cardSizeKB = sd.card()->sectorCount() / 2;
  Serial.printf("[SD] Card size: %llu MB\n\n", cardSizeKB / 1024);

  // --- Step 3: write test ---
  const char* testFile = "/sdtest.txt";
  const char* testData = "SDTest write/read OK\n";
  Serial.printf("[SD] Writing test file %s ... ", testFile);
  SdFile f;
  if (!f.open(testFile, O_RDWR | O_CREAT | O_TRUNC)) {
    Serial.println("FAIL (open for write failed)");
    Serial.printf("    SdFat error code: %d\n", sd.sdErrorCode());
    return;
  }
  f.print(testData);
  f.close();
  Serial.println("OK ✓");

  // --- Step 4: read-back test ---
  Serial.printf("[SD] Reading back  %s ... ", testFile);
  char buf[64] = { 0 };
  if (!f.open(testFile, O_RDONLY)) {
    Serial.println("FAIL (open for read failed)");
    return;
  }
  int n = f.read(buf, sizeof(buf) - 1);
  f.close();
  if (n < 0) {
    Serial.println("FAIL (read error)");
    return;
  }
  buf[n] = '\0';
  bool match = (strcmp(buf, testData) == 0);
  Serial.println(match ? "OK ✓" : "FAIL (data mismatch)");
  if (!match) {
    Serial.printf("    Expected: [%s]\n", testData);
    Serial.printf("    Got:      [%s]\n", buf);
  }

  // --- Done ---
  Serial.println();
  if (match) {
    Serial.println("==============================================");
    Serial.println("All SD tests PASSED.  Hardware is working OK.");
    Serial.println("==============================================");
  } else {
    Serial.println("==============================================");
    Serial.println("SD tests FAILED.  See details above.");
    Serial.println("==============================================");
  }
}

void loop() {
  // Nothing to do – all output is in setup().
}
