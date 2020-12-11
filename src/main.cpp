#include <PubSubClient.h>

#include "Adafruit_NeoPixel.h"
#include "ESP8266WiFi.h"

// Read settingd from config.h
#include "config.h"

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

// Initialize Adafruit_NeoPixel
Adafruit_NeoPixel pixels =
    Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espClient;
// or... use WiFiFlientSecure for SSL
// WiFiClientSecure espClient;

// Initialize MQTT
PubSubClient mqttClient(espClient);
// used for splitting arguments to effects
String s = String();

// Variable to store voltage
#ifdef READVOLTAGE
float lastVolt = 0.0;
int cells = -1;
unsigned long nextVoltageLoop = 0;
#endif

// Variable to store Wifi retries (required to catch some problems when i.e. the
// wifi ap mac address changes)
uint8_t wifiConnectionRetries = 0;

// Logic switches
bool readyToUpload = false;
long lastMsg = 0;
bool initialPublish = false;

// Save var for saving the default effect
byte defaultPayload[255];
unsigned int defaulLength;
bool defaultSaved = false;

// ====== internal effect function variables ======
// general fade loop vars
unsigned long currentFadeStart = 0;
unsigned long currentFadeEnd = 0;
uint8_t fadeStartColor[] = {0, 0, 0};
uint8_t fadeCurrentColor[] = {0, 0, 0};
uint8_t fadeCount = 0;

// RGB Cycle general control (used for cycle and RGB run effect)
unsigned long diffRgbLoop = 0;
unsigned long lastRgbLoop = 0;
// unsigned long nextRgbLoop = 0;
uint8_t rgbCurrentColor[] = {0, 0, 0};
uint16_t rgbCycleStep = 0;
uint8_t rgbCycleMaxBrightness = 0;
uint8_t rgbCycleDecColour = 0;
uint8_t rgbCycleIncColour = 0;

// ====== advanced effects vars ======
uint8_t startColor[] = {0, 0, 0};
uint8_t currentColor[] = {0, 0, 0};
uint8_t targetColor[] = {0, 0, 0};

// sunrise loop vars
bool doSunrise = false;
unsigned long sunriseStartTime = 0;
int sunriseLoopStep = 0;

// fixed color vars
bool doFixedColor = false;

// fire
bool doFire = false;
unsigned long nextFireLoop = 0;

// run
bool doRun = false;
uint8_t runLeds = 0;
uint16_t runDelay = 0;
bool runDirection = false;
unsigned long nextRunLoop = 0;
uint8_t runIndex = 0;
uint8_t activeRunColor[] = {0, 0, 0};
uint8_t inactiveRunColor[] = {0, 0, 0};

// flash
bool doFlash = false;
unsigned long flashStartTime = 0;
uint16_t flashLoopStep = 0;
uint8_t flashColor[] = {0, 0, 0};
uint8_t flashCurrentColor[] = {0, 0, 0};

// RGB Cycle effect control
bool doRgbCycle = false;
uint16_t rgbCycleDelay = 0;

// RGB Run
bool doRgbRun = false;
// RGB Cycle (cycle vars, non RGB)
unsigned long nextCycleLoop = 0;

// twinkle
bool doTwinkle = false;
uint8_t twinkleBgColor[] = {0, 0, 0};
uint8_t twinkleColor[] = {0, 0, 0};
uint8_t twinkleTmpColor[] = {0, 0, 0};
uint16_t twinkleMinDelay = 0;
uint16_t twinkleMaxDelay = 0;
uint16_t twinkleMinDuration = 0;
uint16_t twinkleMaxDuration = 0;
unsigned long nextTwinkleStart = 0;
unsigned long twinkleTmpStartTime = 0;
unsigned long twinkleTmpEndTime = 0;
int16_t twinkleLedIndex[MAX_TWINKLES];
uint16_t twinkleLedDuraion[MAX_TWINKLES];
unsigned long twinkleLedStart[MAX_TWINKLES];
unsigned long twinkleLastLoop = 0;

#ifdef READVOLTAGE
float readVoltage() {
  return ((float)analogRead(VOLT_PIN) - 0.0) * (28.0 - 0.0) / (1024.0 - 0.0);
}
#endif

#ifdef BEEPER
uint16_t buzzUntil = 0;
void buzzerCheck() {
  if (millis() > buzzUntil) {
    digitalWrite(BUZZ_PIN, LOW);
  }
}
void buzz(uint16_t duration) {
  buzzUntil = millis() + duration;
  digitalWrite(BUZZ_PIN, HIGH);
}
#endif

void runDefault();

