// File Copyright Brian Kramp, 2024

#include <Arduino.h>
#include "Util.h"
#include "Adafruit_MAX1704X.h"
#include <Adafruit_ST7789.h> 
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_ImageReader.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "Secrets.h" // Create a file with your ssid and password
//const char* ssid     = "ssid"; // Change this to your WiFi SSID
//const char* password = "password"; // Change this to your WiFi password

#define MICRO_TO_SECONDS 1000000ULL
#define SECONDS 1000

// DEVICE SETUP

const int BUTTON_PIN_ONE = 0;
const int BUTTON_PIN_TWO = 1;
const int BUTTON_PIN_THREE = 2;
const int BUTTON_PIN_TOP = A2;
const int TIMEZONE = -8; // PST is -8.

ButtonHandler buttonHandler(BUTTON_PIN_ONE, BUTTON_PIN_TWO, BUTTON_PIN_THREE, BUTTON_PIN_TOP);

#define TFT_WIDTH     240
#define TFT_HEIGHT    135

Adafruit_MAX17048 lipo;
Adafruit_ST7789 display = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(TFT_WIDTH, TFT_HEIGHT);
NAU7802 scale;

// Logger globals
void log(String data);
void logFormattedData(const char* fmt, ...);

#define COLOR_DARK_GRAY 0x1084
#define COLOR_MEDIUM_GRAY 0x2108
#define COLOR_LIGHT_GRAY 0x318C
#define COLOR_NEAR_WHITE 0xCE59
#define COLOR_DARK_GREEN 0x0320

Preferences preferences;

// Image rendering
Adafruit_FlashTransport_ESP32 flashTransport;
Adafruit_SPIFlash    flash(&flashTransport);
FatFileSystem        filesys;
Adafruit_ImageReader pillImageReader(filesys);
Adafruit_Image       pillImg;
Adafruit_Image       checkImg;


// Datetime setup
const char* ntpServer = "north-america.pool.ntp.org";
const char* ntpServerApple = "time.apple.com";
const char* ntpServerGoogle = "time.google.com";

const long  gmtOffset_sec = TIMEZONE * 60 * 60; 
const int   daylightOffset_sec = 3600;

// App Setup

enum State {
  STATE_INITIAL,
  STATE_NORMAL,
  STATE_CALIBRATION_TARE,
  STATE_CALIBRATION_BOTTLE,
  STATE_CALIBRATION_PILLS,
  STATE_CALIBRATION_DATE,
  STATE_TARE,
  STATE_SETTINGS,
  STATE_INFO,
  STATE_ERROR,
  STATE_POWER_TEST_1,
  STATE_DEEP_SLEEP,
};

// Define initial state
State currentState = STATE_NORMAL;

void stateInitial(Input input);
void stateNormal(Input input);
void stateCalibrationTare(Input input);
void stateCalibrationBottle(Input input);
void stateCalibrationPills(Input input);
void stateCalibrationDate(Input input);
void stateTare(Input input);
void stateSettings(Input input);
void stateInfo(Input input);
void stateError(Input input);
void statePowerTest1(Input input);
void stateDeepSleep(Input input);

// Define function pointer type for state functions
typedef void (*StateFunction)(Input);

// Array to hold state functions
StateFunction stateFunctions[] = {
  stateInitial,
  stateNormal,
  stateCalibrationTare,
  stateCalibrationBottle,
  stateCalibrationPills,
  stateCalibrationDate,
  stateTare,
  stateSettings,
  stateInfo,
  stateError,
  statePowerTest1,
  stateDeepSleep,
};

bool LOG_STARTUP = false;

int zeroOffset = 577396; // Set by taring the scale. This was my value one time I tried.
float calibrationFactor = 1107.0f; // Set by calibrating the scale. Since we don't care about actual weight, just deltas, precision here isn't important.
float pillWeight = 0.533f; // This value is overwritten by reading from persisted settings.
float bottleWeight = 12.0f; // This value is overwritten by reading from persisted settings.
int finalPillDate = 0; // Some of these variables are no longer used once I started using real datetime
time_t finalPillDateTime = 0;
byte pillsPerDay = 1; // I stopped testing any value other than 1.

