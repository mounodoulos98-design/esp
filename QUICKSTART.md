# Γρήγορη Έναρξη / Quick Start Guide

## Για Αρχάριους / For Beginners

Αυτός ο οδηγός σας βοηθά να ξεκινήσετε γρήγορα με την προσαρμογή κώδικα Python σε ESP32/ESP8266.

This guide helps you get started quickly with adapting Python code to ESP32/ESP8266.

---

## Βήμα 1: Εγκατάσταση Arduino IDE / Step 1: Install Arduino IDE

### Windows / macOS / Linux:

1. Κατεβάστε το Arduino IDE από: https://www.arduino.cc/en/software
   - Download Arduino IDE from: https://www.arduino.cc/en/software

2. Εγκαταστήστε το στο σύστημά σας
   - Install it on your system

---

## Βήμα 2: Προσθήκη ESP32/ESP8266 Support / Step 2: Add ESP32/ESP8266 Support

### Για ESP32 / For ESP32:

1. Ανοίξτε Arduino IDE → File → Preferences
   - Open Arduino IDE → File → Preferences

2. Στο πεδίο "Additional Board Manager URLs" προσθέστε:
   - In "Additional Board Manager URLs" field, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```

3. Πατήστε OK

4. Tools → Board → Boards Manager
5. Αναζητήστε "ESP32" / Search for "ESP32"
6. Εγκαταστήστε "ESP32 by Espressif Systems"
   - Install "ESP32 by Espressif Systems"

### Για ESP8266 / For ESP8266:

1. Ανοίξτε Arduino IDE → File → Preferences
   - Open Arduino IDE → File → Preferences

2. Στο πεδίο "Additional Board Manager URLs" προσθέστε:
   - In "Additional Board Manager URLs" field, add:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```

3. Πατήστε OK

4. Tools → Board → Boards Manager
5. Αναζητήστε "ESP8266" / Search for "ESP8266"
6. Εγκαταστήστε "ESP8266 by ESP8266 Community"
   - Install "ESP8266 by ESP8266 Community"

---

## Βήμα 3: Σύνδεση του ESP Board / Step 3: Connect Your ESP Board

1. Συνδέστε το ESP32/ESP8266 στον υπολογιστή με καλώδιο USB
   - Connect ESP32/ESP8266 to computer with USB cable

2. Εγκαταστήστε drivers αν χρειάζεται (συνήθως αυτόματα)
   - Install drivers if needed (usually automatic)
   
3. Στο Arduino IDE:
   - Tools → Board → [Επιλέξτε το board σας / Select your board]
   - Tools → Port → [Επιλέξτε το COM port / Select COM port]

---

## Βήμα 4: Δοκιμή με Blink / Step 4: Test with Blink

### Απλό Test:

```cpp
void setup() {
  pinMode(2, OUTPUT);  // GPIO 2 = Built-in LED
}

void loop() {
  digitalWrite(2, HIGH);  // LED ON
  delay(1000);           // Wait 1 second
  digitalWrite(2, LOW);   // LED OFF
  delay(1000);           // Wait 1 second
}
```

1. Αντιγράψτε αυτόν τον κώδικα στο Arduino IDE
   - Copy this code into Arduino IDE

2. Πατήστε το κουμπί Upload (→) / Click Upload button (→)

3. Περιμένετε να ολοκληρωθεί το upload
   - Wait for upload to complete

4. Το built-in LED θα πρέπει να αναβοσβήνει!
   - The built-in LED should blink!

---

## Βήμα 5: Δοκιμή του Example Code / Step 5: Test Example Code

### Python Example:

```bash
python3 example_python.py
```

Θα δείτε:
- LED messages (simulated)
- Temperature readings (simulated)

You will see:
- LED messages (simulated)
- Temperature readings (simulated)

### Arduino Example:

1. Ανοίξτε το `example_esp32.ino` στο Arduino IDE
   - Open `example_esp32.ino` in Arduino IDE

2. Επιλέξτε το board και port σας
   - Select your board and port

3. Upload τον κώδικα / Upload the code

4. Ανοίξτε Serial Monitor (Ctrl+Shift+M ή Tools → Serial Monitor)
   - Open Serial Monitor (Ctrl+Shift+M or Tools → Serial Monitor)

5. Ορίστε baud rate σε 115200
   - Set baud rate to 115200

6. Θα δείτε:
   - LED toggle messages
   - Temperature readings

---

## Κοινά Προβλήματα / Common Problems

### 1. "Port not found" / "Δεν βρέθηκε port"

**Λύση / Solution:**
- Βεβαιωθείτε ότι το USB καλώδιο είναι συνδεδεμένο
  - Make sure USB cable is connected
- Δοκιμάστε διαφορετικό USB καλώδιο (όχι μόνο για φόρτιση)
  - Try different USB cable (not charge-only)
- Εγκαταστήστε CH340 ή CP2102 drivers
  - Install CH340 or CP2102 drivers

### 2. "Upload failed" / "Αποτυχία upload"

**Λύση / Solution:**
- Κρατήστε πατημένο το κουμπί BOOT κατά το upload
  - Hold BOOT button during upload
