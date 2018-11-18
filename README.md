# D1 Mini WS2812 MQTT
This sketch is for connecting D1 Minis with WS2812 RGB LEDs attached to a MQTT.

## MQTT interface
### Discovery
It publishes its MAC address regularly to `/d1ws2812/discovery`.

### Control
It subscribes to two MQTT topics:
* `/d1ws2812/all`
* `/d1ws2812/MAC address`

To enable a effect, send a semicolon separated string to one of the topics. Please be aware, that you have to end the attributes with a semicolon!

# Available effects
## index
| ID | Name      | Version |
|---:|-----------|---------|
|  0 | Off       | 0.1     |
|  1 | Sunrise   | 0.1     |
|  2 | Fixed     | 0.1     |
| *3*| *Fade*    | *0.1*   |
| *4*|*Rainbow*  | *0.1*   |
|  5 | Fire      | 0.1     |
|  6 | Flash     | 0.1     |
|  7 | Run       | 0.1     |
|  8 | Fixed LED | 0.1     |

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
*Attributes:* num of leds;delay;direction;acrive red;active green;active blue;passive red;passive green;passive blue;  
Animate running light.

### 8: Fixed LED
*Attributes:* red;green;blue;LED index;LED index;LED index;...
Fixed color for a single LED. Please be aware, than this is not as effective as the normal fixed effect, since it will call `pixel.show()` for every LED on its own, not to mention the MQTT overhead.