// The app assumes you have a date downloaded from wifi. (Some code may be still around that didn't assume this)
bool isTimeInfoValid = false;
struct tm timeinfo;
// If you don't then infer the date from
byte todaysDate = 1;
bool isMorning = true; // assuming 2 pills a day.


void setup() {
  delay(100);
  int startTime = millis();

  // Pins and Power
  pinMode(BUTTON_PIN_ONE, INPUT_PULLUP);
  pinMode(BUTTON_PIN_TWO, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_THREE, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN_TOP, INPUT); // I have a pulldown resistor
  pinMode(NEOPIXEL_POWER, OUTPUT);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, LOW);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // Connect to wifi, cause it takes awhile in the background.
  WiFi.begin(ssid, password);

  // Display
  display.init(TFT_HEIGHT, TFT_WIDTH);
  display.setRotation(3);
  canvas.setFont(&FreeSans12pt7b);
  canvas.setTextColor(ST77XX_WHITE);
  
  if (LOG_STARTUP) logFormattedData("Inited Display %d", millis() - startTime);

  // Boot logo, initilize flash and file system.
  if (!flash.begin()) {
    log("flash failed");
  }
  if(!filesys.begin(&flash)) {
    log("filesys begin() failed");
  }
  display.fillScreen(0);

  // Draw the pill on the screen.
  ImageReturnCode imageStatus = pillImageReader.loadBMP("/PillLogo6.bmp", pillImg);
  if (imageStatus != IMAGE_SUCCESS) {
    logFormattedData("imageLoad: %d", imageStatus);
  }
  GFXcanvas16* imageCanvas = (GFXcanvas16*)pillImg.getCanvas();
  display.drawRGBBitmap(53, 0, imageCanvas->getBuffer(), imageCanvas->width(), imageCanvas->height());

  // Load the check into memory here for use later.
  imageStatus = pillImageReader.loadBMP("/PillCheck2.bmp", checkImg);
  if (imageStatus != IMAGE_SUCCESS) {
    logFormattedData("checkImageLoad: %d", imageStatus);
  }

  int stopwatch_drawBootLogo = millis();

  preferences.begin("MiniScale"); 

  if (LOG_STARTUP) logFormattedData("BootLogo: %d", stopwatch_drawBootLogo - startTime);
  if (LOG_STARTUP) printWakeupReason();

  // Battery
  if (!lipo.begin()) {
    log(String("Battery Error"));
    while (1) delay(100);
  }

  // Scale
  if (!scale.begin()) {
    if (LOG_STARTUP) log("Scale not init");
  }

  if (scale.isConnected()) {
    if (LOG_STARTUP) log("Scale connected");
  }

  readCalibrationSettings();

  if (zeroOffset == 0 || calibrationFactor <= 0 || isnan(calibrationFactor)) {
    log("Bad calibration values");
    transitionTo(STATE_ERROR);
    return;
  }

  scale.setZeroOffset(zeroOffset);
  scale.setCalibrationFactor(calibrationFactor);

  if (LOG_STARTUP) logFormattedData("Setup() %d", millis() - startTime);

  // Give time to connect.
  delay(2500);

  // Check that you're on Wifi, and initilize the date time.
  while (WiFi.status() != WL_CONNECTED) {
    // Infinitely loop here if not on wifi
    log("Not on wifi");
    delay(100);
  }

  // Connect to time server and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServerGoogle, ntpServerApple, ntpServer);

  if (!getLocalTime(&timeinfo)){
    log("bad time");
  } else {
    isTimeInfoValid = true;
    if (LOG_STARTUP) {
      char buf[64];
      strftime(buf, 64, "%A, %B %d %H:%M:%S", &timeinfo);
      log(buf);
      delay(2500);
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  transitionTo(STATE_INITIAL);
}

unsigned long transitionTime;

float getPillCount(float weight) {
    float numPills = (weight - bottleWeight) / pillWeight;
    return numPills;
}

// Define function to transition to the next state
void transitionTo(State nextState) {
  currentState = nextState;
  transitionTime = millis();
}

// State functions
void stateInitial(Input input) {
  // Wait up to 5 seconds for scale to become available.
  WAIT_FOR_CONDITION_WITH_ACTIONS(scale.available(), 5 * SECONDS, log("Scale not ready"); transitionTo(STATE_ERROR););

  transitionTo(STATE_TARE);
}

void stateNormal(Input input) {

  if (finalPillDateTime == 0) {
    transitionTo(STATE_CALIBRATION_DATE);
    return;
  }

  static bool debounceNextWeight = false;

  // Only redraw every 500ms.
  EXECUTE_EVERY(500, {

    // TODO: It takes 40ms for 8 samples, seemingly double for 16.
    // Might be more responsive to calculate our own average.
    float weight = scale.getWeight(true, 8); 

    canvas.fillScreen(ST77XX_BLACK);
    canvas.setFont(&FreeSans9pt7b);
    canvas.setCursor(0, 17);

    weight = max(weight, 0.0f); // print weight is negative, you need to tare?

    int pillCount = getPillCount(weight) + .5f;
    int daysDelta = computeDaysDeltaFromNow(finalPillDateTime);

    // Connected, on track.
    if (isTimeInfoValid && daysDelta == pillCount) {
      // Render the check image.
      debounceNextWeight = true;
      GFXcanvas16* imageCanvas = (GFXcanvas16*)checkImg.getCanvas();
      canvas.drawRGBBitmap(55, 2, imageCanvas->getBuffer(), imageCanvas->width(), imageCanvas->height());

      // // Date (debug)
      // canvas.setTextColor(ST77XX_WHITE);
      // canvas.print(todaysDate);
      // canvas.print(" : ");
      // canvas.print(timeinfo.tm_mday);

      // battery
      float batteryLevel = lipo.cellPercent();
      if (batteryLevel > 15.0f) {
        canvas.setTextColor(COLOR_DARK_GREEN);
      } else {
        canvas.setTextColor(ST77XX_RED);
      }
      canvas.setCursor(185, 17);
      canvas.print(batteryLevel, 0);
      canvas.print("%");

      const int screenBottom = 129;
      // Date
      char buf[64];
      strftime(buf, 64, "%b %d", &timeinfo);
      canvas.setTextColor(COLOR_NEAR_WHITE);
      canvas.setCursor(0, screenBottom);
      canvas.print(buf);

      // Pills
      canvas.setCursor(160, screenBottom);
      canvas.print(getPillCount(weight), 1);
      canvas.println(pillCount != 1 ? " pills" : " pill");
    }
    else if (pillCount > 0 && pillCount < 10000)
    {
      // Pill count incorrect
      if (debounceNextWeight) {
        debounceNextWeight = false;
        return;
      }

      canvas.setTextColor(ST77XX_WHITE);
      canvas.setFont(&FreeSans12pt7b);
      canvas.setCursor(1, 15);
      canvas.println("");
      canvas.println("");
      
      int expected = daysDelta;
      int delta = pillCount - expected;
      if (delta > 0) {
        canvas.print("         ");
        canvas.print(delta);
        canvas.println(delta == 1 ? " extra pill" : " extra pills");
      } else {
        delta = -delta;
        canvas.print("        ");
        canvas.print(delta);
        canvas.println(delta == 1 ? " pill missing" : " pills missing");
      }

      canvas.setTextColor(COLOR_NEAR_WHITE);
      canvas.println();
      canvas.print("<- Setup           ");
      canvas.print(pillCount);
      canvas.print(pillCount == 1 ? " pill" : " pills");
    } 
    else
    {
      // Empty
      debounceNextWeight = true;

      GFXcanvas16* imageCanvas = (GFXcanvas16*)pillImg.getCanvas();
      canvas.drawRGBBitmap(53, 0, imageCanvas->getBuffer(), imageCanvas->width(), imageCanvas->height());

      // battery
      float batteryLevel = lipo.cellPercent();
      if (batteryLevel > 15.0f) {
        canvas.setTextColor(COLOR_DARK_GREEN);
      } else {
        canvas.setTextColor(ST77XX_RED);
      }
      canvas.setCursor(185, 17);
      canvas.print(batteryLevel, 0);
      canvas.print("%");

      
      const int screenBottom = 129;
      // Date
      char buf[64];
      strftime(buf, 64, "%b %d", &timeinfo);
      canvas.setTextColor(COLOR_NEAR_WHITE);
      canvas.setCursor(0, screenBottom);
      canvas.print(buf);

      canvas.setCursor(140, screenBottom);
      canvas.println("scale empty");
    }

    canvas.setFont(&FreeSans12pt7b);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), TFT_WIDTH, TFT_HEIGHT);
  }); // End execute every

  if (input.buttonOnePressed) {
    transitionTo(STATE_DEEP_SLEEP);

    // If you weigh a can of food, put the grams here, and you can read out the calibration.
    //canvas.print("D0, ");
    //scale.calculateCalibrationFactor(508.5f); // Can of refried beans
    //logFormattedData("Calibration %.2f", scale.getCalibrationFactor());
  }
  
  if (input.buttonTwoPressed) {
    // Start Calibration.
    transitionTo(STATE_SETTINGS);
  }

  if (input.buttonThreePressed) {
    // Just fix the date.
    transitionTo(STATE_CALIBRATION_DATE);
  }

  if (input.buttonTopPressed) {
    transitionTo(STATE_TARE);
  }

  // After 30 seconds in normal mode, power down.
  if (millis() - transitionTime > 30 * SECONDS) {
    transitionTo(STATE_DEEP_SLEEP);
  }
}

