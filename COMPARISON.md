# Σύγκριση Κώδικα Python vs Arduino/ESP32
# Code Comparison: Python vs Arduino/ESP32

## Side-by-Side Comparison

### 1. Imports & Includes

**Python:**
```python
import time
```

**Arduino:**
```cpp
// No imports needed for basic functionality
// Arduino libraries are automatically included
```

### 2. Class Definition

**Python:**
```python
class LED:
    def __init__(self, pin):
        self.pin = pin
        self.state = False
        print(f"LED initialized on pin {self.pin}")
```

**Arduino:**
```cpp
class LED {
  private:
    int pin;
    bool state;
    
  public:
    LED(int p) {
      pin = p;
      state = false;
      pinMode(pin, OUTPUT);
      Serial.print("LED initialized on pin ");
      Serial.println(pin);
    }
};
```

**Διαφορές / Differences:**
- Python: `__init__` method with `self` parameter
- Arduino: Constructor with same name as class
- Arduino: Must declare variable types (`int`, `bool`)
- Arduino: Must specify `private`/`public` access modifiers
- Arduino: Must call `pinMode()` to configure GPIO

### 3. Methods

**Python:**
```python
def on(self):
    self.state = True
    print(f"LED on pin {self.pin} is ON")
```

**Arduino:**
```cpp
void on() {
    state = true;
    digitalWrite(pin, HIGH);
    Serial.print("LED on pin ");
    Serial.print(pin);
    Serial.println(" is ON");
}
```

**Διαφορές / Differences:**
- Python: `def` keyword, `self` parameter
- Arduino: Return type (`void`), no `self`
- Python: f-strings for formatting
- Arduino: Multiple `Serial.print()` calls
- Arduino: Actually controls hardware with `digitalWrite()`

### 4. Main Program Structure

**Python:**
```python
def main():
    led = LED(13)
    sensor = TemperatureSensor("A0")
    
    try:
        while True:
            led.toggle()
            temp = sensor.read()
            print(f"Temperature: {temp}°C")
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nProgram stopped")
        led.off()
```

**Arduino:**
```cpp
LED led(LED_PIN);
TemperatureSensor sensor(SENSOR_PIN);

void setup() {
    Serial.begin(115200);
    Serial.println("Starting...");
}

void loop() {
    led.toggle();
    float temp = sensor.read();
    Serial.print("Temperature: ");
    Serial.print(temp, 2);
    Serial.println("°C");
    delay(1000);
}
```

**Διαφορές / Differences:**
- Python: One main function with while loop
- Arduino: Separate `setup()` and `loop()` functions
- Python: Exception handling for Ctrl+C
- Arduino: `loop()` runs forever automatically
- Arduino: Must initialize Serial communication
- Arduino: Objects created globally

### 5. Timing/Delays

**Python:**
```python
time.sleep(1)  # Sleep for 1 second
```

**Arduino:**
```cpp
delay(1000);  // Delay for 1000 milliseconds
```

**Διαφορές / Differences:**
- Python: Time in seconds (float)
- Arduino: Time in milliseconds (integer)
- Both block execution

### 6. Printing Output

**Python:**
```python
print(f"Temperature: {temp}°C")
print(f"LED {self.pin} is ON")
```

**Arduino:**
```cpp
Serial.print("Temperature: ");
Serial.print(temp, 2);  // 2 decimal places
Serial.println("°C");

Serial.print("LED ");
Serial.print(pin);
Serial.println(" is ON");
```

**Διαφορές / Differences:**
- Python: Single `print()` with f-strings
- Arduino: Multiple print statements
- Arduino: `println()` adds newline, `print()` doesn't
- Arduino: Can specify decimal places for floats

### 7. Variables & Types

**Python:**
```python
pin = 13              # Dynamic typing
state = False         # Boolean
temperature = 25.0    # Float
name = "LED"          # String
```

**Arduino:**
```cpp
int pin = 13;           // Integer (16-bit)
bool state = false;     // Boolean
float temperature = 25.0; // Float (32-bit)
String name = "LED";    // String object
char* name2 = "LED";    // C-style string
```

