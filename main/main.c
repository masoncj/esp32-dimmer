#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"
#include "freertos/xtensa_api.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <math.h>
#include <stdio.h>
#include <soc/ledc_struct.h>
#include <sys/time.h>

// Timing configuration ///////////////////////////////////////////////////////////

#define TICK_RATE_HZ 100
#define TICK_PERIOD_MS (1000 / TICK_RATE_HZ)
#define HZ 60
#define TESTING_FADE_DURATION_MSECS 5000
#define REPORT_DURATION_MSECS 10000
#define DUTY_BIT_DEPTH 10

// TRIAC is kept high for TRIAC_GATE_IMPULSE_CYCLES PWM counts before setting low.
#define TRIAC_GATE_IMPULSE_CYCLES 10

// TRIAC is always set low at least TRIAC_GATE_QUIESCE_CYCLES PWM counts before the next zero crossing.
#define TRIAC_GATE_QUIESCE_CYCLES 50

// Pin configuration //////////////////////////////////////////////////////////////
#define LED_PIN GPIO_NUM_5
#define ZERO_CROSSING_PIN GPIO_NUM_4
#define NUM_CHANNELS 4
static const uint8_t channel_pins[NUM_CHANNELS] = {
    GPIO_NUM_16,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_14,
};

// End Configurable ///////////////////////////////////////////////////////////////

volatile static uint32_t cycle_counter = 0;

// Fade record: each record represents a dimming interval for one or more channels.
typedef struct {
    uint16_t channels;  // Bitmask of channels to fade.
    uint16_t start_brightness;  // 0 to 1 << DUTY_BIT_DEPTH - 1.
    uint16_t end_brightness;  // 0 to 1 << DUTY_BIT_DEPTH - 1.
    uint32_t start_cycle_num;  // Value of cycle_counter when fade starts/started.
    uint32_t end_cycle_num;  // Value of cycle_counter when fade ends/ended.
} fade_t;

// NUM_FADE_RECORDS Must be power of 2.
#define NUM_FADE_RECORDS 32

// Circular buffer of fade records:
volatile static fade_t fades[NUM_FADE_RECORDS];
// See https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/ for explanation of ring buffer mechanics.
volatile static uint32_t start_fade_index = 0;
volatile static uint32_t end_fade_index = 0;

#define str(x) #x
#define xstr(x) str(x)
#define SHOW(x) ESP_LOGD(LOG_FADE, "%s %i\n", xstr(x), x);

#define MIN(x, y) ((x > y) ? y : x);
#define MAX(x, y) ((x > y) ? x : y);

static const char* LOG_STARTUP = "startup";
static const char* LOG_FADE = "fade";
static const char* LOG_CYCLES = "cycles";


esp_err_t event_handler(void *ctx, system_event_t *event) {
    return ESP_OK;
}

/** Fade start task and queue are used to ensure serialized access to ring buffer.  External tasks push fade
    request records onto fade_start_queue and fade_start_task then places them into buffer. */
xQueueHandle fade_start_queue;
void fade_start_task(void* arg){
    ESP_LOGI(LOG_STARTUP, "Fade start task started.\n");
    while(1) {
        fade_t fade;
        if (xQueueReceive(fade_start_queue, &fade, portMAX_DELAY)) {
            if (end_fade_index - start_fade_index > NUM_FADE_RECORDS) {
                ESP_LOGE(LOG_FADE, "Dropping fade at %i.\n", cycle_counter);
                continue;
            }
            fades[end_fade_index & (NUM_FADE_RECORDS - 1)] = (volatile fade_t) fade;
            end_fade_index += 1;
            ESP_LOGD(
                LOG_FADE,
                "At %i, enqueued fade from time %i, brightness %i to time %i, brightness %i. %i active fades.\n",
                cycle_counter,
                fade.start_cycle_num,
                fade.start_brightness,
                fade.end_cycle_num,
                fade.end_brightness,
                end_fade_index - start_fade_index
            );
        }
    }
}

/** Enqueue a request to fade the given channel over given time period to given final brightness.
 *
 * @param channel Integer channel number (0 - MAX_CHANNELS-1)
 * @param duration_msecs Duration of fade in milliseconds.
 * @param end_brightness ending brightness (from 0 to 1 DUTY_BIT_DEPTH -1).  Starting brightness is determined
 *    from either current brightness of channel (if not fading) or ending brightness of most recent fade.
 * @return pdPASS if successful, or error from queuing.
 */
