# D1 Mini WS2812 MQTT
This sketch is for connecting D1 Minis with WS2812 RGB LEDs attached to a MQTT.

## MQTT interface
### Discovery
It publishes its MAC address regularly to `/d1ws2812/discovery`.

### Control
It subscribes to two MQTT topics:
* `/d1ws2812/all`
* `/d1ws2812/MAC address`

To enable a effect, send a semicolon separated string to one of the topics.

# Available effects
## index
| ID | Name    | Version |
|---:|---------|---------|
|  0 | Off     | 0.1     |
|  1 | Sunrise | 0.1     |
|  2 | Fixed   | 0.1     |
| *3*| *Fade*  | *0.1*   |
| *4*|*Rainbow*| *0.1*   |
|  5 | Fire    | 0.1     |
|  6 | Flash   | 0.1     |
|  7 | Run     | 0.1     |

## Detailed description
### 0: off
All LEDs are switched off
Options: None

### 1: Sunrise
Sunrise simulation overt time
Options: None

### 2: Fixed
Fixed color
Options:  red;green;blue;wait ms

### 3: Fade
**Not yet implemented**
Fade from to a color
Options: None

### 4: Rainbow
**Not yet implemented**
Animated rainbow colors
Options: None

### 5: Sunrise
Animated fire effect
Options: None

### 6: Fixed
Flash a color
Options: red;green;blue

### 7: Run
Animate running light
Options: num of leds;delay;direction;acrive red;active green;active blue;passive red;passive green;passive blue
