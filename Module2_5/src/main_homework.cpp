#include <Arduino.h>
#include <esp_task_wdt.h>

struct Config {
  static constexpr int FAN_CONTROL_PIN = 4;
  static constexpr int WDT_TIMEOUT_SEC = 60;

  // scaled timing for testing
  static constexpr int FAN_WAIT_TIME_SEC = 12;
  static constexpr int FAN_WORK_TIME_SEC = 3;
};

hw_timer_t *timer = NULL;
bool fanAlarm = false;
bool fanState = false;

void IRAM_ATTR onTimerFanOn() {
  fanAlarm = true;
}

void setup() {
  Serial.begin(9600);

  // Watchdog
  esp_task_wdt_init(Config::WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimerFanOn, true);
  timerAlarmWrite(timer, Config::FAN_WAIT_TIME_SEC * 1000000, true);
  timerAlarmEnable(timer);

  pinMode(Config::FAN_CONTROL_PIN, OUTPUT);
  digitalWrite(Config::FAN_CONTROL_PIN, LOW);     
}

void loop() {
  esp_task_wdt_reset();
  
  if (fanAlarm) {
    fanAlarm = false;     
    fanState = !fanState;
    digitalWrite(Config::FAN_CONTROL_PIN, fanState);
    if (fanState) {
      Serial.println("Fan ON. Working....");
      timerAlarmWrite(timer, Config::FAN_WORK_TIME_SEC * 1000000, true);
    } else {
      Serial.println("Fan OFF. Waiting....");
      timerAlarmWrite(timer, Config::FAN_WAIT_TIME_SEC * 1000000, true);
    }  
  }
}