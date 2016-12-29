#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <math.h>
#include <stdio.h>

#define HZ 60
#define DUTY_BIT_DEPTH 10
#define FADE_DURATION_SECS 4
#define FADE_CYCLES_BIT_DEPTH 10

#define LED_PIN GPIO_NUM_5
#define ZERO_CROSSING_PIN GPIO_NUM_4

#define NUM_CHANNELS 1
uint8_t channel_pins[NUM_CHANNELS] = { GPIO_NUM_16 };

bool channel_dir[NUM_CHANNELS] = { 0 };


#define str(x) #x
#define xstr(x) str(x)
#define SHOW(x) printf("%s %i\n", xstr(x), x);

xQueueHandle fade_end_queue;

esp_err_t event_handler(void *ctx, system_event_t *event) {
    return ESP_OK;
}

void set_channel_fade(uint32_t channel, bool dir) {
    uint32_t starting_duty;
    if (dir) {  // Increasing.
        starting_duty = 0;
    } else {  // Decreasing.
        starting_duty = (1 << (DUTY_BIT_DEPTH)) - 1;
    }
    uint32_t fade_duration_cycles = HZ * FADE_DURATION_SECS * 2;  // 2 half cycles per 60 hz cycle.
    uint32_t fade_steps = 1;
    if (fade_duration_cycles >= (1 << FADE_CYCLES_BIT_DEPTH)) {
        // If it would require more PWM cycles to fade than is available in the counter,
        // then we increase the number of cycles each step takes.
        fade_steps = ceil((float)fade_duration_cycles / (float)(1 << FADE_CYCLES_BIT_DEPTH));
        fade_duration_cycles = fade_duration_cycles / fade_steps;
    }
    if (fade_duration_cycles > 200) {
        fade_duration_cycles -= 200;
    }
    
    esp_err_t ledc_res = ledc_set_fade(
        LEDC_HIGH_SPEED_MODE,
        channel,
        starting_duty,  // duty - Starting duty cycle (either 0 if increasing or max if decreasing,
        dir, // gradule_direction - Direction of fade (true is increasing).
        (uint32_t)fade_duration_cycles,  // step_num - Number of PWM cycles that fading occurs over.
        fade_steps,  // duty_cyle_num - Number of cycles per step.
        1  // duty_scale - Scale for number of cycles??
    );
    ESP_ERROR_CHECK( ledc_res );
    LEDC.channel_group[0].channel[channel].hpoint.hpoint = 200;
    
    ESP_ERROR_CHECK( ledc_update_duty(
        LEDC_HIGH_SPEED_MODE,
        channel
    ) );
    
    printf("set channel %i fade direction %i\n", channel, dir);
}


void IRAM_ATTR zero_crossing_interrupt(void* arg) {
    // ISR triggered by GPIO edge at the end of each Alternating Current half-cycle.
    // Used to reset the PWM timer and synchronize the MOSFET operation with
    // the mains frequency.
    
    uint32_t intr_st = GPIO.status;
    if (intr_st & (1 << ZERO_CROSSING_PIN)) {
        // Delay very slightly to avoid bounce on negative edge.
        for (int i = 0; i < 50; ++i) {}
        if (GPIO.in & (1 << ZERO_CROSSING_PIN)) {
            LEDC.timer_group[0].timer[0].conf.rst = 1;
            LEDC.timer_group[0].timer[0].conf.rst = 0;
            for (int i = 0; i < 50; ++i) {}
        }
    }
    GPIO.status_w1tc = intr_st;
}


void IRAM_ATTR fade_end_task(void* arg) {
    printf("Fade end task started.");
    while(1) {
        uint32_t intr_st;
        // Await notification from fade end interrupt.
        xQueueReceive(fade_end_queue, &intr_st, portMAX_DELAY);    
        for (int i = 0; i < NUM_CHANNELS; ++i) {
            if (intr_st & 1 << (LEDC_DUTY_CHNG_END_HSCH0_INT_ST_S + i)) {    
                set_channel_fade(i, channel_dir[i]);
                channel_dir[i] = !channel_dir[i];
            }
        }
    }
}


void IRAM_ATTR fade_end_interrupt(void* arg) {
    
    GPIO.out_w1ts = 1 << LED_PIN;   

    // Look at which channels triggered the interrupt,
    uint32_t intr_st = LEDC.int_st.val; 
    
    // and enqueue a task to reset the fade.
    xQueueSendFromISR(
        fade_end_queue,
        &intr_st,
        NULL  // ESP examples don't seem to care about xHigherPriorityTaskWoken.
    );

    // Clear interrupts.
    LEDC.int_clr.val = intr_st;
    
    GPIO.out_w1tc = 1 << LED_PIN;  
}


