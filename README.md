## ESP32 Light Dimmer


An attempt at a multi-channel incandescent light dimmer using the ESP32.  Will eventually provide HTML/javascript based UI for dimming lights.

### Status

Still under construction.  Able to do fades of up to 4 dimmer channels.

Experiments with the LEDC peripheral revealed a need to perform dimming manually (instead of with LEDC [fade functionality](http://esp-idf.readthedocs.io/en/latest/api/ledc.html#_CPPv213ledc_set_fade11ledc_mode_t14ledc_channel_t8uint32_t21ledc_duty_direction_t8uint32_t8uint32_t8uint32_t)) due to inability to adjust PWM phase via dimming.  So, we implemented the dimming logic inside zero-crossing interrupt routine, but needed to perform all math as fixed-point due to hard faults when doing floating point in ISR.

Next step is to implement HTML/wifi based control.  Looking at possibly using 


### Hardware

* [Sparkfun ESP 32 Thing](https://www.sparkfun.com/products/13907) (although any ESP32 module should work, possibly with changes in [pin configuration](https://cdn.sparkfun.com/assets/learn_tutorials/5/0/7/esp32-thing-graphical-datasheet-v02.png)).
* 2x [4 channel AC light dimmer board](http://www.inmojo.com/store/krida-electronics/item/4-channel-ac-light-dimmer-arduino--v2/) (but any MOSFET-based dimmer board with zero-crossing detection should work).

### Building

Requires [ESP32 IDF](https://github.com/espressif/esp-idf/).  

Currently using v4.4.3 (`git checkout v4.4 --recurse-submodules`)

```
. /path/to/esp/idf/export.sh
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p /dev/tty.usbserial* flash
```