bool mqttReconnect() {
  // Create a client ID based on the MAC address
  String clientId = String("D1WS2812") + "-";
  clientId += String(WiFi.macAddress());

  // Loop 5 times or until we're reconnected
  int counter = 0;
  while (!mqttClient.connected()) {
    counter++;
#ifdef BEEPER
    buzzerCheck();
#endif
    if (counter > 5) {
      DEBUG_PRINTLN("Exiting MQTT reconnect loop");
      return false;
    }

    DEBUG_PRINT("Attempting MQTT connection...");

    // Attempt to connect
    String clientMac = WiFi.macAddress();
    char lastWillTopic[36] = "/d1ws2812/lastwill/";
    strcat(lastWillTopic, clientMac.c_str());
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                           lastWillTopic, 1, 1, clientMac.c_str())) {
      DEBUG_PRINTLN("connected");

      // clearing last will message
      mqttClient.publish(lastWillTopic, "", true);

      // subscribe to "all" topic
      mqttClient.subscribe("/d1ws2812/all", 1);

      // subscript to the mac address (private) topic
      char topic[27];
      strcat(topic, "/d1ws2812/");
      strcat(topic, clientMac.c_str());
      mqttClient.subscribe(topic, 1);

#ifdef BEEPER
      buzz(100);
#endif
      return true;
    } else {
      DEBUG_PRINT("failed, rc=");
      DEBUG_PRINT(mqttClient.state());
      DEBUG_PRINTLN(" try again in 2 seconds");
      // Wait 1 second before retrying
      delay(1000);
    }
  }
  return false;
}

// fill the neopixel dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
  // since it is very slow to call pixels.show on 120 leds, only do that
  // if it is required.
  if (wait) {
    for (uint16_t i = 0; i < pixels.numPixels(); i++) {
      delay(wait);
      pixels.setPixelColor(i, c);
      pixels.show();
    }
  } else {
    for (uint16_t i = 0; i < pixels.numPixels(); i++) {
      pixels.setPixelColor(i, c);
    }
    pixels.show();
  }
}

// connect to wifi
bool wifiConnect() {
  bool blinkState = true;
  wifiConnectionRetries += 1;
  int retryCounter = CONNECT_TIMEOUT * 1000;
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA);  //  Force the ESP into client-only mode
  delay(1);
  DEBUG_PRINT("My Mac: ");
  DEBUG_PRINTLN(WiFi.macAddress());
  DEBUG_PRINT("Reconnecting to Wifi ");
  DEBUG_PRINT(wifiConnectionRetries);
  DEBUG_PRINT("/20 ");
  while (WiFi.status() != WL_CONNECTED) {
    retryCounter--;
    if (retryCounter <= 0) {
      DEBUG_PRINTLN(" timeout reached!");
      if (wifiConnectionRetries > 19) {
        // set warning color since we are rebooting (white)
        colorWipe(pixels.Color(30, 30, 30), 0);

        DEBUG_PRINTLN(
            "Wifi connection not sucessful after 20 tries. Resetting ESP8266!");
        ESP.restart();
      }
      return false;
    }
    delay(1);
    if (retryCounter % 500 == 0) {
#ifdef BEEPER
      buzzerCheck();
#endif
      DEBUG_PRINT(".");
      if (blinkState) {
        blinkState = false;
        // set warning color since we are not connected to mqtt (red high)
        colorWipe(pixels.Color(50, 0, 0), 0);
      } else {
        blinkState = true;
        // set warning color since we are not connected to mqtt (red low)
        colorWipe(pixels.Color(20, 0, 0), 0);
      }
    }
  }
  DEBUG_PRINT(" done, got IP: ");
  DEBUG_PRINTLN(WiFi.localIP().toString());
  wifiConnectionRetries = 0;
#ifdef BEEPER
  buzz(100);
#endif
  return true;
}

// helper functions
// Thanks to https://gist.github.com/mattfelsen/9467420
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length();

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

