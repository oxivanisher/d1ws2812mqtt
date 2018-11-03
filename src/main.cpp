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
uint8_t currentColor[] = {0, 0, 0};
uint8_t fadeCount = 0;

// sunrise loop vars
unsigned long sunriseStartTime = 0;
uint8_t startColor[] = {0, 0, 0};
int sunriseLoopStep = 0;
bool doSunrise = false;

// fixed color vars
bool doFixedColor = false;

// fire
bool doFire = true;
unsigned long nextFireLoop = 0;

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
  }
  pixels.show();
}

// neopixel color fade loop
bool fade(uint8_t currentColor[], uint8_t startColor[], uint32_t fadeDuration, uint16_t redEnd, uint16_t greenEnd, uint16_t blueEnd) {
  if (currentFadeStart < 1) {
    // new fade loop. calculating and setting required things
    fadeCount++;
    currentFadeStart = millis();
    currentFadeEnd = currentFadeStart + (fadeDuration * 1000);
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
    { 50,   0,   0,   5},
    { 50,   0,  20,  52},
    { 50,  25,  20,  60},
    { 50, 207,  87,  39},
    { 50, 220, 162,  16},
    { 50, 255, 165,   0},
    { 50, 255, 255,  30}
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

  uint16_t r = 255;
  uint16_t g = r-40;
  uint16_t b = 40;

  for (uint16_t i = 0; i < pixels.numPixels(); i++) {
    uint16_t flicker = random(0,150);
    uint16_t r1 = r-flicker;
    uint16_t g1 = g-flicker;
    uint16_t b1 = b-flicker;
    if(r1<0) r1=0;
    if(g1<0) g1=0;
    if(b1<0) b1=0;
    pixels.setPixelColor(i, pixels.Color(r1, g1, b1));
    pixels.show();
  }

  nextFireLoop = millis() + random(50,150);
}

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
  } else if ((char)payload[0] == '2') {
      // check for correct number of options
      DEBUG_PRINTLN("Enabling fixed color");
      doSunrise = false;
      doFixedColor = true;
      doFire = false;

      String s = String((char*)payload);
      colorWipe (pixels.Color(getValue(s,';',1).toInt(), getValue(s,';',2).toInt(), getValue(s,';',3).toInt()), getValue(s,';',4).toInt());
  } else if ((char)payload[0] == '3') {
      // not yet implemented
      DEBUG_PRINTLN("Enabling fade to color");
      doSunrise = false;
      doFixedColor = false;
      doFire = false;
  } else if ((char)payload[0] == '4') {
      // not yet implemented
      DEBUG_PRINTLN("Enabling rainbow");
      doSunrise = false;
      doFixedColor = false;
      doFire = false;
  } else if ((char)payload[0] == '5') {
      // not yet implemented
      DEBUG_PRINTLN("Enabling fire");
      doSunrise = false;
      doFixedColor = false;
      doFire = true;
  } else if ((char)payload[0] == '0') {
      DEBUG_PRINTLN("Disabling everything");
      doSunrise = false;
      doFixedColor = false;
      doFire = false;
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
    long loopDrift = (now - lastMsg) - PUBLISH_LOOP_SLEEP;
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

  // disabling the onboard led
  digitalWrite(LED_BUILTIN, HIGH);

  // int delayval = 10;
  // for(int i=0;i<NUMPIXELS;i++) {
  //   // pixels.Color takes RGB values, from 0,0,0 up to 255,255,255
  //   pixels.setPixelColor(i, pixels.Color(0,255,0));
  //   pixels.show();
  //   delay(delayval);
  //   pixels.setPixelColor(i, pixels.Color(255,0,0));
  //   pixels.show();
  //   delay(delayval);
  //   pixels.setPixelColor(i, pixels.Color(0,0,255));
  //   pixels.show();
  //   delay(delayval);
  //   pixels.setPixelColor(i, pixels.Color(0,0,0));
  //   pixels.show();
  // }
}
