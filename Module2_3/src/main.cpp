#include <Arduino.h>

enum class LedState {
  On,
  Off
};

struct Config {
  static constexpr int RED_LED_PIN = 4;
  static constexpr int BLUE_LED_PIN = 5;
  static constexpr int GREEN_LED_PIN = 6;

  static constexpr int RED_BLINK_INTERVAL = 200;
  static constexpr int BLUE_BLINK_INTERVAL = 500;
  static constexpr int GREEN_BLINK_INTERVAL = 1000;

  static constexpr int SERIAL_INTERVAL = 9600;
};

class Led {
  private: 
    int pin;
    int blinkInterval;
    LedState currentLedState = LedState::Off;
    unsigned int lastTimestamp = 0;

  public:
    Led(int pin, int blinkInterval) : pin(pin), blinkInterval(blinkInterval) {}
    
    void init() {
      pinMode(pin, OUTPUT);
    }

    void switchState() {
      currentLedState = currentLedState == LedState::On ? LedState::Off : LedState::On;
      digitalWrite(pin, currentLedState == LedState::On ? HIGH : LOW);
    }

    void update() {
      unsigned int currentTimestamp = millis();
      
      if (currentTimestamp - lastTimestamp >= blinkInterval) {
        lastTimestamp = currentTimestamp;
        switchState();
      }
    }
};

Led redLed = Led(Config::RED_LED_PIN, Config::RED_BLINK_INTERVAL);
Led blueLed = Led(Config::BLUE_LED_PIN, Config::BLUE_BLINK_INTERVAL);
Led greenLed = Led(Config::GREEN_LED_PIN, Config::GREEN_BLINK_INTERVAL);

void setup() {
  Serial.begin(Config::SERIAL_INTERVAL);
  redLed.init();
  blueLed.init();
  greenLed.init();
}

void loop() {
  redLed.update();
  blueLed.update();
  greenLed.update();
}