// neopixel color fade loop
bool fade(uint8_t fadeCurrentColor[], uint8_t fadeStartColor[],
          uint32_t fadeDuration, uint16_t redEnd, uint16_t greenEnd,
          uint16_t blueEnd) {
  if (currentFadeStart < 1) {
    // new fade loop. calculating and setting required things
    fadeCount++;
    currentFadeStart = millis();
    currentFadeEnd = currentFadeStart + fadeDuration;
    fadeStartColor[0] = fadeCurrentColor[0];
    fadeStartColor[1] = fadeCurrentColor[1];
    fadeStartColor[2] = fadeCurrentColor[2];
    DEBUG_PRINT((String) "Fade " + fadeCount + " will take " +
                (currentFadeEnd - currentFadeStart) + " millis ");
    DEBUG_PRINTLN((String) "from " + fadeStartColor[0] + ", " +
                  fadeStartColor[1] + ", " + fadeStartColor[2] + " to " +
                  redEnd + ", " + greenEnd + ", " + blueEnd);
  }

  unsigned long now = millis();
  fadeCurrentColor[0] =
      map(now, currentFadeStart, currentFadeEnd, fadeStartColor[0], redEnd);
  fadeCurrentColor[1] =
      map(now, currentFadeStart, currentFadeEnd, fadeStartColor[1], greenEnd);
  fadeCurrentColor[2] =
      map(now, currentFadeStart, currentFadeEnd, fadeStartColor[2], blueEnd);

  colorWipe(pixels.Color(fadeCurrentColor[0], fadeCurrentColor[1],
                         fadeCurrentColor[2]),
            0);

  if (millis() >= currentFadeEnd) {
// current fade finished
#ifdef DEBUG
    unsigned long endTime = millis();
    unsigned long fadeDuration = (endTime - currentFadeStart) / 1000;
    DEBUG_PRINTLN((String) "Fade " + fadeCount + " ended after " +
                  fadeDuration + " seconds.");
#endif
    currentFadeStart = 0;
    return true;
  } else {
    // current fade not yet finished
    return false;
  }
}

// sunrise
void sunrise() {
  if (!doSunrise) {
    // no sunrise happening!
    sunriseStartTime = 0;
    return;
  }
  if (sunriseStartTime < 1) {
    DEBUG_PRINTLN((String) "Sunrise starting");
    sunriseStartTime = millis();
    sunriseLoopStep = 0;
    currentFadeStart = 0;
    fadeCount = 0;
    colorWipe(pixels.Color(0, 0, 0), 0);
    currentColor[0] = 0;
    currentColor[1] = 0;
    currentColor[2] = 0;
  }

  int sunriseData[][4] = {{50 * 1000, 0, 0, 5},      {50 * 1000, 0, 20, 52},
                          {50 * 1000, 25, 20, 60},   {50 * 1000, 207, 87, 39},
                          {50 * 1000, 220, 162, 16}, {50 * 1000, 255, 165, 0},
                          {50 * 1000, 255, 255, 30}};

  if (fade(currentColor, startColor, sunriseData[sunriseLoopStep][0],
           sunriseData[sunriseLoopStep][1], sunriseData[sunriseLoopStep][2],
           sunriseData[sunriseLoopStep][3])) {
    sunriseLoopStep++;
  }

  int fadeSteps = sizeof(sunriseData) / sizeof(int) / 4;
  if (sunriseLoopStep >= fadeSteps) {
// reset all variables
#ifdef DEBUG
    unsigned long duration = (millis() - sunriseStartTime) / 1000;
    DEBUG_PRINTLN((String) "Sunrise ended after " + duration + " seconds.");
#endif
    sunriseLoopStep = 0;
    doSunrise = false;

    // set default effect
    doFire = true;
  }
}

// fire
void fire() {
  if (!doFire) {
    // no fire happening!
    nextFireLoop = 0;
    return;
  }

  if (millis() < nextFireLoop) return;

  for (uint16_t i = 0; i < pixels.numPixels(); i++) {
    uint8_t flicker = random(0, 100);
    int16_t r1 = 256 - flicker;
    int16_t g1 = 120 - flicker;
    int16_t b1 = 30 - flicker;
    if (r1 < 0) r1 = 0;
    if (g1 < 0) g1 = 0;
    if (b1 < 0) b1 = 0;
    pixels.setPixelColor(i, pixels.Color(r1, g1, b1));
  }
  pixels.show();

  nextFireLoop = millis() + random(50, 150);
}

// flash a color
void flash() {
  if (!doFlash) {
    // no flash happening!
    flashStartTime = 0;
    return;
  }
  if (flashStartTime < 1) {
    DEBUG_PRINTLN((String) "Flash starting");
    flashStartTime = millis();
    flashLoopStep = 0;
    currentFadeStart = 0;
    fadeCount = 0;
    colorWipe(pixels.Color(0, 0, 0), 0);
    flashCurrentColor[0] = 0;
    flashCurrentColor[1] = 0;
    flashCurrentColor[2] = 0;
  }

  int flashData[][4] = {{100, flashColor[0], flashColor[1], flashColor[2]},
                        {100, flashColor[0], flashColor[1], flashColor[2]},
                        {400, 0, 0, 0}};

  if (fade(flashCurrentColor, startColor, flashData[flashLoopStep][0],
           flashData[flashLoopStep][1], flashData[flashLoopStep][2],
           flashData[flashLoopStep][3])) {
    flashLoopStep++;
  }

  int fadeSteps = sizeof(flashData) / sizeof(int) / 4;
  if (flashLoopStep >= fadeSteps) {
// reset all variables
#ifdef DEBUG
    unsigned long duration = (millis() - flashStartTime) / 1000;
    DEBUG_PRINTLN((String) "Flash ended after " + duration + " seconds.");
#endif
    colorWipe(pixels.Color(0, 0, 0), 0);
    flashLoopStep = 0;
    doFlash = false;

    runDefault();
  }
}

