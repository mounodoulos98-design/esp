# Οδηγός Προσαρμογής Python σε ESP32/Arduino
# Python to ESP32/Arduino Adaptation Guide

Αυτός ο οδηγός εξηγεί πώς να προσαρμόσετε κώδικα Python για χρήση σε ESP32/ESP8266 με Arduino.

This guide explains how to adapt Python code for use on ESP32/ESP8266 with Arduino.

## Βασικές Διαφορές / Key Differences

### 1. Γλώσσα Προγραμματισμού / Programming Language

| Python | Arduino C++ |
|--------|-------------|
| Interpreted language | Compiled language |
| Dynamic typing | Static typing |
| Automatic memory management | Manual memory management |
| `import` για βιβλιοθήκες | `#include` for libraries |

### 2. Δομή Προγράμματος / Program Structure

**Python:**
```python
def main():
    # Initialization
    while True:
        # Main loop
        pass

if __name__ == "__main__":
    main()
```

**Arduino:**
```cpp
void setup() {
    // Initialization (runs once)
}

void loop() {
    // Main loop (runs forever)
}
```

### 3. Βασική Σύνταξη / Basic Syntax

| Feature | Python | Arduino C++ |
|---------|--------|-------------|
| Variables | `x = 10` | `int x = 10;` |
| Print | `print("Hello")` | `Serial.println("Hello");` |
| Comments | `# comment` | `// comment` |
| Delay | `time.sleep(1)` | `delay(1000);` |
| Boolean | `True`, `False` | `true`, `false` |

### 4. Τύποι Δεδομένων / Data Types

| Python | Arduino C++ | Description |
|--------|-------------|-------------|
| `int` | `int` | Integer (32-bit in Python, 16-bit in Arduino) |
| `float` | `float` | Floating point |
| `bool` | `bool` | Boolean |
| `str` | `String` or `char[]` | Text |
| `list` | `array[]` or `std::vector` | Collections |

### 5. Κλάσεις / Classes

**Python:**
```python
class LED:
    def __init__(self, pin):
        self.pin = pin
    
    def on(self):
        print(f"LED {self.pin} ON")
```

**Arduino:**
```cpp
class LED {
  private:
    int pin;
    
  public:
    LED(int p) {
      pin = p;
    }
    
    void on() {
      Serial.print("LED ");
      Serial.print(pin);
      Serial.println(" ON");
    }
};
```

### 6. GPIO Operations

**Python (using RPi.GPIO or similar):**
```python
import RPi.GPIO as GPIO

GPIO.setmode(GPIO.BCM)
GPIO.setup(2, GPIO.OUT)
GPIO.output(2, GPIO.HIGH)
```

**Arduino:**
```cpp
pinMode(2, OUTPUT);
digitalWrite(2, HIGH);
```

### 7. Analog Reading

**Python (simulated or using ADC library):**
```python
value = adc.read(channel)
```

**Arduino:**
```cpp
int value = analogRead(pin);
// ESP32: 0-4095 (12-bit)
// ESP8266: 0-1023 (10-bit)
```

## Βήματα Προσαρμογής / Adaptation Steps

### Βήμα 1: Ανάλυση Python Κώδικα / Step 1: Analyze Python Code

1. Εντοπίστε τις βιβλιοθήκες που χρησιμοποιούνται
   - Identify libraries used
2. Καταγράψτε τις λειτουργίες GPIO
   - Document GPIO operations
3. Σημειώστε τις καθυστερήσεις και τους χρόνους
   - Note delays and timing

### Βήμα 2: Επιλογή Hardware / Step 2: Hardware Selection

- **ESP32**: Περισσότερη μνήμη, WiFi, Bluetooth
  - More memory, WiFi, Bluetooth
- **ESP8266**: Χαμηλότερο κόστος, WiFi μόνο
  - Lower cost, WiFi only

### Βήμα 3: Μετατροπή Κώδικα / Step 3: Code Conversion

1. Αντικαταστήστε `import` με `#include`
   - Replace `import` with `#include`
2. Μετατρέψτε `main()` σε `setup()` και `loop()`
   - Convert `main()` to `setup()` and `loop()`
3. Προσθέστε τύπους μεταβλητών
   - Add variable types
4. Αντικαταστήστε `print()` με `Serial.println()`
   - Replace `print()` with `Serial.println()`
5. Μετατρέψτε `time.sleep()` σε `delay()`
   - Convert `time.sleep()` to `delay()`

### Βήμα 4: Διαμόρφωση Pins / Step 4: Pin Configuration

**ESP32 Common Pins:**
- GPIO 2: Built-in LED
- GPIO 34-39: Input only (ADC)
- GPIO 0-33: Input/Output

**ESP8266 Common Pins:**
- GPIO 2 (D4): Built-in LED
- A0: Analog input
- D0-D8: Digital pins

### Βήμα 5: Δοκιμή / Step 5: Testing

1. Μεταγλώττιση στο Arduino IDE
   - Compile in Arduino IDE
2. Ανέβασμα στο ESP
   - Upload to ESP
3. Παρακολούθηση Serial Monitor
   - Monitor Serial output
4. Αποσφαλμάτωση
   - Debug issues

## Κοινά Προβλήματα / Common Issues

### 1. Μνήμη / Memory
- ESP32: ~520 KB RAM
- ESP8266: ~80 KB RAM
- Προσοχή σε μεγάλα arrays και strings
  - Watch out for large arrays and strings

### 2. Χρόνοι / Timing
- `delay()` μπλοκάρει το πρόγραμμα
  - `delay()` blocks program
- Χρησιμοποιήστε `millis()` για non-blocking timing
  - Use `millis()` for non-blocking timing

### 3. Float Precision
- Το Arduino έχει περιορισμένη ακρίβεια float
  - Arduino has limited float precision
- Χρησιμοποιήστε integers όπου είναι δυνατόν
  - Use integers where possible

## Παραδείγματα / Examples

Δείτε τα αρχεία:
- `example_python.py` - Python implementation
- `example_esp32.ino` - Arduino/ESP32 implementation

## Πόροι / Resources

### Arduino IDE Setup για ESP32/ESP8266

1. Προσθήκη Board Manager URLs:
   - ESP32: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - ESP8266: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`

2. Tools → Board → Boards Manager
3. Αναζήτηση "ESP32" ή "ESP8266"
4. Εγκατάσταση

### Χρήσιμοι Σύνδεσμοι / Useful Links

- [ESP32 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [ESP8266 Documentation](https://arduino-esp8266.readthedocs.io/)
- [Arduino Reference](https://www.arduino.cc/reference/en/)

## Συμβουλές / Tips

1. Ξεκινήστε με απλό κώδικα
   - Start with simple code
2. Δοκιμάστε κάθε λειτουργία ξεχωριστά
   - Test each function separately
3. Χρησιμοποιήστε Serial.println() για debugging
   - Use Serial.println() for debugging
4. Προσέξτε τα voltage levels (3.3V για ESP)
   - Watch voltage levels (3.3V for ESP)
5. Διαβάστε το datasheet του hardware σας
   - Read your hardware datasheet
