#ifndef HOST_LOG_H
#define HOST_LOG_H
#include <stdio.h>
#include <inttypes.h>
/* Keep format arguments type-checked without disclosing command credentials. */
#define ESP_LOGE(tag, ...) do { (void)(tag); if (0) { printf(__VA_ARGS__); } } while (0)
#define ESP_LOGW ESP_LOGE
#define ESP_LOGI ESP_LOGE
#define ESP_LOGD ESP_LOGE
#define ESP_LOG_WARN 2
#define ESP_LOG_INFO 3
#define esp_log_level_set(tag, level) ((void)0)
#endif