BaseType_t set_channel_fade(uint32_t channel, uint32_t duration_msecs, uint16_t end_brightness) {
    if (duration_msecs == 0) {
        return 0;
    }
    end_brightness = MIN(end_brightness, (1 << DUTY_BIT_DEPTH) - 1);

    // Look for currently running fades to derive initial brightness and start time.
    uint16_t start_brightness;
    uint32_t start_cycle = cycle_counter;
    bool found = false;
    for(uint32_t fade_index = start_fade_index; fade_index < end_fade_index; ++fade_index) {
        fade_t fade = (fade_t) fades[fade_index & (NUM_FADE_RECORDS - 1)];
        if ((fade.channels & (1 << channel)) && fade.end_cycle_num >= start_cycle) {
            start_brightness = fade.end_brightness;
            start_cycle = fade.end_cycle_num;
            found = true;
        }
    }
    if (!found) {
        // Not currently fading, use current value, but remember to invert it (low hpoint is high brightness).
        start_brightness = ((1 << DUTY_BIT_DEPTH) - 1) - LEDC.channel_group[0].channel[channel].hpoint.hpoint;
    }

    fade_t fade;
    fade.channels = 1 << channel;
    fade.start_brightness = start_brightness;
    fade.end_brightness = end_brightness;
    fade.start_cycle_num = cycle_counter;
    fade.end_cycle_num = (uint32_t)(fade.start_cycle_num + (duration_msecs / 1000 * HZ * 2));

    // Enqueue a task to put the fade on the ring buffer.
    BaseType_t ret = xQueueSend(
        fade_start_queue,
        &fade,
        0  // Don't block.
    );

    if (ret == pdPASS) {
        ESP_LOGD(
            LOG_FADE,
            "At %i, setting channel %i to fade from time %i, brightness %i to time %i, brightness %i.\n",
            cycle_counter,
            channel,
            fade.start_cycle_num,
            fade.start_brightness,
            fade.end_cycle_num,
            fade.end_brightness
        );
    } else {
        ESP_LOGE(LOG_FADE, "At %i, failed to set channel fade: %i\n", cycle_counter, ret);
    }
    return ret;
}

/** Fade end queue and task receive notification when a given fade has completed.
 * Currently, for testing, we simply enqueue another request to fade to max or min brightness.
 */
xQueueHandle fade_end_queue;
void fade_end_task(void* arg){
    ESP_LOGI(LOG_STARTUP, "Fade end task started.\n");
    while(1) {
        fade_t fade;
        if(xQueueReceive(fade_end_queue, &fade, portMAX_DELAY)) {
            uint16_t brightness = fade.end_brightness > fade.start_brightness ? 0 : (1 << DUTY_BIT_DEPTH) - 1;
            for (uint16_t channel = 0; channel < NUM_CHANNELS; ++channel) {
                if (fade.channels & (1 << channel)) {
                    set_channel_fade(channel, TESTING_FADE_DURATION_MSECS, brightness);
                    ESP_LOGD(LOG_FADE, "Reset fade for channel %i to %i.\n", channel, brightness);
                }
            }
        }
    }
}

/** Debugging task that reports current cycle counter. */
void timer_report_task(void* arg) {
    ESP_LOGI(LOG_STARTUP, "Timer report task started, %i ticks per.\n", REPORT_DURATION_MSECS / TICK_PERIOD_MS);
    while(1) {
        vTaskDelay( REPORT_DURATION_MSECS / TICK_PERIOD_MS);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ESP_LOGD(LOG_CYCLES, "At time %li.%li, cycle count %i with %i active fades.\n",
               tv.tv_sec, tv.tv_usec, cycle_counter, end_fade_index - start_fade_index);
    }
}

/** Apply any active fade requests to the LEDC hardware.
 *
 * @param current_cycle The current cycle counter.
 */
