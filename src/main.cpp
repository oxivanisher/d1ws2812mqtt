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

#define PUBLISH_LOOP_SLEEP 10000

// Initialize Adafruit_NeoPixel
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espClient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure espClient;

// Initialize MQTT
PubSubClient mqttClient(espClient);


// Logic switches
bool readyToUpload = false;
long lastMsg = 0;


// RBG function vars
// general fade loop vars
unsigned long currentFadeStart = 0;
unsigned long currentFadeEnd = 0;
uint8_t startColor[] = {0, 0, 0};
uint8_t currentColor[] = {0, 0, 0};
uint8_t targetColor[] = {0, 0, 0};
uint8_t fadeCount = 0;

// sunrise loop vars
bool doSunrise = false;
unsigned long sunriseStartTime = 0;
int sunriseLoopStep = 0;

// fixed color vars
bool doFixedColor = false;

// fire
bool doFire = true;
unsigned long nextFireLoop = 0;

// run
bool doRun = false;
uint8_t runLeds = 0;
uint16_t runDelay = 0;
bool runDirection = false;
unsigned long nextRunLoop = 0;
uint8_t runIndex = 0;

// flash
bool doFlash = false;
unsigned long flashStartTime = 0;
uint16_t flashLoopStep = 0;

bool mqttReconnect() {
  // Loop until we're reconnected
  int counter = 0;
  while (!mqttClient.connected()) {
    counter++;
    if (counter > 5) {
      DEBUG_PRINTLN("Exiting MQTT reconnect loop");
      return false;
    }

    DEBUG_PRINT("Attempting MQTT connection...");

    // Create a random client ID
    String clientId = String("D1WS2812") + "-";
    clientId += String(random(0xffff), HEX);

    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      DEBUG_PRINTLN("connected");
      // Once connected, publish an announcement...
      //mqttClient.publish("outTopic", "hello world");
      // ... and resubscribe

      // subscribe to "all" topic
      mqttClient.subscribe("/d1ws2812/all", 1);

      // subscript to the mac address (private) topic
      char topic[27];
      strcat(topic, "/d1ws2812/");
      String clientMac = WiFi.macAddress();
      strcat(topic, clientMac.c_str());
      mqttClient.subscribe(topic, 1);
      //mqttClient.subscribe("/d1ws2812/mac", 1);

      return true;
    } else {
      DEBUG_PRINT("failed, rc=");
      DEBUG_PRINT(mqttClient.state());
      DEBUG_PRINTLN(" try again in 2 seconds");
      // Wait 2 seconds before retrying
      delay(2000);
    }
  }
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
  for (uint16_t i = 0; i < pixels.numPixels(); i++) {
    delay(wait);
    pixels.setPixelColor(i, c);
    pixels.show();
  }
}

