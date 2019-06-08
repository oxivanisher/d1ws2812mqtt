#include "Adafruit_NeoPixel.h"
#include "ESP8266WiFi.h"
#include <PubSubClient.h>

// Read settingd from config.h
#include "config.h"

#ifdef DEBUG
  #define DEBUG_PRINT(x) Serial.print (x)
  #define DEBUG_PRINTLN(x) Serial.println (x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// Initialize Adafruit_NeoPixel
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espClient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure espClient;

// Initialize MQTT
PubSubClient mqttClient(espClient);
// used for splitting arguments to effects
String s = String();

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

unsigned long nextVoltageLoop = 0;
float readVoltage() {
	int sensor = analogRead(VOLT_PIN);
	float voltage = sensor * 3.3 / 1024;
}

unsigned long beepOff = 0;
void beepCheck() {
  if (millis() > beepOff) {
    noTone(BUZZ_PIN);     // Stop sound...
  }
}
void beep(int freq, int duration) {
  beepOff = millis() + duration;
  tone(BUZZ_PIN, freq);
}

void runDefault();

bool mqttReconnect() {
  // Create a client ID based on the MAC address
  String clientId = String("D1WS2812") + "-";
  clientId += String(WiFi.macAddress());

  // Loop until we're reconnected
  int counter = 0;
  while (!mqttClient.connected()) {
    counter++;
    if (counter > 5) {
      DEBUG_PRINTLN("Exiting MQTT reconnect loop");
      return false;
    }

    DEBUG_PRINT("Attempting MQTT connection...");

    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      DEBUG_PRINTLN("connected");

      // subscribe to "all" topic
      mqttClient.subscribe("/d1ws2812/all", 1);

      // subscript to the mac address (private) topic
      char topic[27];
      strcat(topic, "/d1ws2812/");
      String clientMac = WiFi.macAddress();
      strcat(topic, clientMac.c_str());
      mqttClient.subscribe(topic, 1);

      return true;
    } else {
      DEBUG_PRINT("failed, rc=");
      DEBUG_PRINT(mqttClient.state());
      DEBUG_PRINTLN(" try again in 2 seconds");
      // Wait 2 seconds before retrying
      delay(2000);
    }
  }
  return false;
}

bool wifiConnect() {
  int retryCounter = CONNECT_TIMEOUT * 10;
  WiFi.forceSleepWake();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA); //  Force the ESP into client-only mode
  delay(100);
  DEBUG_PRINT("Reconnecting to Wifi ");
  while (WiFi.status() != WL_CONNECTED) {
    retryCounter--;
    if (retryCounter <= 0) {
      DEBUG_PRINTLN(" timeout reached!");
      return false;
    }
    delay(100);
    DEBUG_PRINT(".");
  }
  DEBUG_PRINTLN(" done");
  return true;
}