void set_up_fades(uint32_t current_cycle) {
    // Apply any active fades.
    for (uint32_t fade_index = start_fade_index; fade_index != end_fade_index; ++fade_index) {
        fade_t fade = (fade_t) (fades[fade_index & (NUM_FADE_RECORDS - 1)]);
        if (fade.end_cycle_num >= current_cycle && fade.start_cycle_num <= current_cycle) {
            int32_t duration = fade.end_cycle_num - fade.start_cycle_num;
            int32_t time_left = fade.end_cycle_num - current_cycle;
            int32_t brightness = ((int32_t)fade.end_brightness) - ((int32_t)fade.start_brightness);
            int32_t hpoint_scale = brightness * time_left / duration;
            // TODO: do we need to scale here to account for non-linear brightness response?
            uint32_t hpoint = (uint32_t) (((int32_t)fade.start_brightness) + hpoint_scale);
            hpoint = MIN((1 << DUTY_BIT_DEPTH) - 1, hpoint);

            // Don't get too close to the zero crossing or the TRIAC will turn immediately off at highest
            // brightness.
            hpoint = MAX(TRIAC_GATE_QUIESCE_CYCLES, hpoint);
            uint32_t duty = TRIAC_GATE_IMPULSE_CYCLES;

            for (uint16_t channel = 0; channel < NUM_CHANNELS; ++channel) {
                if (fade.channels & (1 << channel)) {
                    if (hpoint >= (1 << DUTY_BIT_DEPTH) - 1 - TRIAC_GATE_IMPULSE_CYCLES) {
                        // If hpoint if very close to the maximum value, ie mostly off, simply turn off
                        // the output to avoid glitch where hpoint exceeds duty.
                        LEDC.channel_group[0].channel[channel].conf0.sig_out_en = 0;
                    } else {
                        LEDC.channel_group[0].channel[channel].hpoint.hpoint = hpoint;
                        LEDC.channel_group[0].channel[channel].duty.duty = duty << 4;
                        LEDC.channel_group[0].channel[channel].conf0.sig_out_en = 1;
                        LEDC.channel_group[0].channel[channel].conf1.duty_start = 1;
                    }
                }
            }
        }
    }

    uint32_t old_start_fade_index = start_fade_index;
    // Check if head of buffer is a completed fade.
    for (start_fade_index; start_fade_index != end_fade_index; ++start_fade_index) {
        volatile fade_t *start_fade = fades + (start_fade_index & (NUM_FADE_RECORDS - 1));
        if (start_fade->end_cycle_num > current_cycle) break;
    }
    // Notify of finished fades.  (Deliver after updating start_fade_index above so that receivers always see
    // finished fades as complete.)
    for (uint32_t fade_index = old_start_fade_index; fade_index != end_fade_index; ++fade_index) {
        volatile fade_t *fade = fades + (fade_index & (NUM_FADE_RECORDS - 1));
        if (fade->end_cycle_num == current_cycle) {
            xQueueSendFromISR(fade_end_queue, (fade_t*) fade, NULL);
        }
    }
}


void IRAM_ATTR zero_crossing_interrupt(void* arg) {
    // ISR triggered by GPIO edge at the end of each Alternating Current half-cycle.
    // Used to reset the PWM timer, which synchronize the TRIAC operation with
    // the mains frequency.  Also used to perform fading of PWM phase.

    uint32_t intr_st = GPIO.status;
    if (intr_st & (1 << ZERO_CROSSING_PIN)) {
        // Delay very slightly and retest pin to avoid bounce on negative edge.
        for (int i = 0; i < 100; ++i) {}
        if (GPIO.in & (1 << ZERO_CROSSING_PIN)) {

            GPIO.out_w1ts = 1 << LED_PIN;

            // Zero the PWM timer at the zero crossing.
            LEDC.timer_group[0].timer[0].conf.rst = 1;
            LEDC.timer_group[0].timer[0].conf.rst = 0;

            uint32_t current_cycle = (uint32_t)cycle_counter;
            set_up_fades(current_cycle);
            cycle_counter += 1;

            GPIO.out_w1tc = 1 << LED_PIN;
        }
    }
    GPIO.status_w1tc = intr_st;
}


