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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "example";

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

    while (1) 
    {
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
    }
}