// helper functions
// Thanks to https://gist.github.com/mattfelsen/9467420
String getValue(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length();

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
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

// neopixel color fade loop
bool fade(uint8_t fadeCurrentColor[], uint8_t fadeStartColor[], uint32_t fadeDuration, uint16_t redEnd, uint16_t greenEnd, uint16_t blueEnd) {
  if (currentFadeStart < 1) {
    // new fade loop. calculating and setting required things
    fadeCount++;
    currentFadeStart = millis();
    currentFadeEnd = currentFadeStart + fadeDuration;
    fadeStartColor[0] = fadeCurrentColor[0];
    fadeStartColor[1] = fadeCurrentColor[1];
    fadeStartColor[2] = fadeCurrentColor[2];
    DEBUG_PRINT((String)"Fade " + fadeCount + " will take " + (currentFadeEnd - currentFadeStart) + " millis ");
    DEBUG_PRINTLN((String)"from " + fadeStartColor[0] + ", " + fadeStartColor[1] + ", " + fadeStartColor[2] +" to " + redEnd + ", " + greenEnd + ", " + blueEnd);
  }

  unsigned long now = millis();
  fadeCurrentColor[0] = map(now, currentFadeStart, currentFadeEnd, fadeStartColor[0], redEnd);
  fadeCurrentColor[1] = map(now, currentFadeStart, currentFadeEnd, fadeStartColor[1], greenEnd);
  fadeCurrentColor[2] = map(now, currentFadeStart, currentFadeEnd, fadeStartColor[2], blueEnd);

  colorWipe (pixels.Color(fadeCurrentColor[0], fadeCurrentColor[1], fadeCurrentColor[2]), 0);

  if (millis() >= currentFadeEnd) {
    // current fade finished
    unsigned long endTime = millis();
    unsigned long fadeDuration = (endTime - currentFadeStart) / 1000;
    DEBUG_PRINTLN((String)"Fade " + fadeCount + " ended after " + fadeDuration + " seconds.");
    currentFadeStart = 0;
    return true;
  } else {
    // current fade not yet finished
    return false;
  }
}

// sunrise
void sunrise() {
  if (! doSunrise) {
    // no sunrise happening!
    sunriseStartTime = 0;
    return;
  }
  if (sunriseStartTime < 1) {
    DEBUG_PRINTLN((String)"Sunrise starting");
    sunriseStartTime = millis();
    sunriseLoopStep = 0;
    currentFadeStart = 0;
    fadeCount = 0;
    colorWipe (pixels.Color(0, 0, 0), 0);
    currentColor[0] = 0;
    currentColor[1] = 0;
    currentColor[2] = 0;
  }

  int sunriseData[][4] = {
    { 50 * 1000,   0,   0,   5},
    { 50 * 1000,   0,  20,  52},
    { 50 * 1000,  25,  20,  60},
    { 50 * 1000, 207,  87,  39},
    { 50 * 1000, 220, 162,  16},
    { 50 * 1000, 255, 165,   0},
    { 50 * 1000, 255, 255,  30}
  };

  if (fade(currentColor, startColor, sunriseData[sunriseLoopStep][0], sunriseData[sunriseLoopStep][1], sunriseData[sunriseLoopStep][2], sunriseData[sunriseLoopStep][3])) {
    sunriseLoopStep++;
  }

  int fadeSteps = sizeof(sunriseData) / sizeof(int) / 4;
  if (sunriseLoopStep >= fadeSteps) {
    // reset all variables
    unsigned long duration = (millis() - sunriseStartTime) / 1000;
    DEBUG_PRINTLN((String)"Sunrise ended after " + duration + " seconds.");
    sunriseLoopStep = 0;
    doSunrise = false;

    // set default effect
    doFire = true;
  }
}

// fire
void fire() {
  if (! doFire) {
    // no fire happening!
    nextFireLoop = 0;
    return;
  }

  if (millis() < nextFireLoop) {
    return;
  }

  for (uint16_t i = 0; i < pixels.numPixels(); i++) {
    uint8_t flicker = random(0,100);
    int16_t r1 = 256 - flicker;
    int16_t g1 = 120 - flicker;
    int16_t b1 = 30  - flicker;
    if(r1<0) r1=0;
    if(g1<0) g1=0;
    if(b1<0) b1=0;
    pixels.setPixelColor(i, pixels.Color(r1, g1, b1));
  }
  pixels.show();

  nextFireLoop = millis() + random(50,150);
}

// flash a color
void flash() {
  if (! doFlash) {
    // no flash happening!
    flashStartTime = 0;
    return;
  }
  if (flashStartTime < 1) {
    DEBUG_PRINTLN((String)"Flash starting");
    flashStartTime = millis();
    flashLoopStep = 0;
    currentFadeStart = 0;
    fadeCount = 0;
    colorWipe (pixels.Color(0, 0, 0), 0);
    flashCurrentColor[0] = 0;
    flashCurrentColor[1] = 0;
    flashCurrentColor[2] = 0;
  }

  int flashData[][4] = {
    { 100, flashColor[0], flashColor[1], flashColor[2]},
    { 100, flashColor[0], flashColor[1], flashColor[2]},
    { 400, 0, 0, 0}
  };

  if (fade(flashCurrentColor, startColor, flashData[flashLoopStep][0], flashData[flashLoopStep][1], flashData[flashLoopStep][2], flashData[flashLoopStep][3])) {
    flashLoopStep++;
  }

  int fadeSteps = sizeof(flashData) / sizeof(int) / 4;
  if (flashLoopStep >= fadeSteps) {
    // reset all variables
    unsigned long duration = (millis() - flashStartTime) / 1000;
    DEBUG_PRINTLN((String)"Flash ended after " + duration + " seconds.");
    colorWipe (pixels.Color(0, 0, 0), 0);
    flashLoopStep = 0;
    doFlash = false;

    runDefault();
  }
}

// RGB Cycle loop
void rgbCycle() {
  if ( ! doRgbRun && ! doRgbCycle ) { return; }
  diffRgbLoop = millis() - lastRgbLoop;

  if ( diffRgbLoop >= rgbCycleDelay ) {
    lastRgbLoop = millis();
    DEBUG_PRINT("delay: ");
    DEBUG_PRINT(rgbCycleDelay);
    rgbCycleStep += int(diffRgbLoop / rgbCycleDelay);
  } else {
    return;
  }

  DEBUG_PRINT("\tdiff: ");
  DEBUG_PRINT(diffRgbLoop);
  DEBUG_PRINT("\tstep: ");
  DEBUG_PRINT(rgbCycleStep);

  // check if brightness is to high. if so, reset and iterate to next color pair.
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
  if (! doRun && ! doRgbRun ) { return; }
  if (millis() < nextRunLoop) { return; }

  if ( doRgbRun ) {
    activeRunColor[0] = rgbCurrentColor[0];
    activeRunColor[1] = rgbCurrentColor[1];
    activeRunColor[2] = rgbCurrentColor[2];
  }

  for(int i=0;i<NUMPIXELS;i++) {
    if (((i + runIndex) % runLeds) == 0) {
      pixels.setPixelColor(i, pixels.Color(activeRunColor[0],activeRunColor[1],activeRunColor[2]));
    } else {
      pixels.setPixelColor(i, pixels.Color(inactiveRunColor[0],inactiveRunColor[1],inactiveRunColor[2]));
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
  if (! doRgbCycle) { return; }
  colorWipe (pixels.Color(rgbCurrentColor[0], rgbCurrentColor[1], rgbCurrentColor[2]), 0);
}

// logic
void mqttCallback(char* topic, byte* payload, unsigned int length) {
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
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;
    colorWipe (pixels.Color(0, 0, 0), 0);
  } else if ((char)payload[0] == '1') {
    DEBUG_PRINTLN("Enabling sunrise");
    doSunrise    = true;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;
  } else if ((char)payload[0] == '2') {
    DEBUG_PRINTLN("Enabling fixed color");
    // options: red;green;blue;wait ms;
    doSunrise    = false;
    doFixedColor = true;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;
    s = String((char*)payload);
    // String s = String((char*)payload);
    colorWipe (pixels.Color(getValue(s,';',1).toInt(), getValue(s,';',2).toInt(), getValue(s,';',3).toInt()), getValue(s,';',4).toInt());
  } else if ((char)payload[0] == '3') {
    // not yet implemented
    DEBUG_PRINTLN("Enabling fade to color");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;
  } else if ((char)payload[0] == '4') {
    // not yet implemented
    DEBUG_PRINTLN("Enabling rainbow");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;
  } else if ((char)payload[0] == '5') {
    DEBUG_PRINTLN("Enabling fire");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = true;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;
  } else if ((char)payload[0] == '6') {
    DEBUG_PRINTLN("Enabling flash");
    // options: red;green;blue;
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = true;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;

    s = String((char*)payload);
    flashColor[0] = getValue(s,';',1).toInt();
    flashColor[1] = getValue(s,';',2).toInt();
    flashColor[2] = getValue(s,';',3).toInt();
  } else if ((char)payload[0] == '7') {
    DEBUG_PRINT("Enabling run with active: ");
    // options: num of leds;delay;direction;acrive red;active green;active blue;passive red;passive green;passive blue;
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = true;
    doRgbRun     = false;
    doRgbCycle   = false;

    s = String((char*)payload);
    runLeds = getValue(s,';',1).toInt();
    runDelay = getValue(s,';',2).toInt();
    if (getValue(s,';',3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    activeRunColor[0] = getValue(s,';',4).toInt();
    activeRunColor[1] = getValue(s,';',5).toInt();
    activeRunColor[2] = getValue(s,';',6).toInt();
    inactiveRunColor[0] = getValue(s,';',7).toInt();
    inactiveRunColor[1] = getValue(s,';',8).toInt();
    inactiveRunColor[2] = getValue(s,';',9).toInt();

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
    doSunrise    = false;
    doFixedColor = true;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = false;

    s = String((char*)payload);

    // add support for multiple leds
    uint32_t color = pixels.Color(getValue(s,';',1).toInt(), getValue(s,';',2).toInt(), getValue(s,';',3).toInt());
    DEBUG_PRINT(" color: r");
    DEBUG_PRINT(getValue(s,';',1).toInt());
    DEBUG_PRINT(" g");
    DEBUG_PRINT(getValue(s,';',2).toInt());
    DEBUG_PRINT(" b");
    DEBUG_PRINT(getValue(s,';',3).toInt());
    DEBUG_PRINT(" indexes: ");

    for (unsigned int i = 0; i <= (numOfOptions - 5); i++) {
      DEBUG_PRINT(getValue(s,';',i + 4).toInt());
      DEBUG_PRINT(", ");
      pixels.setPixelColor(getValue(s,';',i + 4).toInt(), color);
    }
    pixels.show();

    DEBUG_PRINTLN();
  } else if ((char)payload[0] == '9') {
    //max brightness;loop delay
    DEBUG_PRINT("Enabling RGB Cycling with reset");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = true;

    rgbCurrentColor[0] = 0;
    rgbCurrentColor[1] = 0;
    rgbCurrentColor[2] = 0;

    // reset vars so the cycle always starts in sync
    rgbCycleDecColour = 0;
    rgbCycleStep = 0;
    lastRgbLoop = millis();

    s = String((char*)payload);
    rgbCycleMaxBrightness = getValue(s,';',1).toInt();
    rgbCycleDelay = getValue(s,';',2).toInt();
  } else if ((char)payload[0] == 'a') {
    //max brightness;loop delay
    DEBUG_PRINT("Enabling RGB Cycling");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = false;
    doRgbCycle   = true;

    s = String((char*)payload);
    rgbCycleMaxBrightness = getValue(s,';',1).toInt();
    rgbCycleDelay = getValue(s,';',2).toInt();
    lastRgbLoop = millis();

  } else if ((char)payload[0] == 'b') {
    //num of leds;run loop dely;direction;max brightness;rgb cycle delay
    DEBUG_PRINT("Enabling RGB run reset");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = true;
    doRgbCycle   = false;

    s = String((char*)payload);
    runLeds = getValue(s,';',1).toInt();
    runDelay = getValue(s,';',2).toInt();
    if (getValue(s,';',3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    rgbCycleMaxBrightness = getValue(s,';',4).toInt();
    rgbCycleDelay = getValue(s,';',5).toInt();

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
    //num of leds;run loop dely;direction;max brightness;rgb cycle delay
    DEBUG_PRINT("Enabling RGB run");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    doRgbRun     = true;
    doRgbCycle   = false;

    s = String((char*)payload);
    runLeds = getValue(s,';',1).toInt();
    runDelay = getValue(s,';',2).toInt();
    if (getValue(s,';',3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    rgbCycleMaxBrightness = getValue(s,';',4).toInt();
    rgbCycleDelay = getValue(s,';',5).toInt();
    lastRgbLoop = millis();

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

  //return false;
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
  Serial.begin(SERIAL_BAUD); // initialize serial connection
  // delay for the serial monitor to start
  delay(3000);
  #endif

  // setup NeoPixel
  DEBUG_PRINTLN("Initializing LEDs");
  pixels.begin();
  pixels.setBrightness(255);

  // Set buzzer pin to output
  pinMode(BUZZ_PIN, OUTPUT);

  // show system startup
  colorWipe (pixels.Color(20, 0, 0), 0);

  // Start the Pub/Sub client
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  mqttClient.setCallback(mqttCallback);

  // initial delay to let millis not be 0
  delay(1);
}

void loop() {
  // Check if the wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("Calling wifiConnect() as it seems to be required");
    wifiConnect();
  }

  if (!mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT is not connected, let's try to reconnect");
    if (! mqttReconnect()) {
      // This should not happen, but seems to...
      DEBUG_PRINTLN("MQTT was unable to connect! Exiting the upload loop");
      // set warning color since we can not connect to mqtt
      colorWipe (pixels.Color(15, 5, 0), 0);
      // force reconnect to mqtt
      initialPublish = false;
    } else {
      // readyToUpload = true;
      DEBUG_PRINTLN("MQTT successfully reconnected");
    }
  }

  // read voltage if required
  if (millis() >= nextVoltageLoop) {
    float volt = readVoltage();

    char voltChar[10];
    sprintf(voltChar, "%d", volt);

    String clientMac = WiFi.macAddress(); // 17 chars
    char topic[37] = "/d1ws2812/voltage/";
    strcat(topic, clientMac.c_str());

    DEBUG_PRINT("Voltage read: ");
    DEBUG_PRINTLN(voltChar);


    if (volt == 0.0) {
      DEBUG_PRINTLN("No voltage could be read");
    } else if (volt == 0.0) {
      unsigned cells = (volt / 3.2);
      float cellVoltage = (volt / cells);
      DEBUG_PRINT("Calculated cells: ");
      DEBUG_PRINTLN(cells);
      DEBUG_PRINT("Calculated cell voltage: ");
      DEBUG_PRINTLN(cellVoltage);

      if (cellVoltage < 3.6) {
        beep(2000, 500);
      }
    }

    if (mqttClient.publish(topic, voltChar, true)) {
      nextVoltageLoop = millis() + 30000;
    }
  }

  // mqtt loop
  mqttClient.loop();

  if (initialPublish == false) {
    DEBUG_PRINT("MQTT discovery publish loop:");

    String clientMac = WiFi.macAddress(); // 17 chars
    char topic[37] = "/d1ws2812/discovery/";
    strcat(topic, clientMac.c_str());

    if (mqttClient.publish(topic, VERSION, true)) {
      // Publishing values successful, removing them from cache
      DEBUG_PRINTLN(" successful");

      initialPublish = true;

      // show system startup
      colorWipe (pixels.Color(0, 20, 0), 0);
      beep(1500, 250);

    } else {
      DEBUG_PRINTLN(" FAILED!");
    }
  }

  // Call RGB Strip functions
  rgbCycle();
  sunrise();
  fire();
  flash();
  run();
  cycle();

  // Beep check called
  beepCheck();
}
