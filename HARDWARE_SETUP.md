# Οδηγός Σύνδεσης Hardware / Hardware Setup Guide

## ESP32 Development Board

### Pinout Overview

```
                                ESP32 DevKit v1
                    
                         +-----------------+
                         |                 |
                    3.3V |[ ]           [ ]| GND
                      EN |[ ]           [ ]| GPIO23
                  GPIO36 |[ ]           [ ]| GPIO22
                  GPIO39 |[ ]           [ ]| GPIO1 (TX)
                  GPIO34 |[ ]           [ ]| GPIO3 (RX)
                  GPIO35 |[ ]           [ ]| GPIO21
                  GPIO32 |[ ]           [ ]| GND
                  GPIO33 |[ ]           [ ]| GPIO19
                  GPIO25 |[ ]           [ ]| GPIO18
                  GPIO26 |[ ]           [ ]| GPIO5
                  GPIO27 |[ ]           [ ]| GPIO17
                  GPIO14 |[ ]           [ ]| GPIO16
                  GPIO12 |[ ]           [ ]| GPIO4
                     GND |[ ]           [ ]| GPIO0
                  GPIO13 |[ ]           [ ]| GPIO2 (LED)
                   GPIO9 |[ ]           [ ]| GPIO15
                  GPIO10 |[ ]           [ ]| GPIO8
                  GPIO11 |[ ]           [ ]| GPIO7
                     VIN |[ ]           [ ]| GPIO6
                         |                 |
                         +-----------------+
                                USB
```

### Pin Categories

#### Output Pins (Digital):
- GPIO 0-33 (except input-only pins)
- Built-in LED: GPIO 2

#### Input-Only Pins (ADC):
- GPIO 34, 35, 36, 39
- These cannot be used as outputs

#### ADC Pins (Analog Input):
- ADC1: GPIO 32-39
- ADC2: GPIO 0, 2, 4, 12-15, 25-27
- Note: ADC2 cannot be used when WiFi is active

#### PWM Pins:
- Any output pin can be used for PWM (using ledc library)

#### Special Purpose Pins:
- GPIO 0: Boot mode (must be HIGH during boot)
- GPIO 2: Built-in LED, Boot mode
- GPIO 12: Boot voltage selection (must be LOW during boot)
- GPIO 15: Boot mode (must be HIGH during boot)

## ESP8266 NodeMCU

### Pinout Overview

```
                          NodeMCU v1.0 (ESP8266)
                    
                         +-----------------+
                         |                 |
                     A0  |[ ]           [ ]| D0  (GPIO16)
                Reserved |[ ]           [ ]| D1  (GPIO5)
                Reserved |[ ]           [ ]| D2  (GPIO4)
                   SD3   |[ ]           [ ]| D3  (GPIO0)
                   SD2   |[ ]           [ ]| D4  (GPIO2) LED
                   SD1   |[ ]           [ ]| 3.3V
                   CMD   |[ ]           [ ]| GND
                   SD0   |[ ]           [ ]| D5  (GPIO14)
                   CLK   |[ ]           [ ]| D6  (GPIO12)
                     GND |[ ]           [ ]| D7  (GPIO13)
                    3.3V |[ ]           [ ]| D8  (GPIO15)
                      EN |[ ]           [ ]| D9  (GPIO3) RX
                     RST |[ ]           [ ]| D10 (GPIO1) TX
                     GND |[ ]           [ ]| GND
                     VIN |[ ]           [ ]| 3.3V
                         |                 |
                         +-----------------+
                              micro-USB
```

### Pin Mapping

| NodeMCU Label | GPIO | Function |
|---------------|------|----------|
| D0 | GPIO16 | Digital I/O |
| D1 | GPIO5 | Digital I/O, SCL |
| D2 | GPIO4 | Digital I/O, SDA |
| D3 | GPIO0 | Digital I/O, FLASH |
| D4 | GPIO2 | Digital I/O, LED, TX1 |
| D5 | GPIO14 | Digital I/O, SCK |
| D6 | GPIO12 | Digital I/O, MISO |
| D7 | GPIO13 | Digital I/O, MOSI |
| D8 | GPIO15 | Digital I/O, CS |
| D9 | GPIO3 | RX |
| D10 | GPIO1 | TX |
| A0 | ADC0 | Analog Input (0-1V) |

## Example Connections

### Basic LED Blink

**Parts Needed:**
- ESP32 or ESP8266 board
- LED (optional - use built-in)
- 220Ω resistor (if using external LED)
- Breadboard and wires

**Wiring (External LED):**

ESP32:
```
GPIO2 ----[LED]----[220Ω]---- GND
         (Anode)  (Cathode)
```

ESP8266:
```
D4 (GPIO2) ----[LED]----[220Ω]---- GND
              (Anode)  (Cathode)
```

**Note:** Built-in LED is already connected to GPIO2 (ESP32) or D4 (ESP8266).

### Temperature Sensor (Analog)

**Parts Needed:**
- ESP32 or ESP8266 board
- TMP36 or LM35 temperature sensor
- Breadboard and wires

