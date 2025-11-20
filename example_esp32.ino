/*
 * ESP32/ESP8266 Arduino Example
 * Adapted from Python code for IoT applications
 * 
 * This example demonstrates:
 * - LED control (blinking)
 * - Temperature sensor reading simulation
 * 
 * Hardware connections:
 * - LED: GPIO 2 (built-in LED on most ESP32 boards)
 * - Temperature sensor: GPIO 34 (ADC1_CH6) for analog reading
 * 
 * For ESP8266:
 * - LED: GPIO 2 (D4 on NodeMCU)
 * - Sensor: A0 (analog input)
 */

// Pin definitions
#define LED_PIN 2        // Built-in LED on most ESP32 boards (GPIO2)
#define SENSOR_PIN 34    // Analog pin for temperature sensor (ADC1_CH6)

// Global variables
bool ledState = false;
float temperature = 25.0;

// LED class equivalent
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
    
    void on() {
      state = true;
      digitalWrite(pin, HIGH);
      Serial.print("LED on pin ");
      Serial.print(pin);
      Serial.println(" is ON");
    }
    
    void off() {
      state = false;
      digitalWrite(pin, LOW);
      Serial.print("LED on pin ");
      Serial.print(pin);
      Serial.println(" is OFF");
    }
    
    void toggle() {
      if (state) {
        off();
      } else {
        on();
      }
    }
};

// Temperature sensor class equivalent
class TemperatureSensor {
  private:
    int pin;
    float temperature;
    
  public:
    TemperatureSensor(int p) {
      pin = p;
      temperature = 25.0;
      pinMode(pin, INPUT);
      Serial.print("Temperature sensor initialized on pin ");
      Serial.println(pin);
    }
    
    float read() {
      // Read analog value (0-4095 on ESP32, 0-1023 on ESP8266)
      int analogValue = analogRead(pin);
      
      // Convert to temperature (example conversion)
      // For real sensor, use appropriate formula
      temperature = 25.0 + (analogValue / 100.0);
      
      // Add some variation for simulation
      temperature += (random(-50, 50) / 100.0);
      
      return temperature;
    }
};

// Create objects
LED led(LED_PIN);
TemperatureSensor sensor(SENSOR_PIN);

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\nESP32/ESP8266 Example Starting...");
  Serial.println("Adapted from Python code");
  Serial.println("\nStarting main loop...");
  Serial.println("Press reset button to restart\n");
  
  // Initialize random seed
  randomSeed(analogRead(0));
}

void loop() {
  // Toggle LED
  led.toggle();
  
  // Read temperature
  float temp = sensor.read();
  Serial.print("Temperature: ");
  Serial.print(temp, 2);
  Serial.println("°C");
  
  // Wait 1 second
  delay(1000);
}
