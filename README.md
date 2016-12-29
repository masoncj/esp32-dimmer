## ESP32 Light Dimmer


An attempt at a multi-channel incandescent light dimmer using the ESP32.  Will provide HTML/javascript based UI for dimming lights.

### Status

Still under construction.  Experiments with the LEDC peripheral have revealed a need to perform dimming manually (instead of with LEDC [fade functionality](http://esp-idf.readthedocs.io/en/latest/api/ledc.html#_CPPv213ledc_set_fade11ledc_mode_t14ledc_channel_t8uint32_t21ledc_duty_direction_t8uint32_t8uint32_t8uint32_t)) due to inability to adjust PWM phase via dimming.  We're working on using the zero crossing interrupt to perform dimming instead.


### Hardware

* [Sparkfun ESP 32 Thing](https://www.sparkfun.com/products/13907) (although any ESP32 module should work, possibly with changes in [pin configuration](https://cdn.sparkfun.com/assets/learn_tutorials/5/0/7/esp32-thing-graphical-datasheet-v02.png)).
* 2x [4 channel AC light dimmer board](http://www.inmojo.com/store/krida-electronics/item/4-channel-ac-light-dimmer-arduino--v2/) (but any MOSFET-based dimmer board with zero-crossing detection should work).

### Building

Requires [ESP32 IDF](https://github.com/espressif/esp-idf/).

```
export IDF_PATH=/path/to/esp-idf
make menuconfig
make flash
```