**TMP36 Wiring:**

ESP32:
```
TMP36 Pin 1 (Vcc)    ---- 3.3V
TMP36 Pin 2 (Vout)   ---- GPIO34 (ADC)
TMP36 Pin 3 (GND)    ---- GND
```

ESP8266:
```
TMP36 Pin 1 (Vcc)    ---- 3.3V
TMP36 Pin 2 (Vout)   ---- A0
TMP36 Pin 3 (GND)    ---- GND
```

**Conversion Formula:**
```cpp
// For TMP36
float voltage = (analogValue * 3.3) / 4095.0;  // ESP32
// float voltage = (analogValue * 3.3) / 1023.0;  // ESP8266
float temperatureC = (voltage - 0.5) * 100.0;
```

### DHT11/DHT22 Temperature & Humidity Sensor

**Parts Needed:**
- ESP32 or ESP8266 board
- DHT11 or DHT22 sensor
- 10kΩ pull-up resistor (some modules have built-in)
- DHT library (install via Arduino Library Manager)

**Wiring:**

ESP32:
```
DHT Pin 1 (VCC)  ---- 3.3V
DHT Pin 2 (Data) ---- GPIO4 ----[10kΩ]---- 3.3V
DHT Pin 3 (NC)   ---- (not connected)
DHT Pin 4 (GND)  ---- GND
```

ESP8266:
```
DHT Pin 1 (VCC)  ---- 3.3V
DHT Pin 2 (Data) ---- D2 (GPIO4) ----[10kΩ]---- 3.3V
DHT Pin 3 (NC)   ---- (not connected)
DHT Pin 4 (GND)  ---- GND
```

**Arduino Code:**
```cpp
#include <DHT.h>

#define DHTPIN 4      // GPIO4
#define DHTTYPE DHT22 // or DHT11

DHT dht(DHTPIN, DHTTYPE);

void setup() {
    Serial.begin(115200);
    dht.begin();
}

void loop() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print("% Temperature: ");
    Serial.print(t);
    Serial.println("°C");
    
    delay(2000);
}
```

## Important Notes

### Voltage Levels
⚠️ **CRITICAL:** ESP32 and ESP8266 operate at **3.3V logic level**!
- Do NOT connect 5V signals directly to GPIO pins
- Use level shifters for 5V devices
- USB power is 5V, but internal logic is 3.3V

### Current Limitations
- Maximum current per GPIO pin: ~12mA (ESP8266), ~40mA (ESP32)
- Use transistors or relays for high-current loads
- Total current for all GPIOs: ~200mA

### Power Supply
- USB provides: 5V
- Board regulator provides: 3.3V
- Stable 3.3V power is essential for reliable operation
- External sensors should use 3.3V when possible

### Boot Mode Pins
Certain pins affect boot mode if pulled HIGH or LOW during startup:
- **ESP32:** GPIO0, GPIO2, GPIO12, GPIO15
- **ESP8266:** GPIO0, GPIO2, GPIO15

Avoid using these for inputs that might be LOW during boot.

### ADC Reference
- **ESP32:** 0-3.3V (0-4095 digital value, 12-bit)
- **ESP8266:** 0-1.0V (0-1023 digital value, 10-bit)
  - Use voltage divider for higher voltages on ESP8266

### Input Impedance
- High impedance on ADC pins (~100kΩ)
- Use capacitor (0.1µF) near sensor for stability
- Keep analog wires short to reduce noise

## Common Issues & Solutions

### Issue: Code uploads but doesn't run
- **Solution:** Check if GPIO0 is held LOW (boot mode)
- Press RESET button after upload

### Issue: Unstable analog readings
- **Solution:** 
  - Add 0.1µF capacitor between sensor output and GND
  - Take multiple readings and average them
  - Use proper grounding

### Issue: Random resets/crashes
- **Solution:**
  - Check power supply (needs stable 500mA+)
  - Avoid WiFi if using ADC2 pins (ESP32)
  - Add bulk capacitor (100µF) on power rails

### Issue: Built-in LED not working
- **Solution:**
  - Some boards have inverted LED (LOW = ON)
  - Try both `digitalWrite(LED_PIN, HIGH)` and `LOW`

## Tools & Equipment Needed

1. **Required:**
   - ESP32 or ESP8266 development board
   - USB cable (micro-USB or USB-C depending on board)
   - Computer with Arduino IDE

2. **Recommended:**
   - Breadboard
   - Jumper wires
   - Multimeter
   - LED and resistors (220Ω, 10kΩ)

3. **Optional:**
   - Logic analyzer
   - Oscilloscope
   - Power supply (3.3V regulated)

## Learning Path

1. **Start:** Built-in LED blink (no external components)
2. **Next:** External LED with resistor
3. **Then:** Button input with pull-up resistor
4. **Advanced:** Analog sensor reading
5. **Expert:** Multiple sensors + WiFi

## Resources

- [ESP32 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP8266 Datasheet](https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf)
- [Arduino-ESP32 GitHub](https://github.com/espressif/arduino-esp32)
- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)