void stateCalibrationTare(Input input) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setCursor(0, 17);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.println("Empty the scale");
  canvas.println("and press a button");
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
  delay(1);

  if (input.buttonThreePressed || input.buttonTopPressed) {
    scale.calculateZeroOffset(64);
    transitionTo(STATE_CALIBRATION_BOTTLE);
  }
}

void stateCalibrationBottle(Input input) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setCursor(0, 17);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.println("Place the bottle");
  canvas.println("and press a button");
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
  delay(1);

  if (input.buttonThreePressed || input.buttonTopPressed) {
    bottleWeight = scale.getWeight(false, 64);
    // TODO: Validation
    transitionTo(STATE_CALIBRATION_PILLS);
  }
}

void stateCalibrationPills(Input input) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setCursor(0, 17);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.println("Place 30 pills");
  canvas.println("and press a button");
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
  delay(1);

  if (input.buttonThreePressed || input.buttonTopPressed) {
    float pills30 = scale.getWeight(false, 64);
    pillWeight = (pills30 - bottleWeight) / 30.0f;
    // TODO: Validation
    storeWeights(bottleWeight, pillWeight);
    transitionTo(STATE_CALIBRATION_DATE);
  }
}

void stateCalibrationDate(Input input) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setCursor(0, 30);
  canvas.setTextColor(ST77XX_WHITE);

  canvas.println("Place pills on the scale");
  canvas.println();
  canvas.println("Press button to save");
  canvas.println("pillcount");

  if (input.buttonTopPressed && isTimeInfoValid) {
    canvas.fillScreen(ST77XX_BLACK);
    canvas.setCursor(0, 17);
    float weight = scale.getWeight(false, 16);
    float fPillCount = getPillCount(weight);
    int pillCount = fPillCount + .5f;

    // Assumes 1 pill per day.    
    time_t finalTime = addDaysFromNow(pillCount);
    tm *localTime = localtime(&finalTime);
    localTime->tm_sec = 0;
    localTime->tm_min = 0;
    localTime->tm_hour = 0;
    time_t finalMidnight = mktime(localTime);
    storeFinalPillDateTime(finalMidnight);

    canvas.println("Saving settings.");
    canvas.print(fPillCount, 1);
    canvas.println(" pills");
    canvas.println();
    canvas.print("Last pill on ");
    char buf[64];
    strftime(buf, 64, "%B %d", localTime);
    canvas.println(buf);
    display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
    delay(4000);
    transitionTo(STATE_NORMAL);
    return;
  }
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
}

