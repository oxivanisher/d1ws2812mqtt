#include "Adafruit_NeoPixel.h"
#include "ESP8266WiFi.h"
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

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
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

/****************************** Feeds ***************************************/

// Setup a feed called 'photocell' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish discovery = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/d1ws2812/discovery");

// Setup a feed called 'onoff' for subscribing to changes.
Adafruit_MQTT_Subscribe onoffbutton = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/d1ws2812/onoff");

/*************************** Sketch Code ************************************/

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
void MQTT_connect();

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

  // Connect to WiFi access point.
  DEBUG_PRINT("Connecting to "); DEBUG_PRINTLN(WLAN_SSID);
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_PRINT(".");
  }
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("WiFi connected");
  pixels.setPixelColor(0, pixels.Color(255,165,0));
  DEBUG_PRINT("MAC Address: "); DEBUG_PRINTLN(WiFi.macAddress());
  DEBUG_PRINT("IP address: "); DEBUG_PRINTLN(WiFi.localIP());

  // Setup MQTT subscription for onoff feed.
  mqtt.subscribe(&onoffbutton);
}

void loop() {
  // loop MQTT
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();
  pixels.setPixelColor(0, pixels.Color(0,255,0));
  #ifdef DEBUG
    delay(1000);
  #endif


  // loop NeoPixel
 int delayval = 100;
 // For a set of NeoPixels the first NeoPixel is 0, second is 1, all the way up to the count of pixels minus one.

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


 // this is our 'wait for incoming subscription packets' busy subloop
 // try to spend your time here
 Adafruit_MQTT_Subscribe *subscription;
 while ((subscription = mqtt.readSubscription(1000))) {
   if (subscription == &onoffbutton) {
     DEBUG_PRINT(F("Got: "));
     DEBUG_PRINTLN((char *)onoffbutton.lastread);
   }
 }

 // Now we can publish stuff!
 DEBUG_PRINT(F("\nSending discovery package "));  //DEBUG_PRINT(mac);

 // try to include WiFi.macAddress() in the future
 int mac = 5;

 if (! discovery.publish(mac)) {
   DEBUG_PRINTLN(F(" Failed"));
 } else {
   DEBUG_PRINTLN(F(" OK!"));
 }

 // ping the server to keep the mqtt connection alive
 // NOT required if you are publishing once every KEEPALIVE seconds
 /*
 if(! mqtt.ping()) {
   mqtt.disconnect();
 }
 */

}


// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  DEBUG_PRINT("Connecting to MQTT... ");

  uint8_t retries = 5;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       DEBUG_PRINTLN(mqtt.connectErrorString(ret));
       DEBUG_PRINTLN("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         DEBUG_PRINTLN("Too many retries. Resetting WDT.");
         while (1);
       }
  }
  DEBUG_PRINTLN("MQTT Connected!");
}
