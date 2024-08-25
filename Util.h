#define WAIT_FOR_CONDITION_WITH_ACTIONS(condition, timeout, action) \
do { \
  unsigned long startTime = millis(); \
  while (!(condition)) { \
    if (millis() - startTime >= timeout) { \
      action; \
      return; \
    } \
    delay(10); \
  } \
} while(0)

void log(String data);
void logFormattedData(const char* fmt, ...) ;

#define EXECUTE_EVERY(interval, block) \
do { \
  static unsigned long lastExecutionTime = 0; \
  unsigned long currentTime = millis(); \
  if (currentTime - lastExecutionTime >= (interval)) { \
    lastExecutionTime = currentTime; \
    block \
  } \
} while(0)


class Stopwatch {
  unsigned long startTime;

public:
  Stopwatch() {
    startTime = millis();
  }

  void Log(const char* message) {
    unsigned long endTime = millis();
    unsigned long duration = endTime - startTime;
    logFormattedData("%s %d", message, duration);
  }
};

class Button {
private:
    const int BUTTON_PIN;
    unsigned long lastPressTime = 0;
    bool lastButtonState = LOW;
    const bool pressed;
    bool reportedPress = false;

public:
    Button(int pin, bool pressed = HIGH) : BUTTON_PIN(pin), pressed(pressed) {}

    bool handleButton(unsigned long currentMillis) {
        bool buttonState = digitalRead(BUTTON_PIN) == pressed;

        if (buttonState && !lastButtonState) {
            reportedPress = false;
        }

         if (!buttonState && lastButtonState) {
            // Button released, reset.
            reportedPress = false;
            lastPressTime = 0;
            delay(10);
        }
        
        if (!reportedPress && buttonState && (currentMillis - lastPressTime >= 600)) {
            reportedPress = true;
            lastPressTime = currentMillis;
            delay(10);
            return true;
        } else if (reportedPress && buttonState && (currentMillis - lastPressTime >= 100)) {
            lastPressTime = currentMillis;
            return true;
        }
        
        lastButtonState = buttonState;
        return false;
    }
};

// Define input structure
struct Input {
  bool buttonOnePressed;
  bool buttonTwoPressed;
  bool buttonThreePressed;
  bool buttonTopPressed;
};

class ButtonHandler {
private:
    Button buttonOne;
    Button buttonTwo;
    Button buttonThree;
    Button buttonTop;

public:
    ButtonHandler(int pinOne, int pinTwo, int pinThree, int pinTop)
        : buttonOne(pinOne, LOW), buttonTwo(pinTwo), buttonThree(pinThree), buttonTop(pinTop) {}

    Input handleButtons() {
        unsigned long currentMillis = millis();
        Input input;

        input.buttonOnePressed = buttonOne.handleButton(currentMillis);
        input.buttonTwoPressed = buttonTwo.handleButton(currentMillis);
        input.buttonThreePressed = buttonThree.handleButton(currentMillis);
        input.buttonTopPressed = buttonTop.handleButton(currentMillis);

        return input;
    }
};


