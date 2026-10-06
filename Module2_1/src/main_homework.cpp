#include <Arduino.h>

enum class LedState {
  On,
  Off
};

enum class LedMode {
  Blink, 
  Always_On,
  Always_Off
};

struct Config {
  static constexpr int LED_PIN = 15;
  static constexpr int BUTTON_PIN = 17;

  static constexpr int BUTTON_DEBOUNCE_MS = 50;
  static constexpr int BLINK_INTERVAL = 500;

  static constexpr int SERIAL_INTERVAL = 9600;
  static constexpr int AVG_TIME_LOG_ITERATIONS = 100000;
};

class Led {
  private: 
    int pin;

    LedMode currentMode = LedMode::Blink;
    LedState currentLedState;

    unsigned long lastBlinkTime = 0;

    void setState(LedState newState) {
      currentLedState = newState;
      digitalWrite(pin, newState == LedState::On ? HIGH : LOW);
    }

  public:
    Led(int pin) : pin(pin) {}
    
    void init() {
      pinMode(pin, OUTPUT);
      setState(LedState::Off);
    }

    void switchLedMode() {
      switch (currentMode) {
        case LedMode::Always_On:
          currentMode = LedMode::Always_Off;
          Serial.println("Switched to ALWAYS OFF mode !");
          break;

        case LedMode::Always_Off:
          currentMode = LedMode::Blink;
          Serial.println("Switched to BLINK mode !");
          break;

        case LedMode::Blink:
          currentMode = LedMode::Always_On;
           Serial.println("Switched to ALWAYS ON mode !");
          break;
    }
}
    
    void update() {
      switch (currentMode) {
        case LedMode::Always_On:
            setState(LedState::On);
            break;

        case LedMode::Always_Off:
            setState(LedState::Off);
            break;

        case LedMode::Blink:
            if (millis() - lastBlinkTime >= Config::BLINK_INTERVAL) {
                lastBlinkTime = millis();
                setState(currentLedState == LedState::On ? LedState::Off : LedState::On);
            }
            break;
      }
    }
};

class Button {
  private:
    int pin;

  public:
    Button(int pin) : pin(pin) {}
    
    void init() {
      pinMode(pin, INPUT_PULLUP);
    }
};

volatile bool buttonPressed = false;
unsigned int lastInterruptTime = 0;
void IRAM_ATTR onButtonPress() {
  int currentMillis = millis();
  if (currentMillis - lastInterruptTime >= Config::BUTTON_DEBOUNCE_MS) {
    buttonPressed = true;
  }
}

Led led = Led(Config::LED_PIN);
Button button = Button(Config::BUTTON_PIN);

int iterationsCounter = 0;
unsigned long totalTime = 0;

void setup() {
  Serial.begin(Config::SERIAL_INTERVAL);
  led.init();
  button.init();
  attachInterrupt(digitalPinToInterrupt(Config::BUTTON_PIN), onButtonPress, FALLING);
}

void loop() {
  unsigned long startMicros = micros();

  if (buttonPressed) {
    buttonPressed = false;

    led.switchLedMode();
  }

  led.update();
  
  totalTime += micros() - startMicros;
  iterationsCounter++;
  if (iterationsCounter >= Config::AVG_TIME_LOG_ITERATIONS) {
    float averageTime = static_cast<float>(totalTime) / Config::AVG_TIME_LOG_ITERATIONS;

    Serial.printf("Average loop iteration time %.2f uS \n", averageTime);

    totalTime = 0;
    iterationsCounter = 0;
  }
}
