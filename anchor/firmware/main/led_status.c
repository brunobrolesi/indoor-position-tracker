#include "led_status.h"
#include "anchor_config.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_status";

static led_strip_handle_t s_strip;
static volatile led_state_t s_state = LED_CONNECTING;
static TaskHandle_t s_led_task_handle = NULL;

static void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void led_off(void)
{
    led_strip_clear(s_strip);
}

static void led_task(void *arg)
{
    (void)arg;

    while (true) {
        esp_task_wdt_reset();

        led_state_t state = s_state;

        switch (state) {
        case LED_CONNECTING:
            /* Amber 500 ms on / 500 ms off */
            led_set_color(255, 80, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_task_wdt_reset();
            led_off();
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_CONNECTED:
            /* Solid green */
            led_set_color(0, 200, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_REBOOT:
            /* 3 rapid red blinks then off — one-shot, then block */
            for (int i = 0; i < 3; i++) {
                led_set_color(255, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_task_wdt_reset();
                led_off();
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_task_wdt_reset();
            }
            /* Block indefinitely — caller calls esp_restart() after ~700 ms */
            vTaskSuspend(NULL);
            break;
        }
    }
}

void led_status_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num             = ANCHOR_LED_GPIO,
        .max_leds                   = 1,
        .led_model                  = LED_MODEL_WS2812,
        .color_component_format     = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz  = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 LED init on GPIO %d", ANCHOR_LED_GPIO);
}

void led_status_start(void)
{
    xTaskCreate(led_task, "led_task", 2048, NULL, 5, &s_led_task_handle);
    configASSERT(s_led_task_handle);
    esp_task_wdt_add(s_led_task_handle);
    ESP_LOGI(TAG, "LED task started");
}

void led_status_set(led_state_t state)
{
    s_state = state;
}