**Διαφορές / Differences:**
- Python: No type declaration needed
- Arduino: Must declare type before use
- Arduino: Statements end with semicolon
- Arduino: Different string types available

### 8. Conditionals

**Python:**
```python
if self.state:
    self.off()
else:
    self.on()
```

**Arduino:**
```cpp
if (state) {
    off();
} else {
    on();
}
```

**Διαφορές / Differences:**
- Python: Colon and indentation
- Arduino: Parentheses and curly braces
- Python: `True`/`False` (capital T/F)
- Arduino: `true`/`false` (lowercase)

### 9. Analog Reading

**Python (simulation):**
```python
def read(self):
    import random
    value = random.randint(0, 1023)
    return value
```

**Arduino (actual hardware):**
```cpp
float read() {
    int analogValue = analogRead(pin);  // 0-4095 on ESP32
    float temperature = 25.0 + (analogValue / 100.0);
    return temperature;
}
```

**Διαφορές / Differences:**
- Python: Simulated values
- Arduino: Real hardware reading
- ESP32: 12-bit ADC (0-4095)
- ESP8266: 10-bit ADC (0-1023)

## Complete Feature Mapping

| Feature | Python | Arduino/C++ |
|---------|--------|-------------|
| Comments | `# comment` | `// comment` or `/* comment */` |
| Multi-line string | `'''text'''` | Not available (use multiple strings) |
| Variable declaration | `x = 10` | `int x = 10;` |
| Constants | `X = 10` (convention) | `const int X = 10;` or `#define X 10` |
| Function definition | `def func():` | `void func() {}` |
| Function with return | `def func(): return x` | `int func() { return x; }` |
| Boolean values | `True`, `False` | `true`, `false` |
| Logical operators | `and`, `or`, `not` | `&&`, `||`, `!` |
| None/Null | `None` | `NULL` or `nullptr` |
| String concatenation | `str1 + str2` | `String(str1) + String(str2)` |
| Array/List | `list = [1,2,3]` | `int list[] = {1,2,3};` |
| Dictionary | `dict = {'key': 'value'}` | Not available (use struct or library) |
| For loop | `for i in range(10):` | `for(int i=0; i<10; i++) {}` |
| While loop | `while condition:` | `while(condition) {}` |
| GPIO Output | `GPIO.output(pin, HIGH)` | `digitalWrite(pin, HIGH);` |
| GPIO Input | `GPIO.input(pin)` | `digitalRead(pin);` |
| Analog Read | Library-dependent | `analogRead(pin);` |
| PWM | Library-dependent | `analogWrite(pin, value);` |
| Delay | `time.sleep(1)` | `delay(1000);` |
| Print | `print("text")` | `Serial.println("text");` |
| Random number | `random.random()` | `random()` or `random(min, max)` |

## Memory Considerations

**Python:**
- Automatic garbage collection
- Dynamic memory allocation
- No manual memory management
- Typically runs on systems with GB of RAM

**Arduino/ESP:**
- Manual memory management
- Limited RAM (80KB - 520KB)
- Stack overflow risks
- Must be careful with large arrays/strings
- Use `const` for constants stored in flash
- Use `PROGMEM` to store data in flash memory

## Best Practices for Adaptation

1. **Start Simple**: Port basic functionality first
2. **Test Incrementally**: Test each function separately
3. **Use Serial Debug**: Replace `print()` with `Serial.println()`
4. **Watch Memory**: Monitor RAM usage
5. **Consider Timing**: Account for different execution speeds
6. **Pin Compatibility**: Check ESP32/ESP8266 pin capabilities
7. **Voltage Levels**: ESP operates at 3.3V (not 5V!)

## Example Workflow

1. Write and test Python code
2. Identify core functionality
3. Create Arduino sketch structure (`setup()` and `loop()`)
4. Convert classes and functions
5. Replace Python libraries with Arduino equivalents
6. Adjust timing and delays
7. Test on actual hardware
8. Debug using Serial output
9. Optimize for memory and performance