// RGB Cycle loop
void rgbCycle() {
  if (!doRgbRun && !doRgbCycle) return;

  diffRgbLoop = millis() - lastRgbLoop;

  if (diffRgbLoop >= rgbCycleDelay) {
    lastRgbLoop = millis();
    DEBUG_PRINT("delay: ");
    DEBUG_PRINT(rgbCycleDelay);
    if (rgbCycleDelay > 0) {
      rgbCycleStep += int(diffRgbLoop / rgbCycleDelay);
    } else {
      DEBUG_PRINTLN("Devision by zero avoided!");
    }
  } else {
    return;
  }

  DEBUG_PRINT("\tdiff: ");
  DEBUG_PRINT(diffRgbLoop);
  DEBUG_PRINT("\tstep: ");
  DEBUG_PRINT(rgbCycleStep);

  // check if brightness is to high. if so, reset and iterate to next color
  // pair.
  if (rgbCycleStep >= rgbCycleMaxBrightness) {
    rgbCycleStep = rgbCycleStep - rgbCycleMaxBrightness;
    if (rgbCycleDecColour == 2) {
      rgbCycleDecColour = 0;
    } else {
      rgbCycleDecColour += 1;
    }
  }

  rgbCycleIncColour = rgbCycleDecColour == 2 ? 0 : rgbCycleDecColour + 1;
  rgbCurrentColor[rgbCycleDecColour] = rgbCycleMaxBrightness - rgbCycleStep;
  rgbCurrentColor[rgbCycleIncColour] = rgbCycleStep;

  DEBUG_PRINT("\tr: ");
  DEBUG_PRINT(rgbCurrentColor[0]);
  DEBUG_PRINT("\tg: ");
  DEBUG_PRINT(rgbCurrentColor[1]);
  DEBUG_PRINT("\tb: ");
  DEBUG_PRINTLN(rgbCurrentColor[2]);
}

// running led
void run() {
  if (!doRun && !doRgbRun) return;
  if (millis() < nextRunLoop) return;

  if (doRgbRun) {
    activeRunColor[0] = rgbCurrentColor[0];
    activeRunColor[1] = rgbCurrentColor[1];
    activeRunColor[2] = rgbCurrentColor[2];
  }

  for (int i = 0; i < NUMPIXELS; i++) {
    if (((i + runIndex) % runLeds) == 0) {
      pixels.setPixelColor(i, pixels.Color(activeRunColor[0], activeRunColor[1],
                                           activeRunColor[2]));
    } else {
      pixels.setPixelColor(
          i, pixels.Color(inactiveRunColor[0], inactiveRunColor[1],
                          inactiveRunColor[2]));
    }
  }
  pixels.show();

  if (runDirection) {
    runIndex++;
  } else {
    runIndex--;
  }

  if (runIndex == 0) {
    if (runDirection) {
      runIndex++;
    } else {
      runIndex--;
    }
  }

  nextRunLoop = millis() + runDelay;
}

// RGB Cycle effect
void cycle() {
  if (!doRgbCycle) return;

  colorWipe(
      pixels.Color(rgbCurrentColor[0], rgbCurrentColor[1], rgbCurrentColor[2]),
      0);
}

