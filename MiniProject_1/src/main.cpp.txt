#include <Arduino.h>
const int SENSOR_PIN = 6; // Пін підключення дільника з фоторезистором (GPIO In)
const int RELAY_PIN = 4;  // Пін керування транзистором реле (GPIO Out)


const int THRESHOLD_DARK = 2200; 
const int THRESHOLD_LIGHT = 2900;
const float FILTRATION_COEF = 0.2f;

int lightValue = 0;
int filteredLightValue = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); 
  
  // встановлення роздільної здатності АЦП (12 біт = 0...4095)
  analogReadResolution(12);
}

int emaFilter(int previousValue, int rawValue)
{
  return static_cast<int>(
    round(FILTRATION_COEF * rawValue + (1.0f - FILTRATION_COEF) * previousValue)
  );
}

void loop() {
  lightValue = analogRead(SENSOR_PIN);
  
  Serial.printf(">Raw:%d \n", lightValue);

  filteredLightValue = emaFilter(filteredLightValue, lightValue);
  Serial.printf(">Filtered:%d \n", filteredLightValue);

  if (filteredLightValue < THRESHOLD_DARK) {
    digitalWrite(RELAY_PIN, HIGH);
  } else if(filteredLightValue > THRESHOLD_LIGHT) {
    digitalWrite(RELAY_PIN, LOW);  
  }

  delay(100);
}