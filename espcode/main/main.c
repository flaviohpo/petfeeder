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
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"

#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "../secrets.h"

///////////////////////////////////////////////////
////////// DEFINES ////////////////////////////////
///////////////////////////////////////////////////
/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO
#define EXAMPLE_ESP_MAXIMUM_RETRY  3

//// related to the step motor
#define PIN_STEP        12
#define PIN_DIR         13
#define PIN_EN          25
#define START_FREQ      300

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

///////////////////////////////////////////////////
////////// CUSTOM TYPES ///////////////////////////
///////////////////////////////////////////////////

/**
 * @brief Time eh o tempo que vai ficar ligado
 * Break eh se vai frear ou apenas soltar ao fim 
 */
typedef struct
{
    uint32_t Time; // in seconds
    uint8_t Break;  // 1 = break, 0 = release
}MOTOR_INSTR_ts;

typedef struct
{
    uint32_t Hora;
    uint32_t Min;
    uint32_t Seg;
}CLOCK_ts;

///////////////////////////////////////////////////
////////// VARIABLES //////////////////////////////
///////////////////////////////////////////////////

static uint8_t s_led_state = 0;
QueueHandle_t QueueMotor;
static const char *TAG = "MAIN";
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
CLOCK_ts MainClock = {0};
CLOCK_ts FeedClockStart = {0};
CLOCK_ts FeedClockEnd = {0};

///////////////////////////////////////////////////
////////// DECLARATIONS ///////////////////////////
///////////////////////////////////////////////////

///////////////////////////////////////////////////
/////////// WIFI AND CLIENT REQUESTS //////////////
///////////////////////////////////////////////////
void wifi_routine(void);
void wifi_init_sta(void);

void http_time_request(void);
void http_feed_request(void);
esp_err_t time_api_client_event_handler(esp_http_client_event_t *evt);
esp_err_t feed_value_client_event_handler(esp_http_client_event_t *evt);

static void blink_led(void);
static void configure_led(void);

void task_step_motor(void* pv);
void configure_step_motor(void);
static void seconds_callback(void* arg);

///////////////////////////////////////////////////
////////// PROTOTYPES /////////////////////////////
///////////////////////////////////////////////////
void clock_update(void)
{
    MainClock.Seg++;
    if(MainClock.Seg >= 60)
    {
        MainClock.Seg = 0;
        MainClock.Min++;
        if(MainClock.Min >= 60)
        {
            MainClock.Hora++;
            if(MainClock.Hora >= 24)
            {
                MainClock.Hora = 0;
            }
        }
    }
}

static void seconds_callback(void* arg)
{
    clock_update();
    ESP_LOGW(TAG, "Time=%02d:%02d:%02d", MainClock.Hora, MainClock.Min, MainClock.Seg);
}

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
    const esp_timer_create_args_t seconds_args = {.callback=&seconds_callback, .name="update MainClock"};
    esp_timer_handle_t seconds_timer;
    QueueMotor = xQueueCreate(10, sizeof (MOTOR_INSTR_ts));

    /* Configure the peripheral according to the LED type */
    configure_led();
    
    xTaskCreatePinnedToCore(task_step_motor, "step_motor_driver",
                            4096, (void*)NULL, 4, NULL, 1);

    xQueueSend(QueueMotor, &Instruction, portMAX_DELAY);

    wifi_routine();

    ESP_ERROR_CHECK(esp_timer_create(&seconds_args, &seconds_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(seconds_timer, 1000000)); // microseconds

    while (1) 
    {
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS / 2);
    }
}

////////////////////////////////////////
/////// WIFI CONNECTION ////////////////
////////////////////////////////////////
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
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
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
                 WIFI_SSID, WIFI_PASS);
    } 
    else if (bits & WIFI_FAIL_BIT) 
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 WIFI_SSID, WIFI_PASS);
    } 
    else 
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

////////////////////////////////////////
////////////////////////////////////////
////////////////////////////////////////
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

    http_time_request();

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    http_feed_request();
}

////////////////////////////////////////
////////// WIFI REQUESTS ///////////////
////////////////////////////////////////
void http_time_request(void)
{
    esp_http_client_config_t client_config = 
    {
        .url = "http://worldtimeapi.org/api/timezone/America/Sao_Paulo",
        .event_handler = time_api_client_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

void http_feed_request(void)
{
    esp_http_client_config_t client_config = 
    {           
        .url = "http://127.0.0.1:5000/get_value",
        .event_handler = feed_value_client_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

////////////////////////////////////////
////////// WIFI CALLBACKS //////////////
////////////////////////////////////////
esp_err_t time_api_client_event_handler(esp_http_client_event_t *evt)
{
    char* datetime_ptr;
    switch (evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ERROR\n");
        break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_CONNECTED\n");
        break;

        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_HEADERS_SENT\n");
        break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_HEADER: key=%s value=%s\n", evt->header_key, evt->header_value);
        break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_DATA: data=%s len=%d", (char*)evt->data, evt->data_len);
            datetime_ptr = strstr((char*)evt->data, "\"datetime\":\"");
            printf("\n\n%s\n", datetime_ptr);
            MainClock.Hora = (datetime_ptr[23] - 48) * 10 + (datetime_ptr[24] - 48);
            MainClock.Min = (datetime_ptr[26] - 48) * 10 + (datetime_ptr[27] - 48);
            MainClock.Seg = (datetime_ptr[29] - 48) * 10 + (datetime_ptr[30] - 48);
            printf("Time=%02d:%02d:%02d\n", MainClock.Hora, MainClock.Min, MainClock.Seg);
        break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_FINISH");
        break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_DISCONNECTED\n");
        break;

        default:
            ESP_LOGI("HTTP CLIENT", "EVENTO HTTP NAO TRATADO: %d\n", evt->event_id);
        break;
    }

    return ESP_OK;
}

esp_err_t feed_value_client_event_handler(esp_http_client_event_t *evt)
{
    char* datetime_ptr;
    switch (evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ERROR\n");
        break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_CONNECTED\n");
        break;

        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_HEADERS_SENT\n");
        break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_HEADER: key=%s value=%s\n", evt->header_key, evt->header_value);
            if(strcmp(evt->header_key, "horario_inicio") == 0)
            {
                FeedClockStart.Hora = (evt->header_value[0] - 48) * 10 + (evt->header_value[1] - 48);
                FeedClockStart.Min = (evt->header_value[3] - 48) * 10 + (evt->header_value[4] - 48);
                FeedClockStart.Seg = (evt->header_value[6] - 48) * 10 + (evt->header_value[7] - 48);
            }
            if(strcmp(evt->header_key, "horario_fim") == 0)
            {
                FeedClockEnd.Hora = (evt->header_value[0] - 48) * 10 + (evt->header_value[1] - 48);
                FeedClockEnd.Min = (evt->header_value[3] - 48) * 10 + (evt->header_value[4] - 48);
                FeedClockEnd.Seg = (evt->header_value[6] - 48) * 10 + (evt->header_value[7] - 48);
            }
        break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_DATA: data=%s len=%d", (char*)evt->data, evt->data_len);
        break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_FINISH");
        break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_DISCONNECTED\n");
        break;

        default:
            ESP_LOGI("HTTP CLIENT", "EVENTO HTTP NAO TRATADO: %d\n", evt->event_id);
        break;
    }

    return ESP_OK;
}