void twinkle() {
  if (!doTwinkle) return;
  unsigned long now = millis();
  if (twinkleLastLoop >= now) return;
  twinkleLastLoop = now;
  bool changesMade = false;

  if (nextTwinkleStart <= now) {
    if (sizeof(twinkleLedIndex[0]) == 0) {
      DEBUG_PRINTLN("Devision by zero avoided");
    } else {
      for (byte i = 0;
           i < (sizeof(twinkleLedIndex) / sizeof(twinkleLedIndex[0])); i++) {
        if (twinkleLedIndex[i] == -1) {
          DEBUG_PRINT(i);
          DEBUG_PRINT(" twinkle started ");

          twinkleLedIndex[i] = random(0, NUMPIXELS);
          DEBUG_PRINT("for led ");
          DEBUG_PRINT(twinkleLedIndex[i]);

          twinkleLedDuraion[i] = random(twinkleMinDuration, twinkleMaxDuration);
          DEBUG_PRINT(" with duration ");
          DEBUG_PRINTLN(twinkleLedDuraion[i]);

          twinkleLedStart[i] = now;

          nextTwinkleStart = now + random(twinkleMinDelay, twinkleMaxDelay);
          break;
        }
      }
    }
  }

  if (sizeof(twinkleLedIndex[0]) == 0) {
    DEBUG_PRINTLN("Devision by zero avoided!");
  } else {
    for (byte i = 0; i < (sizeof(twinkleLedIndex) / sizeof(twinkleLedIndex[0]));
         i++) {
      if (twinkleLedIndex[i] != -1) {
        if (now >= (twinkleLedStart[i] + twinkleLedDuraion[i])) {
          DEBUG_PRINT(i);
          DEBUG_PRINTLN(" twinkle finished, reset index");
          // twinkle finished, reset index
          twinkleLedIndex[i] = -1;
          // set default bg colors for finished leds
          DEBUG_PRINTLN("set off color");
          pixels.setPixelColor(
              i, pixels.Color(twinkleBgColor[0], twinkleBgColor[1],
                              twinkleBgColor[2]));
          changesMade = true;
        } else {
          // loop over all active twinkles and adjust colors

          if (millis() < (twinkleLedStart[i] + (twinkleLedDuraion[i] / 2))) {
            DEBUG_PRINTLN("first fade ");
            DEBUG_PRINTLN(i);
            // first fade
            twinkleTmpStartTime = twinkleLedStart[i];
            twinkleTmpEndTime = twinkleLedStart[i] + (twinkleLedDuraion[i] / 2);
            twinkleTmpColor[0] =
                map(now, twinkleTmpStartTime, twinkleTmpEndTime,
                    twinkleBgColor[0], twinkleColor[0]);
            twinkleTmpColor[1] =
                map(now, twinkleTmpStartTime, twinkleTmpEndTime,
                    twinkleBgColor[1], twinkleColor[1]);
            twinkleTmpColor[2] =
                map(now, twinkleTmpStartTime, twinkleTmpEndTime,
                    twinkleBgColor[2], twinkleColor[2]);
          } else {
            DEBUG_PRINT("second fade ");
            DEBUG_PRINTLN(i);
            // second fade
            twinkleTmpStartTime =
                twinkleLedStart[i] + (twinkleLedDuraion[i] / 2);
            twinkleTmpEndTime = twinkleLedStart[i] + twinkleLedDuraion[i];
            twinkleTmpColor[0] =
                map(now, twinkleTmpStartTime, twinkleTmpEndTime,
                    twinkleColor[0], twinkleBgColor[0]);
            twinkleTmpColor[1] =
                map(now, twinkleTmpStartTime, twinkleTmpEndTime,
                    twinkleColor[1], twinkleBgColor[1]);
            twinkleTmpColor[2] =
                map(now, twinkleTmpStartTime, twinkleTmpEndTime,
                    twinkleColor[2], twinkleBgColor[2]);
          }
          DEBUG_PRINTLN("set fade color");
          pixels.setPixelColor(
              twinkleLedIndex[i],
              pixels.Color(twinkleTmpColor[0], twinkleTmpColor[1],
                           twinkleTmpColor[2]));
          changesMade = true;
        }
      }
    }
  }

  if (changesMade) {
    DEBUG_PRINTLN("showing");
    pixels.show();
    delay(1);  // testing without debug ...
  }
}

