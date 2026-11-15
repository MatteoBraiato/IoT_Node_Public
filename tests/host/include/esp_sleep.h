#ifndef HOST_SLEEP_H
#define HOST_SLEEP_H
#include "esp_err.h"
#include <stdint.h>
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_GPIO,
    ESP_SLEEP_WAKEUP_UART, ESP_SLEEP_WAKEUP_TOUCHPAD, ESP_SLEEP_WAKEUP_ULP } esp_sleep_wakeup_cause_t;
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t duration);
void esp_deep_sleep_start(void);
#endif
