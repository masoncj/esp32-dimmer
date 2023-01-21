
#include <stdio.h>
#include <string.h>
#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>
#include <hap_fw_upgrade.h>
#include <iot_button.h>

#include "esp_log.h"

#include "lightbulb.h"
#include <app_hap_setup_payload.h>

/* Comment out the below line to disable Firmware Upgrades */
#define CONFIG_FIRMWARE_SERVICE

static const char *TAG = "HAP lightbulb";

#define LIGHTBULB_TASK_PRIORITY  1
#define LIGHTBULB_TASK_STACKSIZE 4 * 1024
#define LIGHTBULB_TASK_NAME      "hap_lightbulb"

/* Reset network credentials if button is pressed for more than 3 seconds and then released */
#define RESET_NETWORK_BUTTON_TIMEOUT        3

/* Reset to factory if button is pressed and held for more than 10 seconds */
#define RESET_TO_FACTORY_BUTTON_TIMEOUT     10

/* The button "Boot" will be used as the Reset button for the example */
#define RESET_GPIO  GPIO_NUM_0
/**
 * @brief The network reset button callback handler.
 * Useful for testing the Wi-Fi re-configuration feature of WAC2
 */
static void reset_network_handler(void* arg)
{
    hap_reset_network();
}
/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void* arg)
{
    hap_reset_to_factory();
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
    button_handle_t handle = iot_button_create(key_gpio_pin, BUTTON_ACTIVE_LOW);
    iot_button_add_on_release_cb(handle, RESET_NETWORK_BUTTON_TIMEOUT, reset_network_handler, NULL);
    iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);
}

/* Mandatory identify routine for the accessory.
 * In a real accessory, something like LED blink should be implemented
 * got visual identification
 */
static int light_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}


static hap_serv_t* channel_servs[MAX_CHANNELS];

/* Callback for handling writes on the Light Bulb Service
 */
static int lightbulb_write(hap_write_data_t write_data[], int count,
                           void *serv_priv, void *write_priv)
{
    int i, ret = HAP_SUCCESS;
    hap_write_data_t *write;
    for (i = 0; i < count; i++) {
        write = &write_data[i];
        int channel = 0;
        for (; channel < MAX_CHANNELS; ++channel) {
            if (hap_char_get_parent(write_data->hc) == channel_servs[channel]) break;
        }
        if (channel == MAX_CHANNELS) {
            ESP_LOGE(TAG, "Received Write for char %u but couldn't find channel service.", hap_char_get_iid(write_data->hc));
            *(write->status) = HAP_STATUS_RES_ABSENT;
            return HAP_FAIL;
        }
        /* Setting a default error value */
        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
            ESP_LOGI(TAG, "Received Write for Channel %i Light %s", channel, write->val.b ? "On" : "Off");
            if (lightbulb_set_on(channel, write->val.b) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_BRIGHTNESS)) {
            ESP_LOGI(TAG, "Received Write for Channel %i Light Brightness %d", channel, write->val.i);
            if (lightbulb_set_brightness(channel, write->val.i) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }
        /* If the characteristic write was successful, update it in hap core
         */
        if (*(write->status) == HAP_STATUS_SUCCESS) {
            hap_char_update_val(write->hc, &(write->val));
        } else {
            /* Else, set the return value appropriately to report error */
            ret = HAP_FAIL;
        }
    }
    return ret;
}


static int create_lightbulb_channel(int channel, hap_acc_t *accessory) {
    /* Create the Light Bulb Service. Include the "name" since this is a user visible service  */
    hap_serv_t *service = hap_serv_lightbulb_create(true);
    channel_servs[channel] = service;
    if (!service) {
        ESP_LOGE(TAG, "Failed to create LightBulb Service");
        return -1;
    }

    /* Add the optional characteristic to the Light Bulb Service */
    char light_name[30];
    snprintf(light_name, 30, "Light %i", channel);
    int ret = hap_serv_add_char(service, hap_char_name_create(light_name));
    ret |= hap_serv_add_char(service, hap_char_brightness_create(0));

    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add optional characteristics to LightBulb");
        return -1;
    }
    /* Set the write callback for the service */
    hap_serv_set_write_cb(service, lightbulb_write);

    /* Add the Light Bulb Service to the Accessory Object */
    hap_acc_add_serv(accessory, service);
    return 0;
}

/*The main thread for handling the Light Bulb Accessory */
void lightbulb_thread_entry(void *param)
{
    lightbulb_thread_data* lightbulb = (lightbulb_thread_data*)param;
    hap_acc_t *accessory;

    hap_set_debug_level(HAP_DEBUG_LEVEL_INFO);
    hap_http_debug_enable();

    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
            .name = "Multi Dimmer",
            .manufacturer = "Mason",
            .model = "EspLight02",
            .serial_num = "abcdefg",
            .fw_rev = "0.9.0",
            .hw_rev = "1.0",
            .pv = "1.1.0",
            .identify_routine = light_identify,
            .cid = HAP_CID_LIGHTING,
    };

    /* Create accessory object */
    accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        goto light_err;
    }

    /* Add a dummy Product Data */
    uint8_t product_data[] = {'E','S','P','D','I','M','E','E'};
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    /* Add Wi-Fi Transport service required for HAP Spec R16 */
    hap_acc_add_wifi_transport_service(accessory, 0);

    for(int i = 0; i < lightbulb->num_channels; ++i) {
        int ret = create_lightbulb_channel(i, accessory);
        if (ret == -1) goto light_err;
    }

//#ifdef CONFIG_FIRMWARE_SERVICE
//    /*  Required for server verification during OTA, PEM format as string  */
//    static char server_cert[] = {};
//    hap_fw_upgrade_config_t ota_config = {
//            .server_cert_pem = server_cert,
//    };
//    /* Create and add the Firmware Upgrade Service, if enabled.
//     * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
//     * and the top level README for more information.
//     */
//    hap_serv_t *service = hap_serv_fw_upgrade_create(&ota_config);
//    if (!service) {
//        ESP_LOGE(TAG, "Failed to create Firmware Upgrade Service");
//        goto light_err;
//    }
//    hap_acc_add_serv(accessory, service);
//#endif

    /* Add the Accessory to the HomeKit Database */
    hap_add_accessory(accessory);

    /* Initialize the Light Bulb Hardware */
    lightbulb_init();

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    //reset_key_init(RESET_GPIO);

    /* TODO: Do the actual hardware initialization here */

    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    /* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
    hap_set_setup_code(CONFIG_EXAMPLE_SETUP_CODE);
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id(CONFIG_EXAMPLE_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
#else
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
#endif
#endif

    /* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
    //hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

    /* After all the initializations are done, start the HAP core */
    hap_start();

    ESP_LOGI(TAG, "Finished hap_start");

    xEventGroupSetBits(lightbulb->finished_event_group, BIT0);

    /* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
    vTaskDelete(NULL);

    light_err:
    hap_acc_delete(accessory);
    vTaskDelete(NULL);
}
