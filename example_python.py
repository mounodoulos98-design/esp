#!/usr/bin/env python3
"""
Example Python script for LED control and sensor reading
This demonstrates basic GPIO operations that can be adapted to ESP32/Arduino
"""

import time

class LED:
    """Simulates LED control"""
    def __init__(self, pin):
        self.pin = pin
        self.state = False
        print(f"LED initialized on pin {self.pin}")
    
    def on(self):
        """Turn LED on"""
        self.state = True
        print(f"LED on pin {self.pin} is ON")
    
    def off(self):
        """Turn LED off"""
        self.state = False
        print(f"LED on pin {self.pin} is OFF")
    
    def toggle(self):
        """Toggle LED state"""
        if self.state:
            self.off()
        else:
            self.on()

class TemperatureSensor:
    """Simulates temperature sensor reading"""
    def __init__(self, pin):
        self.pin = pin
        self.temperature = 25.0
        print(f"Temperature sensor initialized on pin {self.pin}")
    
    def read(self):
        """Read temperature value"""
        # Simulate temperature variation
        import random
        self.temperature += random.uniform(-0.5, 0.5)
        return round(self.temperature, 2)

def main():
    """Main program loop"""
    # Initialize LED on pin 13
    led = LED(13)
    
    # Initialize temperature sensor on pin A0
    sensor = TemperatureSensor("A0")
    
    print("\nStarting main loop...")
    print("Press Ctrl+C to exit\n")
    
    try:
        while True:
            # Toggle LED
            led.toggle()
            
            # Read temperature
            temp = sensor.read()
            print(f"Temperature: {temp}°C")
            
            # Wait 1 second
            time.sleep(1)
    
    except KeyboardInterrupt:
        print("\n\nProgram stopped by user")
        led.off()
        print("Cleanup complete")

if __name__ == "__main__":
    main()
