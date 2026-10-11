// #include <Arduino.h>

// struct Config {
//   static constexpr int BUTTON_PIN = 15;
//   static constexpr int BUTTON_DEBOUNCE_MS = 50;
// };

// volatile bool buttonPressed = false;
// int buttonPressCounter = 0;
// unsigned long lastInterruptTime = 0;
// unsigned long totalDelayTime = 0;


// void IRAM_ATTR onButtonPress() {
//   buttonPressed = true;
//   lastInterruptTime = micros();
// }

// void setup() {
//   Serial.begin(9600);
//   pinMode(Config::BUTTON_PIN, INPUT);
//   attachInterrupt(digitalPinToInterrupt(Config::BUTTON_PIN), onButtonPress, FALLING);
// }

// void loop() {
//   if (buttonPressed) {
//     buttonPressed = false;

//     if (!digitalRead(Config::BUTTON_PIN) == HIGH) {
//       buttonPressCounter++;
    
//       unsigned long interruptDelay = micros() - lastInterruptTime;
//       totalDelayTime += interruptDelay;
//       float avgDelay = static_cast<float>(totalDelayTime) / buttonPressCounter;

//       Serial.printf("Button presses : %d. \n", buttonPressCounter);
//       Serial.printf("Delay time in us : %d. Avg delay : %0.2f \n", interruptDelay, avgDelay);
//     }
//   }

// }