void app_main(void) {
     nvs_flash_init();
     
     printf("\nIn main.\n");
     
//     tcpip_adapter_init();
//     ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
//     ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
//     ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
//     wifi_config_t sta_config = {
//         .sta = {
//             .ssid = "mason",
//             .password = "birdsareweird",
//             .bssid_set = false
//         }
//     };
//     ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
//     ESP_ERROR_CHECK( esp_wifi_start() );
//     ESP_ERROR_CHECK( esp_wifi_connect() );
//     
//     printf("\nConfigured wifi.\n");
    
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
    
    printf("\nConfigured Zero Crossing Interrupt.\n");
    
    SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_LEDC_CLK_EN);
    CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_LEDC_RST);
    printf("\nConfigured LED Controller.\n");
    
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = 0,
        .bit_num = DUTY_BIT_DEPTH,
        .freq_hz = HZ * 2,
    };
    ESP_ERROR_CHECK( ledc_timer_config(&timer_config) );
    
    printf("\nConfigured timer.\n");
    
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        ledc_channel_config_t led_config = {
            .gpio_num = channel_pins[i], 
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .channel = i,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .intr_type = LEDC_INTR_FADE_END,
        };
        ESP_ERROR_CHECK( ledc_channel_config(&led_config) );
    }
    
    printf("\nConfigured channels.\n");
    
    // Fade end queue and task are used to handle when LEDC channel finishes fading
    // from one brightness to another.
    fade_end_queue = xQueueCreate(
        16,
        sizeof(uint32_t)  // Holds a single uint32_t representing which LEDC channels have finished fading.
    );
    xTaskCreate(
        fade_end_task,
        "Fade end task",  // Task name.
        2048,   // Stack size.
        NULL,  // No parameters.
        6,  // Priority
        NULL  // No need to store task handle.
    );
    
    LEDC.int_clr.val = (1<<17) -1 ;

    ledc_isr_register(
        fade_end_interrupt,
        NULL,
        ESP_INTR_FLAG_LEVEL2,
        NULL
    ); 
    
    printf("\nConfigured fade end interrupt.\n");
    
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        set_channel_fade(i, 1);
    }
    
    printf("\nSet channel fades.\n");

//     SHOW(LEDC.channel_group[0].channel[0].conf0.timer_sel)
//     SHOW(LEDC.channel_group[0].channel[0].conf0.sig_out_en)
//     SHOW(LEDC.channel_group[0].channel[0].hpoint.hpoint)
//     SHOW(LEDC.channel_group[0].channel[0].duty.duty)
//     SHOW(LEDC.channel_group[0].channel[0].conf1.duty_scale)
//     SHOW(LEDC.channel_group[0].channel[0].conf1.duty_cycle)
//     SHOW(LEDC.channel_group[0].channel[0].conf1.duty_num)
//     SHOW(LEDC.channel_group[0].channel[0].conf1.duty_inc)
//     SHOW(LEDC.channel_group[0].channel[0].conf1.duty_start)
//     SHOW(LEDC.channel_group[0].channel[0].duty_rd.duty_read)
//     SHOW(LEDC.timer_group[0].timer[0].conf.bit_num)
//     SHOW(LEDC.timer_group[0].timer[0].conf.div_num)
//     SHOW(LEDC.timer_group[0].timer[0].conf.pause)
//     SHOW(LEDC.timer_group[0].timer[0].conf.rst)
//     SHOW(LEDC.timer_group[0].timer[0].conf.tick_sel)
//     SHOW(LEDC.timer_group[0].timer[0].conf.low_speed_update)
//     SHOW(LEDC.timer_group[0].timer[0].value.timer_cnt)
//     for (int i = 0; i < 20; ++i) {
//         SHOW(LEDC.timer_group[0].timer[0].value.timer_cnt)
//     }
    
    ESP_ERROR_CHECK( gpio_intr_enable(ZERO_CROSSING_PIN) );
    printf("Enabled zero crossing interrupt.\n");
    
    SHOW(LEDC.channel_group[0].channel[0].conf1.duty_start)
    SHOW(LEDC.timer_group[0].timer[0].value.timer_cnt)
    SHOW(LEDC.timer_group[0].timer[0].value.timer_cnt)
    SHOW(LEDC.timer_group[0].timer[0].conf.bit_num)
    SHOW(LEDC.timer_group[0].timer[0].conf.div_num)
    SHOW(LEDC.timer_group[0].timer[0].conf.pause)
    SHOW(LEDC.timer_group[0].timer[0].conf.rst)
    SHOW(LEDC.timer_group[0].timer[0].conf.tick_sel)
    SHOW(LEDC.timer_group[0].timer[0].conf.low_speed_update)
    SHOW(LEDC.timer_group[0].timer[0].value.timer_cnt)
    
    gpio_set_level(LED_PIN, 0);
    
    while(1) {}
}

