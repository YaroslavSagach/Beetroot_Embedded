#include <Arduino.h>
#include <esp_task_wdt.h>

const int ALARM_LED_PIN = 4;   // Пін для аварійного світлодіода
const int HEATER_RELAY_PIN = 5; // Пін керування нагрівачем (небезпечне навантаження)

// Змінна в RTC-пам'яті. Вона НЕ обнулиться після програмного перезавантаження (esp_restart)
RTC_NOINIT_ATTR int crash_counter = 0; 

// Ця функція автоматично викликається системою, якщо Watchdog таймер вийшов.
// extern "C" обов'язкове, щоб компілятор C++ правильно зв'язав цю функцію з ядром ESP-IDF.
extern "C" void esp_task_wdt_isr_user_handler() {

  // 1. Аварійно ВИМИКАЄМО небезпечне навантаження
  digitalWrite(HEATER_RELAY_PIN, LOW);

  // 2. Вмикаємо візуальну індикацію аварії
  digitalWrite(ALARM_LED_PIN, HIGH);

  // 3. Збільшуємо лічильник аварій у енергонезалежній пам'яті
  crash_counter++;

  // 4. Оскільки ми встановили panic = false, система сама не перезавантажиться.
  // Але залишати її завислою назавжди не можна, тому ми самі викликаємо ресет
  // ПІСЛЯ того, як убезпечили обладнання.
  esp_restart();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ALARM_LED_PIN, OUTPUT);
  pinMode(HEATER_RELAY_PIN, OUTPUT);

  // При старті гасимо аварійний світлодіод
  digitalWrite(ALARM_LED_PIN, LOW);
  
  // Вмикаємо нагрівач (імітація нормальної роботи)
  digitalWrite(HEATER_RELAY_PIN, HIGH);

  Serial.printf("\n--- Запуск. Кількість попередніх аварій: %d ---\n", crash_counter);

  // Ініціалізуємо Watchdog на 3 секунди, panic = FALSE
  esp_task_wdt_init(3, false);
  esp_task_wdt_add(NULL); 
}

void loop() {
  static int counter = 0;
  counter++;

  esp_task_wdt_reset(); 
  Serial.println("Система працює нормально...");
  delay(1000); 

  // Імітація жорсткого зависання на 5-й секунді
  if (counter >= 5) {
    Serial.println("Зависання! Чекаємо спрацьовування аварійного колбеку...");
    while (true) {
      delayMicroseconds(10000); 
    }
  }
}