// logic
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  payload[length] = '\0';
  unsigned int numOfOptions = 0;
  DEBUG_PRINT("Message arrived: Topic [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] | Data [");
  for (unsigned int i = 0; i < length; i++) {
    DEBUG_PRINT((char)payload[i]);
    if ((char)payload[i] == ';') {
      numOfOptions++;
    }
  }
  DEBUG_PRINT("] - Found ");
  DEBUG_PRINT(numOfOptions);
  DEBUG_PRINTLN(" options.");

  // setting lastMsg to push the next publish cycle into the future
  lastMsg = millis();

  if ((char)payload[0] == '0') {
    DEBUG_PRINTLN("Disabling everything");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;
    colorWipe(pixels.Color(0, 0, 0), 0);
  } else if ((char)payload[0] == '1') {
    DEBUG_PRINTLN("Enabling sunrise");
    doSunrise = true;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;
  } else if ((char)payload[0] == '2') {
    DEBUG_PRINTLN("Enabling fixed color");
    // options: red;green;blue;wait ms;
    doSunrise = false;
    doFixedColor = true;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;
    s = String((char *)payload);
    // String s = String((char*)payload);
    colorWipe(
        pixels.Color(getValue(s, ';', 1).toInt(), getValue(s, ';', 2).toInt(),
                     getValue(s, ';', 3).toInt()),
        getValue(s, ';', 4).toInt());
  } else if ((char)payload[0] == '3') {
    // not yet implemented
    DEBUG_PRINTLN("Enabling fade to color");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;
  } else if ((char)payload[0] == '4') {
    // not yet implemented
    DEBUG_PRINTLN("Enabling rainbow");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;
  } else if ((char)payload[0] == '5') {
    DEBUG_PRINTLN("Enabling fire");
    doSunrise = false;
    doFixedColor = false;
    doFire = true;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;
  } else if ((char)payload[0] == '6') {
    DEBUG_PRINTLN("Enabling flash");
    // options: red;green;blue;
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = true;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;

    s = String((char *)payload);
    flashColor[0] = getValue(s, ';', 1).toInt();
    flashColor[1] = getValue(s, ';', 2).toInt();
    flashColor[2] = getValue(s, ';', 3).toInt();
  } else if ((char)payload[0] == '7') {
    DEBUG_PRINT("Enabling run with active: ");
    // options: num of leds;delay;direction;acrive red;active green;active
    // blue;passive red;passive green;passive blue;
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = true;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;

    s = String((char *)payload);
    runLeds = getValue(s, ';', 1).toInt();
    runDelay = getValue(s, ';', 2).toInt();
    if (getValue(s, ';', 3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    activeRunColor[0] = getValue(s, ';', 4).toInt();
    activeRunColor[1] = getValue(s, ';', 5).toInt();
    activeRunColor[2] = getValue(s, ';', 6).toInt();
    inactiveRunColor[0] = getValue(s, ';', 7).toInt();
    inactiveRunColor[1] = getValue(s, ';', 8).toInt();
    inactiveRunColor[2] = getValue(s, ';', 9).toInt();

    DEBUG_PRINT(activeRunColor[0]);
    DEBUG_PRINT(" ");
    DEBUG_PRINT(activeRunColor[1]);
    DEBUG_PRINT(" ");
    DEBUG_PRINT(activeRunColor[2]);
    DEBUG_PRINT("  inactive ");
    DEBUG_PRINT(inactiveRunColor[0]);
    DEBUG_PRINT(" ");
    DEBUG_PRINT(inactiveRunColor[1]);
    DEBUG_PRINT(" ");
    DEBUG_PRINT(inactiveRunColor[2]);
    DEBUG_PRINTLN();
  } else if ((char)payload[0] == '8') {
    DEBUG_PRINT("Enabling fixed LED color");
    // options: red;green;blue;LED index;LED index;LED index;...
    doSunrise = false;
    doFixedColor = true;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = false;

    s = String((char *)payload);

    // add support for multiple leds
    uint32_t color =
        pixels.Color(getValue(s, ';', 1).toInt(), getValue(s, ';', 2).toInt(),
                     getValue(s, ';', 3).toInt());
    DEBUG_PRINT(" color: r");
    DEBUG_PRINT(getValue(s, ';', 1).toInt());
    DEBUG_PRINT(" g");
    DEBUG_PRINT(getValue(s, ';', 2).toInt());
    DEBUG_PRINT(" b");
    DEBUG_PRINT(getValue(s, ';', 3).toInt());
    DEBUG_PRINT(" indexes: ");

    for (unsigned int i = 0; i <= (numOfOptions - 5); i++) {
      DEBUG_PRINT(getValue(s, ';', i + 4).toInt());
      DEBUG_PRINT(", ");
      pixels.setPixelColor(getValue(s, ';', i + 4).toInt(), color);
    }
    pixels.show();

    DEBUG_PRINTLN();
  } else if ((char)payload[0] == '9') {
    // max brightness;loop delay
    DEBUG_PRINT("Enabling RGB Cycling with reset");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = true;
    doTwinkle = false;

    rgbCurrentColor[0] = 0;
    rgbCurrentColor[1] = 0;
    rgbCurrentColor[2] = 0;

    // reset vars so the cycle always starts in sync
    rgbCycleDecColour = 0;
    rgbCycleStep = 0;
    lastRgbLoop = millis();

    s = String((char *)payload);
    rgbCycleMaxBrightness = getValue(s, ';', 1).toInt();
    rgbCycleDelay = getValue(s, ';', 2).toInt();
  } else if ((char)payload[0] == 'a') {
    // max brightness;loop delay
    DEBUG_PRINT("Enabling RGB Cycling");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = true;
    doTwinkle = false;

    s = String((char *)payload);
    rgbCycleMaxBrightness = getValue(s, ';', 1).toInt();
    rgbCycleDelay = getValue(s, ';', 2).toInt();
    lastRgbLoop = millis();
  } else if ((char)payload[0] == 'b') {
    // num of leds;run loop dely;direction;max brightness;rgb cycle delay
    DEBUG_PRINT("Enabling RGB run reset");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = true;
    doRgbCycle = false;
    doTwinkle = false;

    s = String((char *)payload);
    runLeds = getValue(s, ';', 1).toInt();
    runDelay = getValue(s, ';', 2).toInt();
    if (getValue(s, ';', 3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    rgbCycleMaxBrightness = getValue(s, ';', 4).toInt();
    rgbCycleDelay = getValue(s, ';', 5).toInt();

    // reset vars so the cycle always starts in sync
    rgbCycleDecColour = 0;
    rgbCycleStep = 0;
    lastRgbLoop = millis();

    activeRunColor[0] = 0;
    activeRunColor[1] = 0;
    activeRunColor[2] = 0;

    inactiveRunColor[0] = 0;
    inactiveRunColor[1] = 0;
    inactiveRunColor[2] = 0;

    rgbCurrentColor[0] = 0;
    rgbCurrentColor[1] = 0;
    rgbCurrentColor[2] = 0;
  } else if ((char)payload[0] == 'c') {
    // num of leds;run loop dely;direction;max brightness;rgb cycle delay
    DEBUG_PRINT("Enabling RGB run");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = true;
    doRgbCycle = false;
    doTwinkle = false;

    s = String((char *)payload);
    runLeds = getValue(s, ';', 1).toInt();
    runDelay = getValue(s, ';', 2).toInt();
    if (getValue(s, ';', 3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    rgbCycleMaxBrightness = getValue(s, ';', 4).toInt();
    rgbCycleDelay = getValue(s, ';', 5).toInt();
    lastRgbLoop = millis();
  } else if ((char)payload[0] == 'd') {
    // background red;background green;background blue;twinkle red;twinkle
    // green;twinkle blue;twinkle min delay;twinkle max delay;twinkle min
    // duration;twinkle max duration
    DEBUG_PRINTLN("Enabling Twinkle");
    doSunrise = false;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
    doRgbRun = false;
    doRgbCycle = false;
    doTwinkle = true;

    s = String((char *)payload);
    // String s = String((char*)payload);
    twinkleBgColor[0] = getValue(s, ';', 1).toInt();
    twinkleBgColor[1] = getValue(s, ';', 2).toInt();
    twinkleBgColor[2] = getValue(s, ';', 3).toInt();

    twinkleColor[0] = getValue(s, ';', 4).toInt();
    twinkleColor[1] = getValue(s, ';', 5).toInt();
    twinkleColor[2] = getValue(s, ';', 6).toInt();

    twinkleMinDelay = getValue(s, ';', 7).toInt();
    twinkleMaxDelay = getValue(s, ';', 8).toInt();
    twinkleMinDuration = getValue(s, ';', 9).toInt();
    twinkleMaxDuration = getValue(s, ';', 10).toInt();

    nextTwinkleStart = 0;

    colorWipe(
        pixels.Color(twinkleBgColor[0], twinkleBgColor[1], twinkleBgColor[2]),
        0);

    // reset all twinkle states
    for (byte i = 0; i < (sizeof(twinkleLedIndex) / sizeof(twinkleLedIndex[0]));
         i++)
      twinkleLedIndex[i] = -1;
  } else if ((char)payload[0] == 'Y') {
    DEBUG_PRINTLN("Running default effect");
    runDefault();
  } else if ((char)payload[0] == 'Z') {
    DEBUG_PRINT("Saving ");
    for (unsigned int i = 0; i < length - 2; i++) {
      DEBUG_PRINT((char)payload[i + 2]);
      defaultPayload[i] = payload[i + 2];
    }
    DEBUG_PRINTLN(" as default effect");
    defaulLength = length;
    defaultSaved = true;
  } else {
    DEBUG_PRINTLN("Unknown RGB command");
  }

  // return false;
}

// default effect
void runDefault() {
  if (defaultSaved) {
    DEBUG_PRINTLN("Running default effect");
    mqttCallback((char *)"Default", defaultPayload, defaulLength);
  } else {
    DEBUG_PRINTLN("No default was saved");
  }
}

void setup() {
#ifdef DEBUG
  Serial.begin(SERIAL_BAUD);  // initialize serial connection
  // delay for the serial monitor to start
  delay(3000);
#endif

  // disable software watchdog for testing
  // ESP.wdtDisable();

  // setup NeoPixel
  DEBUG_PRINTLN("Initializing LEDs");
  pixels.begin();
  pixels.setBrightness(255);

  // show system startup (violett)
  colorWipe(pixels.Color(18, 0, 32), 0);

  // Start the Pub/Sub client
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  mqttClient.setCallback(mqttCallback);

  // initial delay to let millis not be 0
  delay(1);

  // initial wifi connect
  wifiConnect();

// Set buzzer pin to output
#ifdef BEEPER
  pinMode(BUZZ_PIN, OUTPUT);
  buzz(100);
#endif
}

void loop() {
#ifdef BEEPER
  buzzerCheck();
#endif

  // Check if the wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    // set warning color since we are not connected to wifi (red)
    colorWipe(pixels.Color(50, 00, 0), 0);

    DEBUG_PRINTLN("Calling wifiConnect() as it seems to be required");
    wifiConnect();
    DEBUG_PRINTLN("My MAC: " + String(WiFi.macAddress()));
  }

  if ((WiFi.status() == WL_CONNECTED) && (!mqttClient.connected())) {
    // set warning color since we are not connected to mqtt (yellow)
    colorWipe(pixels.Color(25, 25, 0), 0);
    delay(500);

    DEBUG_PRINTLN("MQTT is not connected, let's try to reconnect");
    if (!mqttReconnect()) {
      // This should not happen, but seems to...
      DEBUG_PRINTLN("MQTT was unable to connect! Exiting the upload loop");
      // set warning color since we can not connect to mqtt
      colorWipe(pixels.Color(5, 40, 35), 0);
      delay(500);
      // force reconnect to mqtt
      initialPublish = false;
    } else {
      // readyToUpload = true;
      DEBUG_PRINTLN("MQTT successfully reconnected");
    }
  }

  if ((WiFi.status() == WL_CONNECTED) && (!initialPublish)) {
    DEBUG_PRINT("MQTT discovery publish loop:");

    String clientMac = WiFi.macAddress();  // 17 chars
    char topic[37] = "/d1ws2812/discovery/";
    strcat(topic, clientMac.c_str());

    if (mqttClient.publish(topic, VERSION, true)) {
      // Publishing values successful, removing them from cache
      DEBUG_PRINTLN(" successful");

      initialPublish = true;

      // show system startup success by flashing green
      mqttCallback((char *)"Startup", (byte *)"6;0;60;0", 8);
    } else {
      DEBUG_PRINTLN(" FAILED!");
    }
  }

// read voltage if required
#ifdef READVOLTAGE
  if (millis() >= nextVoltageLoop) {
    float volt = readVoltage();

    char voltChar[5];
    dtostrf(volt, 5, 3, voltChar);

    String clientMac = WiFi.macAddress();  // 17 chars
    char topic[37] = "/d1ws2812/voltage/";
    strcat(topic, clientMac.c_str());

    DEBUG_PRINT("Voltage: ");
    DEBUG_PRINTLN(volt);

    if (volt == 0.0) {
      DEBUG_PRINTLN("No voltage could be read");
    } else if (lastVolt == volt) {
      DEBUG_PRINTLN("Voltage did not change");
    } else {
      lastVolt = volt;
      if (cells == -1) {
        cells = (volt / 3.5);
        DEBUG_PRINT("Calculated cells: ");
      } else {
        DEBUG_PRINT("Cells calculated at start: ");
      }
      DEBUG_PRINTLN(cells);

      if (cells > 0) {
        float cellVoltage = (volt / cells);
        DEBUG_PRINT("Calculated cell voltage: ");
        DEBUG_PRINTLN(cellVoltage);

#ifdef BEEPER
        if (cellVoltage < 3.6) {
          buzz(5000);
        }
#endif
      }

      if (initialPublish) {
        mqttClient.publish(topic, voltChar, true);
      }
    }

    nextVoltageLoop = millis() + 60000;
  }
#endif

  // feeding the watchdog to be sure
  ESP.wdtFeed();

  // Call RGB Strip functions
  rgbCycle();
  sunrise();
  fire();
  flash();
  run();
  cycle();
  twinkle();

  // feeding the watchdog to be sure
  ESP.wdtFeed();

  // calling loop at the end as proposed
  mqttClient.loop();
}