void stateSettings(Input input) {
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setCursor(0, 17);
  canvas.setTextColor(ST77XX_WHITE);

  canvas.println("Info");
  canvas.println();
  canvas.println("Setup weights");
  canvas.println();
  canvas.println("Correct pill count");
  display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);


  if (input.buttonOnePressed) {
    transitionTo(STATE_INFO);
  }
  if (input.buttonTwoPressed) {
    transitionTo(STATE_CALIBRATION_TARE);
  }
  if (input.buttonThreePressed) {
    transitionTo(STATE_CALIBRATION_DATE);
  }
  if (input.buttonTopPressed) {
    transitionTo(STATE_NORMAL);
  }
}

void stateInfo(Input input) {

  // Only redraw every 500ms.
  EXECUTE_EVERY(500, {
    float weight = scale.getWeight(true, 8); 
    canvas.fillScreen(ST77XX_BLACK);
    canvas.setFont(&FreeSans12pt7b);
    canvas.setCursor(0, 17);

    canvas.print("Weight: ");
    canvas.println(weight, 1);

    canvas.print("Bottle: ");
    canvas.println(bottleWeight, 1);
    canvas.print("Pill:   ");
    canvas.println(pillWeight, 2);

    if (isTimeInfoValid) {
      char buf[64];
      strftime(buf, 64, "%a, %b %d %H:%M", &timeinfo);
      canvas.println(buf);
    } else {
      canvas.println("No wifi");
    }

    tm *localTime = localtime(&finalPillDateTime);
    canvas.print("Final:  ");
    char buf[64];
    strftime(buf, 64, "%B %d", localTime);
    canvas.println(buf);

    display.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
  }); // End execute every

  if (input.buttonTopPressed) {
    transitionTo(STATE_NORMAL);
  }
}