// neopixel color fade loop
bool fade(uint8_t currentColor[], uint8_t startColor[], uint32_t fadeDuration, uint16_t redEnd, uint16_t greenEnd, uint16_t blueEnd) {
  if (currentFadeStart < 1) {
    // new fade loop. calculating and setting required things
    fadeCount++;
    currentFadeStart = millis();
    currentFadeEnd = currentFadeStart + fadeDuration;
    startColor[0] = currentColor[0];
    startColor[1] = currentColor[1];
    startColor[2] = currentColor[2];
    Serial.print((String)"Fade " + fadeCount + " will take " + (currentFadeEnd - currentFadeStart) + " millis ");
    DEBUG_PRINTLN((String)"from " + startColor[0] + ", " + startColor[1] + ", " + startColor[2] +" to " + redEnd + ", " + greenEnd + ", " + blueEnd);
  }

  unsigned long now = millis();
  currentColor[0] = map(now, currentFadeStart, currentFadeEnd, startColor[0], redEnd);
  currentColor[1] = map(now, currentFadeStart, currentFadeEnd, startColor[1], greenEnd);
  currentColor[2] = map(now, currentFadeStart, currentFadeEnd, startColor[2], blueEnd);

  colorWipe (pixels.Color(currentColor[0], currentColor[1], currentColor[2]), 0);
  pixels.show();

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
bool sunrise() {
  if (! doSunrise) {
    // no sunrise happening!
    sunriseStartTime = 0;
    return false;
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
bool fire() {
  if (! doFire) {
    // no fire happening!
    nextFireLoop = 0;
    return false;
  }

  if (millis() < nextFireLoop) {
    return false;
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
bool flash() {
  if (! doFlash) {
    // no flash happening!
    flashStartTime = 0;
    return false;
  }
  if (flashStartTime < 1) {
    DEBUG_PRINTLN((String)"Flash starting");
    flashStartTime = millis();
    flashLoopStep = 0;
    currentFadeStart = 0;
    fadeCount = 0;
    colorWipe (pixels.Color(0, 0, 0), 0);
    currentColor[0] = 0;
    currentColor[1] = 0;
    currentColor[2] = 0;
  }

  int flashData[][4] = {
    { 100, targetColor[0], targetColor[1], targetColor[2]},
    { 100, targetColor[0], targetColor[1], targetColor[2]},
    { 400, 0, 0, 0}
  };

  if (fade(currentColor, startColor, flashData[flashLoopStep][0], flashData[flashLoopStep][1], flashData[flashLoopStep][2], flashData[flashLoopStep][3])) {
    flashLoopStep++;
  }

  int fadeSteps = sizeof(flashData) / sizeof(int) / 4;
  if (flashLoopStep >= fadeSteps) {
    // reset all variables
    unsigned long duration = (millis() - flashStartTime) / 1000;
    DEBUG_PRINTLN((String)"Flash ended after " + duration + " seconds.");
    flashLoopStep = 0;
    doFlash = false;

    // set default effect
    doFire = true;
  }
}

// running led
bool run() {
  if (! doRun) {
    return false;
  }

  if (nextRunLoop < millis()) {
    for(int i=0;i<NUMPIXELS;i++) {
      if (((i + runIndex) % runLeds) == 0) {
        pixels.setPixelColor(i, pixels.Color(targetColor[0],targetColor[1],targetColor[2]));
      } else {
        pixels.setPixelColor(i, pixels.Color(startColor[0],startColor[1],startColor[2]));
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

}

// logic
void mqttCallback(char* topic, byte* payload, unsigned int length) {
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

  if ((char)payload[0] == '1') {
    DEBUG_PRINTLN("Enabling sunrise");
    doSunrise = true;
    doFixedColor = false;
    doFire = false;
    doFlash = false;
    doRun = false;
  } else if ((char)payload[0] == '2') {
    DEBUG_PRINTLN("Enabling fixed color");
    // options: red;green;blue;wait ms
    doSunrise    = false;
    doFixedColor = true;
    doFire       = false;
    doFlash      = false;
    doRun        = false;

    String s = String((char*)payload);
    colorWipe (pixels.Color(getValue(s,';',1).toInt(), getValue(s,';',2).toInt(), getValue(s,';',3).toInt()), getValue(s,';',4).toInt());
  } else if ((char)payload[0] == '3') {
    // not yet implemented
    DEBUG_PRINTLN("Enabling fade to color");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
  } else if ((char)payload[0] == '4') {
    // not yet implemented
    DEBUG_PRINTLN("Enabling rainbow");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
  } else if ((char)payload[0] == '5') {
    DEBUG_PRINTLN("Enabling fire");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = true;
    doFlash      = false;
    doRun        = false;
  } else if ((char)payload[0] == '6') {
    DEBUG_PRINTLN("Enabling flash");
    // options: red;green;blue
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = true;
    doRun        = false;

    String s = String((char*)payload);
    targetColor[0] = getValue(s,';',1).toInt();
    targetColor[1] = getValue(s,';',2).toInt();
    targetColor[2] = getValue(s,';',3).toInt();
  } else if ((char)payload[0] == '7') {
    DEBUG_PRINTLN("Enabling run");
    // options: num of leds;delay;direction;acrive red;active green;active blue;passive red;passive green;passive blue
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = true;

    String s = String((char*)payload);
    runLeds = getValue(s,';',1).toInt();
    runDelay = getValue(s,';',2).toInt();
    if (getValue(s,';',3).toInt()) {
      runDirection = true;
    } else {
      runDirection = false;
    }
    targetColor[0] = getValue(s,';',4).toInt();
    targetColor[1] = getValue(s,';',5).toInt();
    targetColor[2] = getValue(s,';',6).toInt();
    startColor[0] = getValue(s,';',7).toInt();
    startColor[1] = getValue(s,';',8).toInt();
    startColor[2] = getValue(s,';',9).toInt();
  } else if ((char)payload[0] == '0') {
    DEBUG_PRINTLN("Disabling everything");
    doSunrise    = false;
    doFixedColor = false;
    doFire       = false;
    doFlash      = false;
    doRun        = false;
    colorWipe (pixels.Color(0, 0, 0), 0);
  } else {
    DEBUG_PRINTLN("Unknown RGB command");
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
  pixels.setBrightness(255);//change how bright here
  pixels.setPixelColor(0, pixels.Color(255,0,0));

  // Start the Pub/Sub client
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  mqttClient.setCallback(mqttCallback);

  // initial delay to let millis not be 0
  delay(1);

  // clear the strip
  colorWipe (pixels.Color(0, 0, 0), 0);
}

void loop() {
  // first, get current millis
  long now = millis();
  mqttClient.loop();

  // Check if the wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("Calling wifiConnect() as it seems to be required");
    wifiConnect();
  }

  // MQTT doing its stuff if the wifi is connected
  if (WiFi.status() == WL_CONNECTED) {
    //DEBUG_PRINT("Is the MQTT Client already connected? ");
    if (!mqttClient.connected()) {
      DEBUG_PRINTLN("MQTT is not connected, let's try to reconnect");
      if (! mqttReconnect()) {
        // This should not happen, but seems to...
        DEBUG_PRINTLN("MQTT was unable to connect! Exiting the upload loop");
      } else {
        readyToUpload = true;
        DEBUG_PRINTLN("MQTT successfully reconnected");
      }
    }
  }

  // if readyToUpload, letste go!
  if (now - lastMsg > PUBLISH_LOOP_SLEEP) {
    // long loopDrift = (now - lastMsg) - PUBLISH_LOOP_SLEEP;
    lastMsg = now;

    if (readyToUpload) {
      DEBUG_PRINT("MQTT discovery publish loop:");
      String clientMac = WiFi.macAddress();
      if (mqttClient.publish("/d1ws2812/discovery", clientMac.c_str())) {
        // Publishing values successful, removing them from cache
        DEBUG_PRINTLN(" successful");
      } else {
        DEBUG_PRINTLN(" FAILED!");
      }
    }
  }

  // enabling the onboard led
  digitalWrite(LED_BUILTIN, LOW);

  // Call RGB Strip functions
  sunrise();
  fire();
  flash();
  run();

  // disabling the onboard led
  digitalWrite(LED_BUILTIN, HIGH);
}
