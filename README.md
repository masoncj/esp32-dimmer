## ESP32 Light Dimmer


An attempt at a multi-channel incandescent light dimmer using the ESP32.  Will eventually provide HTML/javascript based UI for dimming lights.

### Status

Still under construction.  Currently able to do fades of up to [4 dimmer channels]().

Experiments with the LEDC peripheral revealed a need to perform dimming manually (instead of with LEDC [fade functionality](http://esp-idf.readthedocs.io/en/latest/api/ledc.html#_CPPv213ledc_set_fade11ledc_mode_t14ledc_channel_t8uint32_t21ledc_duty_direction_t8uint32_t8uint32_t8uint32_t)) due to inability to adjust PWM phase via dimming.  So, we implemented the dimming logic inside zero-crossing interrupt routine, but needed to perform all math as fixed-point due to hard faults when doing floating point in ISR.

Currently working on implementing HTML/wifi based control.


### Hardware

* [Sparkfun ESP 32 Thing](https://www.sparkfun.com/products/13907) (although any ESP32 module should work, possibly with changes in [pin configuration](https://cdn.sparkfun.com/assets/learn_tutorials/5/0/7/esp32-thing-graphical-datasheet-v02.png)).
* 2x [4 channel AC light dimmer board](http://www.inmojo.com/store/krida-electronics/item/4-channel-ac-light-dimmer-arduino--v2/) (but any MOSFET-based dimmer board with zero-crossing detection should work).


### Software dependencies

Only one external dependency is required [ESP32 IDF](https://github.com/espressif/esp-idf/).  Please see there for toolchain configuration.  

This project makes use of:

* [Duktape-ESP32](https://github.com/nkolban/duktape-esp32/), which itself makes use of:
* [Duktape](http://duktape.org/) JS interpreter,
* ESPFS from [libesphttpd](https://github.com/Spritetm/libesphttpd/tree/master/espfs), 
* SPIFFS from [Lua-RTOS-ESP32](https://github.com/whitecatboard/Lua-RTOS-ESP32/)

All of these projects are included as git submodules.

### Building


```
git submodule update --init --recursive
export IDF_PATH=/path/to/esp-idf
make menuconfig
make duktape_configure
make images
make flash
```