void stateTare(Input input) {
  //log("Taring...");
  scale.calculateZeroOffset(16);

  transitionTo(STATE_NORMAL);
}

void stateError(Input input) {
  // Handle state actions
}

void statePowerTest1(Input input) {
  // log("No TFT Backlite");
  // digitalWrite(TFT_BACKLITE, LOW);
  // delay(5000);
  // This worked well.

  // log("No TFT Power");
  // digitalWrite(TFT_I2C_POWER, LOW);
  // delay(5000);
  // For some reason this used more power than the backlite?!
  
  digitalWrite(TFT_BACKLITE, LOW);

  log("Light sleep");
  uint64_t sleepTime = 10ULL * MICRO_TO_SECONDS;
  esp_sleep_enable_timer_wakeup(sleepTime); // 10 sec
  esp_light_sleep_start();

  digitalWrite(TFT_BACKLITE, HIGH);
  digitalWrite(TFT_I2C_POWER, HIGH);
  log("Restored Power");
  delay(5000);

  transitionTo(STATE_NORMAL);
}

void stateDeepSleep(Input input) {
  //int error = esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 1); // Works for button 3.
  int error = esp_sleep_enable_ext0_wakeup(GPIO_NUM_16, 1); // Top button, A2.
  if (error == ESP_OK) {
    esp_deep_sleep_start();
  }
  log("Invalid Pin for wakeup");
  delay(5000);
  transitionTo(STATE_NORMAL);
}

void printWakeupReason(){
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : log("Wakeup via RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : log("Wakeup via RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : log("Wakeup via timer"); break;
    default : logFormattedData("Initial boot: %d", wakeup_reason); break;
  }
}

void loop() {
  Input input = buttonHandler.handleButtons();
  
  // Call the current state function
  stateFunctions[currentState](input);
}