void app_main(void) {
     nvs_flash_init();

    esp_log_level_set(LOG_FADE, ESP_LOG_WARN);
    esp_log_level_set(LOG_CYCLES, ESP_LOG_WARN);

     ESP_LOGI(LOG_STARTUP, "\n\nIn main.\n");
     
//     tcpip_adapter_init();
//     ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
//     ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
//     ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
//     wifi_config_t sta_config = {
//         .sta = {
//             .ssid = "mason",
//             .password = "PASSWORD",
//             .bssid_set = false
//         }
//     };
//     ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
//     ESP_ERROR_CHECK( esp_wifi_start() );
//     ESP_ERROR_CHECK( esp_wifi_connect() );
//     
//     ESP_LOGI(LOG_STARTUP, "Configured wifi.\n");
    
    // Set LED to monitor setup:
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 1);

    // Zero Crossing interrupt:
    gpio_set_direction(ZERO_CROSSING_PIN, GPIO_MODE_INPUT);
    gpio_set_intr_type(ZERO_CROSSING_PIN, GPIO_INTR_POSEDGE);
    gpio_set_pull_mode(ZERO_CROSSING_PIN, GPIO_PULLDOWN_ONLY);
    
    ESP_ERROR_CHECK( gpio_isr_register(
        zero_crossing_interrupt,
        NULL,  // No argument to handler.
        ESP_INTR_FLAG_LEVEL2,  
        NULL  // No need to store handler to interrupt registration.
    ) );
    
    ESP_LOGI(LOG_STARTUP, "Configured Zero Crossing Interrupt.\n");

    // Fade task and queue are used to serialize addition to the circular buffer of fade actions.
    fade_start_queue = xQueueCreate(
        16,
        sizeof(fade_t)
    );
    xTaskCreate(
        fade_start_task,
        "Fade start task",  // Task name.
        2048,   // Stack size.
        NULL,  // No parameters.
        10,  // Priority
        NULL  // No need to store task handle.
    );

    // Fade end task and queue are used to notify about the end of fades.
    fade_end_queue = xQueueCreate(
        16,
        sizeof(fade_t)
    );
    xTaskCreate(
        fade_end_task,
        "Fade end task",  // Task name.
        2048,   // Stack size.
        NULL,  // No parameters.
        8,  // Priority
        NULL  // No need to store task handle.
    );

    // Debugging task to report current cycle counter.
    xTaskCreate(
        timer_report_task,
        "Timer report task",  // Task name.
        2048,   // Stack size.
        NULL,  // No parameters.
        6,  // Priority
        NULL  // No need to store task handle.
    );

    ESP_LOGI(LOG_STARTUP, "Configured Fade Tasks and Queues.\n");

    
    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_LEDC_CLK_EN);
    DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_LEDC_RST);
    ESP_LOGI(LOG_STARTUP, "\nConfigured LED Controller.\n");
    
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = 0,
        .bit_num = DUTY_BIT_DEPTH,
        .freq_hz = HZ * 2,
    };
    ESP_ERROR_CHECK( ledc_timer_config(&timer_config) );
    
    ESP_LOGI(LOG_STARTUP, "Configured timer.\n");
    
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        ledc_channel_config_t led_config = {
            .gpio_num = channel_pins[i], 
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .channel = i,
            .timer_sel = LEDC_TIMER_0,
            .duty = (1 << DUTY_BIT_DEPTH) - 1,
            .intr_type = LEDC_INTR_DISABLE,
        };
        ESP_ERROR_CHECK( ledc_channel_config(&led_config) );
        LEDC.channel_group[0].channel[i].duty.duty = TRIAC_GATE_IMPULSE_CYCLES << 4;
        // Initial brightness of 0, meaning turn TRIAC on at very end:
        LEDC.channel_group[0].channel[i].hpoint.hpoint = (1 << DUTY_BIT_DEPTH) - 1;
        LEDC.channel_group[0].channel[i].conf0.sig_out_en = 1;
        LEDC.channel_group[0].channel[i].conf1.duty_start = 1;
    }
    
    ESP_LOGI(LOG_STARTUP, "Configured channels.\n");

    ESP_ERROR_CHECK( gpio_intr_enable(ZERO_CROSSING_PIN) );
    ESP_LOGI(LOG_STARTUP, "Enabled zero crossing interrupt.\n");

    for (uint16_t i = 0; i < NUM_CHANNELS; ++i) {
        set_channel_fade(i, TESTING_FADE_DURATION_MSECS, (1 << DUTY_BIT_DEPTH) - 1);
        vTaskDelay( TESTING_FADE_DURATION_MSECS / NUM_CHANNELS / TICK_PERIOD_MS);
    }

    ESP_LOGI(LOG_STARTUP, "Set channel fades.\n");

    gpio_set_level(LED_PIN, 0);

}
