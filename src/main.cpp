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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  DEBUG_PRINT("Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] ");
  for (unsigned int i = 0; i < length; i++) {
    DEBUG_PRINT((char)payload[i]);
  }
  DEBUG_PRINTLN();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    // Turn the LED on (Note that LOW is the voltage level
    digitalWrite(LED_BUILTIN, LOW);
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

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
      mqttClient.publish("outTopic", "hello world");
      // ... and resubscribe
      mqttClient.subscribe("inTopic");
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

void setup() {
  #ifdef DEBUG
  Serial.begin(SERIAL_BAUD); // initialize serial connection
  // delay for the serial monitor to start
  delay(3000);
  #endif

  // setup NeoPixel
  DEBUG_PRINTLN("Initializing LEDs");
  pixels.begin();
  pixels.setPixelColor(0, pixels.Color(255,0,0));

  // Start the Pub/Sub client
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVERPORT);
  mqttClient.setCallback(mqttCallback);

}

void loop() {
  // first, get current millis
  long now = millis();

  // Check if the wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("Calling wifiConnect() as it seems to be required");
    wifiConnect();
  }

  // MQTT doing its stuff if the wifi is connected
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINT("Is the MQTT Client already connected? ");
    if (!mqttClient.connected()) {
      DEBUG_PRINTLN("No, let's try to reconnect");
      if (! mqttReconnect()) {
        // This should not happen, but seems to...
        DEBUG_PRINTLN("MQTT was unable to connect! Exiting the upload loop");
      } else {
        readyToUpload = true;
      }
    } else {
      DEBUG_PRINTLN("Yes");
    }
  }

  // if readyToUpload, letste go!
  if (now - lastMsg > PUBLISH_LOOP_SLEEP) {
    long loopDrift = (now - lastMsg) - PUBLISH_LOOP_SLEEP;
    lastMsg = now;

    if (readyToUpload) {
      DEBUG_PRINT("MQTT discovery publish loop: ");
      mqttClient.loop();
      String clientMac = WiFi.macAddress();
      if (mqttClient.publish("/d1ws2812/discovery", clientMac.c_str())) {
        // Publishing values successful, removing them from cache
        DEBUG_PRINTLN(" successful");
      } else {
        DEBUG_PRINTLN(" FAILED!");
      }
    }
  }

  int delayval = 10;
  for(int i=0;i<NUMPIXELS;i++) {
    // pixels.Color takes RGB values, from 0,0,0 up to 255,255,255
    pixels.setPixelColor(i, pixels.Color(0,255,0));
    pixels.show();
    delay(delayval);
    pixels.setPixelColor(i, pixels.Color(255,0,0));
    pixels.show();
    delay(delayval);
    pixels.setPixelColor(i, pixels.Color(0,0,255));
    pixels.show();
    delay(delayval);
    pixels.setPixelColor(i, pixels.Color(0,0,0));
    pixels.show();
  }
}