const char* key_watermark = "Watermark";
const char* key_calibrationFactor = "Calibration";
const char* key_zeroOffset = "ZeroOffset";
const char* key_bottleWeight = "BottleWeight";
const char* key_pillWeight = "PillWeight";
const char* key_finalPillDate = "FinalPillDate";
const char* key_finalPillDateTime = "FinalPillDT";
/// Reads the current system settings from EEPROM
/// If anything looks weird, reset setting to default value
void readCalibrationSettings(void)
{
  bool forceWriteSettings = false;
  float settingCalibrationFactor; //Value used to convert the load cell reading to grams
  int32_t settingZeroOffset; //Zero value that is found when scale is tared
  float settingBottleWeight;
  float settingPillWeight;

  const int32_t appWatermark = 0x4111; // Just a random number to read from the flash. If this isn't present, assumes no settings are valid.
  int32_t settingWatermark = 1;
    
  // Look up the app watermark. If it's not present, erase settings.
  settingWatermark = preferences.getInt(key_watermark);
  if (settingWatermark != appWatermark)
  {
    preferences.putInt(key_watermark, appWatermark);
    forceWriteSettings = true;
    log("No watermark calibrating");
  }
  
  //logFormattedData("Watermark 0x%x", settingWatermark);

  //Look up the calibration factor
  settingCalibrationFactor = preferences.getFloat(key_calibrationFactor);
  if (settingCalibrationFactor == 0 || isnan(settingCalibrationFactor) || forceWriteSettings)
  {
    settingCalibrationFactor = 1107.0f;
    preferences.putFloat(key_calibrationFactor, settingCalibrationFactor);
  } else {
    calibrationFactor = settingCalibrationFactor;
    //logFormattedData("CalFactor %.2f", settingCalibrationFactor);
  }

  // TODO: Is storing zero offset even worthwhile? Probably should delete this.
  // I was hoping I wouldn't have to tare each time, but it's necessary.
  //Look up the zero tare point
  settingZeroOffset = preferences.getInt(key_zeroOffset);
  if (settingZeroOffset != 0)
  {
    zeroOffset = settingZeroOffset;
    //logFormattedData("Zero offset %d", settingZeroOffset);
  }

  //Look up the bottle weight
  settingBottleWeight = preferences.getFloat(key_bottleWeight);
  if (settingBottleWeight != 0 && !isnan(settingBottleWeight))
  {
    bottleWeight = settingBottleWeight;
    //logFormattedData("Bottle Weight: %.1f", settingBottleWeight);
  }

  //Look up the pill weight
  settingPillWeight = preferences.getFloat(key_pillWeight);
  if (settingPillWeight != 0 && !isnan(settingPillWeight))
  {
    pillWeight = settingPillWeight;
    //logFormattedData("Pill Weight: %.1f", settingPillWeight);
  }

  //Look up the finalPillDate
  finalPillDate = preferences.getInt(key_finalPillDate);
  finalPillDateTime = preferences.getLong(key_finalPillDateTime);
}

void storeFinalPillDate(int finalPillDate) {
  preferences.putInt(key_finalPillDate, finalPillDate);
}

void storeFinalPillDateTime(time_t dateTime) {
  preferences.putLong(key_finalPillDateTime, dateTime);
  finalPillDateTime = dateTime;
}

void storeWeights(float bottle, float pill) {
  preferences.putFloat(key_bottleWeight, bottle);
  preferences.putFloat(key_pillWeight, pill);
}

int computeDaysDeltaFromNow(time_t targetDateTime) {
  time_t currentTime;
  time(&currentTime);
  time_t difference = abs(targetDateTime - currentTime);

  // Convert seconds to days (86400 seconds per day)
  int days = difference / 86400 + 1;

  return days;
}

// Function to add days to a time_t object and return a new time_t
time_t addDaysFromNow(int daysToAdd) {
  time_t currentTime;
  time(&currentTime);
  currentTime += daysToAdd * 86400;
  return currentTime;
}

// Logger globals
#define LOGGER_BUFFER_SIZE 7
String loggerBuffer[LOGGER_BUFFER_SIZE];
int loggerIndex = 0;
#define FONT_SIZE 2
#define FONT_HEIGHT (FONT_SIZE * 8)
#define LINE_SPACING 2

void log(String data) {
  // Add data to buffer
  loggerBuffer[loggerIndex] = data;
  loggerIndex = (loggerIndex + 1) % LOGGER_BUFFER_SIZE;

  // Clear display
  display.fillScreen(0);

  // Draw logged data
  int yPos = TFT_HEIGHT - (LOGGER_BUFFER_SIZE * (FONT_HEIGHT + LINE_SPACING));
  for (int i = 0; i < LOGGER_BUFFER_SIZE; i++) {
    int index = (loggerIndex + i) % LOGGER_BUFFER_SIZE;
    display.setCursor(0, yPos);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(FONT_SIZE);
    display.println(loggerBuffer[index]);
    yPos += FONT_HEIGHT + LINE_SPACING;
  }
}

void logFormattedData(const char* fmt, ...) {
  char logbuffer[50];
  va_list args;
  va_start(args, fmt);
  vsnprintf(logbuffer, sizeof(logbuffer), fmt, args);
  va_end(args);
  log(logbuffer);
}
