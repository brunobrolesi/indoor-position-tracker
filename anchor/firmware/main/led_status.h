#pragma once

typedef enum {
    LED_CONNECTING,  /* slow blink 500 ms — WiFi or MQTT not yet connected */
    LED_CONNECTED,   /* solid on — WiFi + MQTT active */
    LED_OTA,         /* fast blink 100 ms — OTA download in progress */
    LED_REBOOT,      /* 3 rapid blinks then off — controlled reboot imminent */
} led_state_t;

void led_status_init(void);
void led_status_start(void);
void led_status_set(led_state_t state);
