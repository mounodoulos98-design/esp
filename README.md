# ESP32/ESP8266 Arduino - Python Adaptation Examples

Παραδείγματα προσαρμογής κώδικα Python για ESP32/ESP8266 με Arduino.

Examples of adapting Python code for ESP32/ESP8266 with Arduino.

## 📁 Περιεχόμενα / Contents

- `example_python.py` - Python implementation with LED control and sensor reading
- `example_esp32.ino` - Equivalent Arduino/C++ code for ESP32/ESP8266
- `ADAPTATION_GUIDE.md` - Complete guide for adapting Python to Arduino/ESP32

## 🚀 Γρήγορη Έναρξη / Quick Start

### Python Example

Για να τρέξετε το Python παράδειγμα:

```bash
python3 example_python.py
```

### ESP32/ESP8266 Example

1. Ανοίξτε το Arduino IDE
2. Φορτώστε το `example_esp32.ino`
3. Επιλέξτε το board σας (ESP32 ή ESP8266)
4. Επιλέξτε το σωστό COM port
5. Ανεβάστε τον κώδικα
6. Ανοίξτε το Serial Monitor (115200 baud)

## 📚 Οδηγός Προσαρμογής / Adaptation Guide

Διαβάστε το [ADAPTATION_GUIDE.md](ADAPTATION_GUIDE.md) για λεπτομερή οδηγό προσαρμογής κώδικα Python σε Arduino/ESP32.

Read [ADAPTATION_GUIDE.md](ADAPTATION_GUIDE.md) for a detailed guide on adapting Python code to Arduino/ESP32.

## 🔧 Hardware Requirements

### For ESP32:
- ESP32 Development Board
- LED (optional, built-in LED on GPIO 2)
- Temperature sensor (optional, for analog reading example)
- USB cable for programming

### For ESP8266:
- ESP8266 Development Board (e.g., NodeMCU)
- LED (optional, built-in LED on GPIO 2/D4)
- Temperature sensor (optional, for analog reading example)
- USB cable for programming

## 💡 Features Demonstrated

- LED control (blinking)
- Temperature sensor reading simulation
- Class-based structure
- Serial communication
- Main loop implementation

## 🛠️ Setup Arduino IDE for ESP

### ESP32:
1. File → Preferences
2. Additional Board Manager URLs: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → Boards Manager → Search "ESP32" → Install

### ESP8266:
1. File → Preferences
2. Additional Board Manager URLs: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. Tools → Board → Boards Manager → Search "ESP8266" → Install

## 📖 Key Differences: Python vs Arduino

| Feature | Python | Arduino C++ |
|---------|--------|-------------|
| Execution | Interpreted | Compiled |
| Typing | Dynamic | Static |
| GPIO | Library-based | Built-in functions |
| Delay | `time.sleep(1)` | `delay(1000)` |
| Print | `print()` | `Serial.println()` |
| Main Loop | `while True:` | `void loop()` |

## 🤝 Contributing

Feel free to submit issues or pull requests to improve these examples.

## 📝 License

This project is open source and available for educational purposes.