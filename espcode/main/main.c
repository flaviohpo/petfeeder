/**
 * @file main.c
 * @brief this is a simple petfeeder configurable by an localhost flask server.
 * The loop is:
 * - refresh the clock (clock api)
 * - Load feed schedule (flask server)
 * - check if it is time for feed
 * - feed if yes
 * - check if the pet is present
 * - notify the owner by sms
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "driver/mcpwm.h"

#include "sdkconfig.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "../secrets.h"

static const char *TAG = "MAIN";

/**
 * @brief Time eh o tempo que vai ficar ligado
 * Break eh se vai frear ou apenas soltar ao fim 
 */
typedef struct
{
    uint32_t Time; // in seconds
    uint8_t Break;  // 1 = break, 0 = release
}MOTOR_INSTR_ts;

QueueHandle_t QueueMotor;

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;

void wifi_routine(void);

static void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

#define PIN_STEP        12
#define PIN_DIR         13
#define PIN_EN          25
#define START_FREQ      300
void configure_step_motor(void)
{
    ESP_ERROR_CHECK(mcpwm_gpio_init(0, MCPWM0A, PIN_STEP));
    mcpwm_config_t pwm_config = 
    {
        .frequency = START_FREQ,
        .cmpr_a = 0,
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,
    };
    ESP_ERROR_CHECK(mcpwm_init(0, MCPWM_TIMER_0, &pwm_config));
    ESP_ERROR_CHECK(mcpwm_set_duty(0, MCPWM_TIMER_0, MCPWM_OPR_A, 0));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_DIR, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(PIN_DIR, 0));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_EN, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(PIN_EN, 1));
}

void task_step_motor(void* pv)
{
    static MOTOR_INSTR_ts Instruction = {0};
    configure_step_motor();
    while(true)
    {
        xQueueReceive(QueueMotor, &Instruction, portMAX_DELAY);
        if(Instruction.Time > 0)
        {
            ESP_ERROR_CHECK(gpio_set_level(PIN_EN, 0));
            ESP_ERROR_CHECK(mcpwm_set_duty(0, MCPWM_TIMER_0, MCPWM_OPR_A, 0.5));
        }
        while(Instruction.Time > 0)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            Instruction.Time--;
        }
        if(Instruction.Break == 0)
        {
            ESP_ERROR_CHECK(gpio_set_level(PIN_EN, 1));
        }
        ESP_ERROR_CHECK(mcpwm_set_duty(0, MCPWM_TIMER_0, MCPWM_OPR_A, 0));
    }
}

void app_main(void)
{
    MOTOR_INSTR_ts Instruction = {5, 0};

    QueueMotor = xQueueCreate(10, sizeof (MOTOR_INSTR_ts));

    /* Configure the peripheral according to the LED type */
    configure_led();
    
    xTaskCreatePinnedToCore(task_step_motor, "step_motor_driver",
                            4096, (void*)NULL, 4, NULL, 1);

    xQueueSend(QueueMotor, &Instruction, portMAX_DELAY);

    wifi_routine();

    while (1) 
    {
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}

///////////////////////////////////////////
///////// WIFI REQUESTS
#define EXAMPLE_ESP_MAXIMUM_RETRY  3

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) 
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } 
        else 
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = 
    {
        .sta = 
        {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) 
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else if (bits & WIFI_FAIL_BIT) 
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else 
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void wifi_routine(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}





