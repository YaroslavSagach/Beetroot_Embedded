#include <Arduino.h>

constexpr int RELAY_CTRL_PIN = 2;
constexpr int RELAY_SENSE_PIN = 4;

volatile uint32_t reactionTime = 0;
volatile bool eventCaptured = false; 

uint32_t triggerTime = 0;
bool relayState = false; 
bool resultPrinted = true; 

int cycleCount = 0;               
constexpr int MAX_CYCLES = 10;        

uint32_t totalTimeOn = 0;         
uint32_t totalTimeOff = 0;        

uint32_t lastToggleTime = 0;      
constexpr uint32_t TOGGLE_INTERVAL = 1000; 



void IRAM_ATTR relayISR() {
    if (!eventCaptured) {
        reactionTime = micros();
        eventCaptured = true;
    }
}

void setup() {
    Serial.begin(9600);
    
    pinMode(RELAY_CTRL_PIN, OUTPUT);
    digitalWrite(RELAY_CTRL_PIN, LOW);
    
    pinMode(RELAY_SENSE_PIN, INPUT_PULLUP);
    
    attachInterrupt(digitalPinToInterrupt(RELAY_SENSE_PIN), relayISR, CHANGE);
    
    delay(2000);
}

void loop() {
    if (cycleCount >= MAX_CYCLES) {
        static bool finished = false;
        if (!finished) {
            Serial.println("\n====== SUMMARY ======");
            Serial.printf("Average ON time:  %lu us\n", totalTimeOn / MAX_CYCLES);
            Serial.printf("Average OFF time: %lu us\n", totalTimeOff / MAX_CYCLES);
            Serial.println("======================");
            finished = true;
        }
        return;
    }

    if (eventCaptured && !resultPrinted) {
        uint32_t duration = reactionTime - triggerTime;
        
        if (relayState) {
            totalTimeOn += duration;
            Serial.printf("Cycle %d. ON time:  %lu us\n", cycleCount + 1, duration);
        } else {
            totalTimeOff += duration;
            Serial.printf("Cycle %d. OFF time: %lu us\n", cycleCount + 1, duration);
            cycleCount++; 
        }
        resultPrinted = true;
    }

    uint32_t currentMillis = millis();
    
    if (currentMillis - lastToggleTime >= TOGGLE_INTERVAL) {
        lastToggleTime = currentMillis;
        
        relayState = !relayState;
        
        eventCaptured = false;
        resultPrinted = false;
        
        triggerTime = micros();
        
        digitalWrite(RELAY_CTRL_PIN, relayState ? HIGH : LOW); 
    }
}