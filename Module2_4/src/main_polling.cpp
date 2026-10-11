// #include <Arduino.h>

// struct Config {
//   static constexpr int BUTTON_PIN = 15;
//   static constexpr int BUTTON_DEBOUNCE_MS = 50;
//   static constexpr int POLL_INTERVAL = 10;
// };

// int buttonPressCounter = 0;
// bool buttonStableState = HIGH;
// bool lastButtonRawState = buttonStableState;

// unsigned long lastStateChangeTime = 0;
// unsigned long lastPollTime = 0;
// unsigned long totalDelayTime = 0;

// void setup() {
//   Serial.begin(9600);
//   pinMode(Config::BUTTON_PIN, INPUT);
// }

// void loop() {
//   unsigned long currentMillis = millis();

//   if (currentMillis - lastPollTime >= Config::POLL_INTERVAL) {
//     lastPollTime = currentMillis;

//     int buttonRawState = digitalRead(Config::BUTTON_PIN);

//     if (buttonRawState != lastButtonRawState) {
//         lastButtonRawState = buttonRawState;
//         lastStateChangeTime = currentMillis;
//     }

//     if (buttonRawState != buttonStableState && currentMillis - lastStateChangeTime >= Config::BUTTON_DEBOUNCE_MS) {
//         buttonStableState = buttonRawState;

//         if (buttonStableState == LOW) {
//           buttonPressCounter++;

//           unsigned long interruptDelay = millis() - lastStateChangeTime;
//           totalDelayTime += interruptDelay;
//           float avgDelay = static_cast<float>(totalDelayTime) / buttonPressCounter;

//           Serial.printf("Button presses : %d. \n", buttonPressCounter);
//           Serial.printf("Delay time in ms : %d. Avg delay : %0.2f \n", interruptDelay, avgDelay);
//         }
//     }
    
//   }
// }