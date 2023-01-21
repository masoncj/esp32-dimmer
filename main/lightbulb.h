
/* HomeKit Lightbulb Example
*/

#ifndef _LIGHTBULB_H_
#define _LIGHTBULB_H_

#include <freertos/event_groups.h>

#define MAX_CHANNELS 8

/**
 * @brief initialize the lightbulb lowlevel module
 *
 * @param none
 *
 * @return none
 */
void lightbulb_init(void);

/**
 * @brief deinitialize the lightbulb's lowlevel module
 *
 * @param none
 *
 * @return none
 */
void lightbulb_deinit(void);

/**
 * @brief turn on/off the lowlevel lightbulb
 *
 * @param value The "On" value
 *
 * @return none
 */
int lightbulb_set_on(int channel, bool value);

/**
 * @brief set the brightness of the lowlevel lightbulb
 *
 * @param value The Brightness value
 *
 * @return
 *     - 0 : OK
 *     - others : fail
 */
int lightbulb_set_brightness(int channel, int value);

typedef struct {
    int num_channels;
    EventGroupHandle_t finished_event_group;
} lightbulb_thread_data;

void lightbulb_thread_entry(void* lightbulb_thread_data);

#endif /* _LIGHTBULB_H_ */