# D1 Mini WS2812 MQTT
This sketch is for connecting D1 Minis with WS2812 RGB LEDs attached to a MQTT.

## States
The LEDs are used to display minimal connection state information:
* Violet: At startup
* Red: No WiFi connection
* Yellow: WiFi connection but not connected to MQTT Server
* Light blue: Unable to connect to MQTT Server
* Green flash: Connected to WiFi and MQTT Server, everything is OK
* White: Will be set when the ESP gets resetted due to no network connection

## MQTT interface
### Discovery
It publishes its MAC address regularly to `/d1ws2812/discovery/MAC` with the
current version as value.
### Last will
It sets its last will to `/d1ws2812/lastwill/MAC` with the MAC as message. This
message will be retained and cleared on start.

### Control
It subscribes to two MQTT topics:
* `/d1ws2812/all`
* `/d1ws2812/MAC address`

To enable a effect, send a semicolon separated string to one of the topics.
Please be aware, that you have to end the attributes with a semicolon!

# Available effects
## index
| ID | Name              | Version |
|---:|-------------------|---------|
|  0 | Off               | 0.1     |
|  1 | Sunrise           | 0.1     |
|  2 | Fixed             | 0.1     |
| *3*| *Fade*            | *0.1*   |
| *4*|*Rainbow*          | *0.1*   |
|  5 | Fire              | 0.1     |
|  6 | Flash             | 0.1     |
|  7 | Run               | 0.1     |
|  8 | Fixed LED         | 0.1     |
|  9 | RGB Cycle reset   | 0.2     |
|  a | RGB Cycle         | 0.2     |
|  b | RGB run reset     | 0.2     |
|  c | RGB run           | 0.2     |
|  d | Twinkle           | 0.3     |
|  Y | run default       | 0.1     |
|  Z | Save default      | 0.1     |


## Detailed description
### 0: off
*Attributes:* None
All LEDs are switched off.

### 1: Sunrise
*Attributes:* None
Sunrise simulation overt time.

### 2: Fixed
*Attributes:*  red;green;blue;wait ms;
Fixed color for all LEDS.

### 3: Fade
**Not yet implemented**
*Attributes:* None
Fade from to a color.

### 4: Rainbow
**Not yet implemented**
*Attributes:* None
Animated rainbow colors.

### 5: Fire
*Attributes:*: None
Animated fire effect.

### 6: Flash
*Attributes:* red;green;blue;
Flash a color.

### 7: Run
*Attributes:* num of leds;loop delay;direction;active red;active green;active blue;passive red;passive green;passive blue;
Animate running light. It is always one led with the active color in <num of leds> in the passive color.

### 8: Fixed LED
*Attributes:* red;green;blue;LED index;LED index;LED index;...
Fixed color for a single LED. Please be aware, than this is not as effective as the normal fixed effect, since it will call `pixel.show()` for every LED on its own, not to mention the MQTT overhead.

### 9: RGB Cycle reset
*Attributes:* max brightness;loop delay
Cycles trough all colors and starts always at the same color.

### a: RGB Cycle
*Attributes:* max brightness;loop delay
Cycles trough all colors and continues at the last color.

### b: RGB run reset
*Attributes:* num of leds;run loop dely;direction;max brightness;rgb cycle delay
Cycles trough all colors and starts always at the same color for the run effect. The secondary "color" is always switched off LEDs (0;0;0).

### c: RGB run
*Attributes:* num of leds;run loop dely;direction;max brightness;rgb cycle delay
Cycles trough all colors and continues at the last color for the run effect. The secondary "color" is always switched off LEDs (0;0;0).

### d: Twinkle
*Attributes:* background red;background green;background blue;twinkle red;twinkle green;twinkle blue;twinkle min delay;twinkle max delay;twinkle min duration;twinkle max duration
Sets all LEDs to the background color, then "twinkles" (fade to twinkle color and back) random leds at random between twinkle min and max delay for a duration between twinkle min and max duration. Please be aware, that it chooses LEDs from 0 to `NUMPIXELS`. So if you connect only 30 LEDs, but NUMPIXELS is set to 120 (default), you have to consider the number of twinkles not for 30 but for 120 LEDs. There is a configurable amount of max twinkles at any given point in time in config.h under the name `MAX_TWINKLES`.

### Y: Run default
*Attributes:* None
Runs the before (Z) saved default effect.

### Z: Save default
*Attributes:* Z;other effect and options
Saves a effect to be run after time limited effects or called with (Y).
