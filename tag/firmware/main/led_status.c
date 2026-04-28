#include "led_status.h"
#include "tag_config.h"

#include "led_strip.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "led_status";
static led_strip_handle_t s_strip;
static bool s_led_state = false;

static void led_timer_cb(void *arg)
{
    (void)arg;
    s_led_state = !s_led_state;
    if (s_led_state) {
        /* Green at low brightness */
        led_strip_set_pixel(s_strip, 0, 0, 10, 0);
        led_strip_refresh(s_strip);
    } else {
        led_strip_clear(s_strip);
    }
}

void led_status_init(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = LED_STATUS_GPIO,
        .max_leds       = 1,
        .led_model      = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 LED init on GPIO %d", LED_STATUS_GPIO);
}

void led_status_start(void)
{
    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {
        .callback = led_timer_cb,
        .name     = "led_status",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 500 * 1000ULL));
    ESP_LOGI(TAG, "LED heartbeat started at 1 Hz");
}