- Πατήστε RESET μετά το upload
  - Press RESET after upload
- Κλείστε Serial Monitor πριν το upload
  - Close Serial Monitor before upload

### 3. "Compilation error" / "Σφάλμα μεταγλώττισης"

**Λύση / Solution:**
- Βεβαιωθείτε ότι έχετε επιλέξει το σωστό board
  - Make sure you selected the correct board
- Ελέγξτε για syntax errors (ελλείποντα ; κλπ)
  - Check for syntax errors (missing ; etc)
- Επανεκκινήστε το Arduino IDE
  - Restart Arduino IDE

### 4. "Nothing in Serial Monitor" / "Τίποτα στο Serial Monitor"

**Λύση / Solution:**
- Ελέγξτε το baud rate (115200)
  - Check baud rate (115200)
- Πατήστε RESET button
  - Press RESET button
- Κλείστε και ανοίξτε ξανά το Serial Monitor
  - Close and reopen Serial Monitor

---

## Επόμενα Βήματα / Next Steps

### 1. Μελετήστε τα Examples / Study the Examples
- `example_python.py` - Python version
- `example_esp32.ino` - Arduino version
- Συγκρίνετε τις διαφορές / Compare the differences

### 2. Διαβάστε τους Οδηγούς / Read the Guides
- `ADAPTATION_GUIDE.md` - Πλήρης οδηγός προσαρμογής / Complete adaptation guide
- `COMPARISON.md` - Σύγκριση Python vs Arduino / Python vs Arduino comparison
- `HARDWARE_SETUP.md` - Οδηγός hardware / Hardware guide

### 3. Δοκιμάστε Δικό σας Κώδικα / Try Your Own Code
- Ξεκινήστε με απλό Python κώδικα
  - Start with simple Python code
- Ακολουθήστε τα βήματα προσαρμογής
  - Follow adaptation steps
- Δοκιμάστε στο ESP
  - Test on ESP

### 4. Προσθέστε Sensors / Add Sensors
- DHT11/DHT22 (Temperature & Humidity)
- Ultrasonic sensor (HC-SR04)
- OLED display
- PIR motion sensor

---

## Χρήσιμες Εντολές / Useful Commands

### Serial Monitor Shortcuts:
- Άνοιγμα: Ctrl+Shift+M (Windows/Linux) ή Cmd+Shift+M (Mac)
  - Open: Ctrl+Shift+M (Windows/Linux) or Cmd+Shift+M (Mac)
- Upload: Ctrl+U (Windows/Linux) ή Cmd+U (Mac)
  - Upload: Ctrl+U (Windows/Linux) or Cmd+U (Mac)
- Verify: Ctrl+R (Windows/Linux) ή Cmd+R (Mac)
  - Verify: Ctrl+R (Windows/Linux) or Cmd+R (Mac)

### Debugging Tips:
```cpp
// Print variables
Serial.print("Value: ");
Serial.println(value);

// Print with formatting
Serial.print("Temperature: ");
Serial.print(temp, 2);  // 2 decimal places
Serial.println(" °C");

// Check if Serial is ready
if (Serial) {
  Serial.println("Serial is ready!");
}
```

---

## Πρόσθετοι Πόροι / Additional Resources

### Online Tools:
- [Wokwi Simulator](https://wokwi.com/) - Online ESP32 simulator
- [Arduino Reference](https://www.arduino.cc/reference/en/) - Function reference
- [ESP32 Documentation](https://docs.espressif.com/) - Official docs

### Learning Resources:
- YouTube tutorials για ESP32/ESP8266
- Arduino forums για βοήθεια
  - Arduino forums for help
- GitHub examples

### Community:
- [r/esp32](https://reddit.com/r/esp32) - Reddit community
- [r/esp8266](https://reddit.com/r/esp8266) - Reddit community
- Arduino Forums
- Stack Overflow

---

## Συμβουλές / Tips

✅ **DO:**
- Ξεκινήστε με απλά examples / Start with simple examples
- Δοκιμάστε κάθε function ξεχωριστά / Test each function separately
- Χρησιμοποιήστε Serial.println() για debugging
- Διαβάστε error messages προσεκτικά / Read error messages carefully
- Αποθηκεύετε τον κώδικά σας συχνά / Save your code often

❌ **DON'T:**
- Μην συνδέετε 5V σε GPIO pins! / Don't connect 5V to GPIO pins!
- Μην αποσυνδέετε κατά το upload / Don't disconnect during upload
- Μην ξεχνάτε τα semicolons (;) / Don't forget semicolons (;)
- Μην χρησιμοποιείτε delay() παντού / Don't use delay() everywhere
- Μην ξεχνάτε να ορίσετε pinMode() / Don't forget to set pinMode()

---

## Έτοιμοι να ξεκινήσετε! / Ready to Start!

Τώρα είστε έτοιμοι να προσαρμόσετε Python κώδικα στο ESP32/ESP8266!

Now you're ready to adapt Python code to ESP32/ESP8266!

Καλή επιτυχία! / Good luck! 🚀