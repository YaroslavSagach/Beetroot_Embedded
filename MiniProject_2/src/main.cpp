#include <Arduino.h>
#include <esp_task_wdt.h>

struct Config {
  static constexpr int BUTTON_PIN     = 4;
  static constexpr int LED_PIN_GREEN  = 5;
  static constexpr int LED_PIN_YELLOW = 6;
  static constexpr int LED_PIN_RED    = 7;

  static constexpr uint32_t T_GREEN_MS           = 10000UL;  // зелений: 10 с
  static constexpr uint32_t T_GREEN_BLINK_MS     = 3000UL;   // зелений миготливий: 3 с
  static constexpr uint32_t T_YELLOW_MS          = 3000UL;   // жовтий: 3 с
  static constexpr uint32_t T_RED_MS             = 10000UL;  // червоний: 10 с
  static constexpr uint32_t T_RED_YELLOW_MS      = 2000UL;   // червоний + жовтий: 2 с
  static constexpr uint32_t BLINK_HALF_PERIOD_MS = 500UL;    // півперіод миготіння: 0,5 с
  static constexpr uint32_t BUTTON_DEBOUNCE_MS   = 200UL;

  static constexpr int TIMER_DIVIDER = 80;
};

#define MS_TO_TICKS(ms) ((uint64_t)(ms) * 1000ULL)

enum TrafficState {
  STATE_GREEN = 0,      // зелений
  STATE_GREEN_BLINK,    // зелений миготливий
  STATE_YELLOW,         // жовтий
  STATE_RED,            // червоний
  STATE_RED_YELLOW,     // червоний + жовтий
  STATE_YELLOW_BLINK    // особливий режим: жовтий миготливий
};

hw_timer_t *stateTimer = NULL;
hw_timer_t *blinkTimer = NULL;

TrafficState currentTrafficState = TrafficState::STATE_GREEN;
int ledStates[3] = {
  1,  // green
  0,  // yellow
  0   // red
};

void updateLeds() {
  digitalWrite(Config::LED_PIN_GREEN, ledStates[0]);
  digitalWrite(Config::LED_PIN_YELLOW, ledStates[1]);
  digitalWrite(Config::LED_PIN_RED, ledStates[2]);
}

bool stateFlag = 0;
void IRAM_ATTR onStateTimer() {
  stateFlag = 1;
}

bool blinkFlag = 0;
void IRAM_ATTR onBlinkTimer() {
  blinkFlag = 1;
}

bool buttonPressed = 0;
uint32_t lastButtonPressTime = 0;
void IRAM_ATTR onButtonPress() {
  buttonPressed = 1;
}

bool specialModOn = 0;

void setup() {
  Serial.begin(9600);
  pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
  pinMode(Config::LED_PIN_GREEN, OUTPUT);
  pinMode(Config::LED_PIN_YELLOW, OUTPUT);
  pinMode(Config::LED_PIN_RED, OUTPUT);

  stateTimer = timerBegin(0, Config::TIMER_DIVIDER, true);
  timerAttachInterrupt(stateTimer, &onStateTimer, true);
  timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_GREEN_MS), true);
  timerAlarmEnable(stateTimer); 

  blinkTimer = timerBegin(1, Config::TIMER_DIVIDER, true);
  timerAttachInterrupt(blinkTimer, &onBlinkTimer, true);
  timerAlarmWrite(blinkTimer, MS_TO_TICKS(Config::BLINK_HALF_PERIOD_MS), true);
  timerAlarmEnable(blinkTimer);

  attachInterrupt(digitalPinToInterrupt(Config::BUTTON_PIN), onButtonPress, FALLING);

  Serial.println("[Traffic Light State] Start from GREEN mode.");
  updateLeds();
}

void loop() {
  unsigned long currentMillis = millis();

  if (buttonPressed && currentMillis - lastButtonPressTime >= Config::BUTTON_DEBOUNCE_MS) {
    lastButtonPressTime = currentMillis;
    buttonPressed = false;
    if (currentTrafficState == TrafficState::STATE_YELLOW_BLINK) {
      currentTrafficState = TrafficState::STATE_GREEN;
      specialModOn = false;
      
      // start from normal Green mode
      ledStates[0] = 1;
      ledStates[1] = 0;
      ledStates[2] = 0;
      updateLeds();

      stateFlag = 0;
      timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_GREEN_MS), true);
      Serial.println("Changed traffic light state to normal. -> GREEN");
    } else {
      currentTrafficState = TrafficState::STATE_YELLOW_BLINK;
      specialModOn = true;

      ledStates[0] = 0;
      ledStates[1] = 1;
      ledStates[2] = 0;
      updateLeds();

      Serial.println("Changed traffic light state to special - [Blinking Yellow].");
    }
  }

  if (blinkFlag) {
    blinkFlag = 0;
    switch (currentTrafficState) {
      case TrafficState::STATE_GREEN_BLINK:
        ledStates[0] = !ledStates[0];
        digitalWrite(Config::LED_PIN_GREEN, ledStates[0]);
        break;

      case TrafficState::STATE_YELLOW_BLINK:
        ledStates[1] = !ledStates[1];
        digitalWrite(Config::LED_PIN_YELLOW, ledStates[1]);
        break;
    
      default:
        break;
    }
  }

  if (stateFlag && !specialModOn) {
    stateFlag = 0;

    switch (currentTrafficState) {
      case TrafficState::STATE_GREEN:
        Serial.println("[Traffic Light State] GREEN -> GREEN BLINK");

        ledStates[0] = 1;
        ledStates[1] = 0;
        ledStates[2] = 0;

        currentTrafficState = TrafficState::STATE_GREEN_BLINK;
        timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_GREEN_BLINK_MS), true);
        break;

      case TrafficState::STATE_GREEN_BLINK:
        Serial.println("[Traffic Light State] GREEN BLINK -> YELLOW");

        ledStates[0] = 0;
        ledStates[1] = 1;
        ledStates[2] = 0;

        currentTrafficState = TrafficState::STATE_YELLOW;
        timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_YELLOW_MS), true);
        break;

      case TrafficState::STATE_YELLOW:
        Serial.println("[Traffic Light State] YELLOW -> RED");

        ledStates[0] = 0;
        ledStates[1] = 0;
        ledStates[2] = 1;

        currentTrafficState = TrafficState::STATE_RED;
        timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_RED_MS), true);
        break;

      case TrafficState::STATE_RED:
        Serial.println("[Traffic Light State] RED -> RED YELLOW");

        ledStates[0] = 0;
        ledStates[1] = 1;
        ledStates[2] = 1;

        currentTrafficState = TrafficState::STATE_RED_YELLOW;
        timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_RED_YELLOW_MS), true);
        break;

      case TrafficState::STATE_RED_YELLOW:
        Serial.println("[Traffic Light State] RED YELLOW -> GREEN");

        ledStates[0] = 1;
        ledStates[1] = 0;
        ledStates[2] = 0;

        currentTrafficState = TrafficState::STATE_GREEN;
        timerAlarmWrite(stateTimer, MS_TO_TICKS(Config::T_GREEN_MS), true);
        break;

      case TrafficState::STATE_YELLOW_BLINK:
        Serial.println("[Traffic Light State] [Special] YELLOW BLINK");
        ledStates[0] = 0;
        ledStates[1] = 1;
        ledStates[2] = 0;
        break;
    }
    
    updateLeds();
  }
}