/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file app.c
 * History:
 *  - 12/01/2025  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "app.h"
#include "esp_sleep.h"
#include <esp_log.h>
#include "esp_mac.h"
#include <bsp/bsp_do.h>
#include <bsp/bsp_di.h>
#include "e5lora.h"
#include <bsp/bsp_bme280.h>
#include "string.h"
#include <bsp/hw_cfg.h>
#include <bsp/bsp_max1704.h>
#include "bsp/bsp_i2c.h"

/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
#define APP_UART_BAUDRATE               (9600UL)

#define APP_LORA_MESSAGE_TIMEOUT_MS     (10000UL)
#define APP_LORA_MESSAGE_MAX_RETRY      (3U)

#define APP_TASK_30S_TASK_TIMEOUT_MS    (30000UL) // 30 sec

#define APP_FAULT_TEMPERATURE           (0x7FFF)   /* Fault sentinel for temperature (int16_t max, out-of-range) */
#define APP_FAULT_HUMIDITY              (0xFFFFU)  /* Fault sentinel for humidity  (uint16_t max) */
#define APP_FAULT_PRESSURE              (0xFFFFU)  /* Fault sentinel for pressure  (uint16_t max) */
/**************************************************************************************************
 *  3. PRIVATE MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4. PRIVATE ENUMERATIONS                                                                       *
 **************************************************************************************************/
enum app_state_e
{
    APP_ST_WAIT_INIT    = 0,
    APP_ST_RUNNING      = 2,
};

enum app_init_step_e
{
    APP_INIT_BSP_DI,
    #if HW_CFG_COMM_ENABLE
    APP_INIT_E5LORA,
    #endif
    APP_INIT_BME280,
    APP_INIT_MAX1704,
    APP_INIT_NUM
};
/**************************************************************************************************
 *  5. PRIVATE STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/
struct app_s
{
    bool_t gpioState;
    uint8_t buffer[64];

    int16_t  temperature_raw; /* 0.1 °C per LSB, e.g. 301 = 30.1 °C */
    uint16_t pressure_raw;    /* 10 Pa per LSB,  e.g. 10180 = 101800 Pa */
    uint16_t humidity_raw;    /* 0.1 %RH per LSB, e.g. 542 = 54.2 % */

    #if HW_CFG_COMM_ENABLE
    uint8_t loraMsg[E5LORA_MAX_PAYLOAD_LENGTH];
    #endif
    size_t loraMsgLength;
    bool_t loraMsgSent;
    bool_t loraMsgCallbackError;   /* Separate flag for callback errors */
    uint8_t loraMsgRetryNum;

    bsp_di_pin_status_t chargingStatus;

    uint8_t batterySoC;         /* Battery SoC (0-100%) */
    uint16_t batteryVoltage;    /* Voltage in centivolt (0.01V precision) */
};
/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/
typedef struct app_s app_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static void App_Lora_Message_Cb(res_t res, void *userData);
/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static res_t appInitRes[APP_INIT_NUM];

static const char *appInitString[APP_INIT_NUM] =
{
    [APP_INIT_BSP_DI] = "Bsp_DI",
    #if HW_CFG_COMM_ENABLE
    [APP_INIT_E5LORA] = "Bsp_E5Lora",
    #endif
    [APP_INIT_BME280] = "Bsp_BME280",
    [APP_INIT_MAX1704] = "Bsp_MAX1704"
};

static app_t app;

static const char* TAG = "APP";
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t App_Init(void)
{
    if (true == HW_CFG_DEEPSLEEP_EN)
    {
        esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(TAG, "Wakeup cause: %d", wakeupCause);
        switch (wakeupCause)
        {
            case ESP_SLEEP_WAKEUP_TIMER:
                ESP_LOGI(TAG, "Resuming from deep sleep (timer)");
                break;
            case ESP_SLEEP_WAKEUP_UNDEFINED:
            case ESP_SLEEP_WAKEUP_GPIO:
            case ESP_SLEEP_WAKEUP_UART:
            case ESP_SLEEP_WAKEUP_TOUCHPAD:
            case ESP_SLEEP_WAKEUP_ULP:
            default:
                ESP_LOGI(TAG, "Normal power‑on or other wakeup");
                break;
        }
    }

    res_t res = RES_OK;

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("APP", ESP_LOG_INFO);
    esp_log_level_set("E5_LORA", ESP_LOG_INFO);
    esp_log_level_set("bsp_uart", ESP_LOG_WARN);
    esp_log_level_set("BME280", ESP_LOG_WARN);
    esp_log_level_set("BSP_MAX1704", ESP_LOG_INFO);
    esp_log_level_set("Bdi_I2C", ESP_LOG_WARN);
    esp_log_level_set("Bsp_DI", ESP_LOG_WARN);

    appInitRes[APP_INIT_BSP_DI] = Bsp_Di_InitPin(BSP_DI_CHG, BSP_DI_PULLUP);
    #if HW_CFG_COMM_ENABLE
    appInitRes[APP_INIT_E5LORA] = E5Lora_Init();
    #endif
    appInitRes[APP_INIT_BME280] = Bsp_Bme280_Init();
    /* DEBUG wait to let voltage settle */
    appInitRes[APP_INIT_MAX1704] = Bsp_Max1704_Init();
    // appInitRes[APP_INIT_MAX1704] = RES_OK;  /* DEBUG: force MAX1704 init to OK for testing */

    for(uint8_t idx = 0; idx < APP_INIT_NUM; idx++)
    {
        if (RES_OK != appInitRes[idx])
        {
            ESP_LOGE(TAG, "Initialization failed: %s!",
                    appInitString[idx]);

            /* MAX17048 failure must not stop the application */
            if (idx != APP_INIT_MAX1704)
            {
                res = RES_ERROR;
            }
        }
        else
        {
            ESP_LOGI(TAG, "Module initialized: %s!",
                    appInitString[idx]);
        }
    }
    
    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "Initialization error\n\n");
    }
    else
    {
        ESP_LOGI(TAG, "Initialization done!\n\n");
    }

    if (true == HW_CFG_DEV_DEBUG_EN)
    {
        ESP_LOGW(TAG, "Development debug mode enabled, deep sleep is suspended");
    }
    
    return res;
}

UBaseType_t  enteringTaskStack;
void App_Task_1ms(void *arg)
{
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(1);
    enteringTaskStack = uxTaskGetStackHighWaterMark( NULL );
    xLastWakeTime = xTaskGetTickCount();
    while(1)
    {
         xTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
    vTaskDelete(NULL);
}


void App_Task_100ms(void *arg);

void App_Task_10s(void *arg)
{
    res_t    res;
    int32_t  tmpTemperature;  /* Temporary to receive int32_t from BSP driver */
    uint32_t tmpHumidity;     /* Temporary to receive uint32_t from BSP driver */
    uint32_t tmpPressure;     /* Temporary to receive uint32_t from BSP driver */
    bool_t   joined_now = false;

    ESP_LOGI(TAG, "Starting wake cycle");

    ESP_LOGI(TAG, "---Sensor data---");

    /* Check charging status */
    Bsp_Di_GetState(BSP_DI_CHG, &app.chargingStatus);
    if (BSP_DI_CHG_CHARGING == app.chargingStatus)
    {
        ESP_LOGI(TAG, "Device is CHARGING");
    }
    else
    {
        ESP_LOGI(TAG, "Device is NOT CHARGING");
    }

    /* Read data from the sensor */
    res = Bsp_Bme280_Read_Raw(&tmpTemperature, &tmpHumidity, &tmpPressure);
    
    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "Failed to read BME280 sensor data - error code: %d", res);
        app.temperature_raw = APP_FAULT_TEMPERATURE;  /* Set fault sentinel to signal invalid reading */
        app.humidity_raw    = APP_FAULT_HUMIDITY;     /* Set fault sentinel to signal invalid reading */
        app.pressure_raw    = APP_FAULT_PRESSURE;     /* Set fault sentinel to signal invalid reading */
    }
    else
    {
        /* Convert to 16-bit fields: temperature (0.1°C), humidity (0.1%RH), pressure (10 Pa per LSB) */
        app.temperature_raw = (int16_t)tmpTemperature;
        app.humidity_raw    = (uint16_t)tmpHumidity;
        app.pressure_raw    = (uint16_t)(tmpPressure / 10U);
        ESP_LOGI(TAG, "BME280 data - Temperature=%d.%d°C, Humidity=%d.%d%%, Pressure=%lu Pa",
                 (app.temperature_raw / 10), (app.temperature_raw % 10),
                 (app.humidity_raw / 10),    (app.humidity_raw % 10),
                 (uint32_t)app.pressure_raw * 10U);
    }

    /* Read Battery Data */
    res = Bsp_Max1704_Read_Soc(&app.batterySoC);
    res |= Bsp_Max1704_Read_Voltage(&app.batteryVoltage);

    /* Print battery data with 2 decimal places for voltage (stored as centivolt) */
    ESP_LOGI(TAG, "Battery data: voltage=%d.%02dV, SoC=%d%%\n\n", 
             (app.batteryVoltage/100), (app.batteryVoltage%100), 
             app.batterySoC);

    /* Plausibility check of the battery data */
    if (RES_OK == res)
    {
        if (app.batteryVoltage < 200 || (app.batteryVoltage > 500))  // <2V or >5V is not plausible for this device
        {
            ESP_LOGW(TAG, "Battery voltage reading is implausible: %d.%02dV. Check communication with MAX1704.", (app.batteryVoltage/100), (app.batteryVoltage%100));
            app.batteryVoltage = 0xFFFF;    // Set to 0xFFFF to indicate invalid reading
            app.batterySoC = 0xFF;          // Set to 0xFF to indicate invalid reading
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read battery data - error code: %d", res);
        app.batteryVoltage = 0xFFFF;    // Set to 0xFFFF to indicate invalid reading
        app.batterySoC = 0xFF;          // Set to 0xFF to indicate invalid reading
    }

    
    #if HW_CFG_COMM_ENABLE
    res = E5Lora_EnsureConnected(&joined_now);
    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "LoRa link is not ready for this wake cycle - error code: %d", res);
    }
    else
    {
        if (true == joined_now)
        {
            ESP_LOGI(TAG, "LoRa network joined during this wake cycle");
        }
        else
        {
            ESP_LOGI(TAG, "Reusing retained LoRa network session");
        }

        /* Build the LoRa message with sensor data */
        ESP_LOGI(TAG, "--- LoRa communication ---");
        ESP_LOGI(TAG, "Preparing LoRa message with sensors data...");
        /* Temperature: 2 bytes, big-endian, 0.1°C per LSB, signed */
        app.loraMsg[0]  = (uint8_t)((uint16_t)app.temperature_raw >> 8);
        app.loraMsg[1]  = (uint8_t)((uint16_t)app.temperature_raw & 0xFFU);

        /* Humidity: 2 bytes, big-endian, 0.1 %RH per LSB */
        app.loraMsg[2]  = (uint8_t)(app.humidity_raw >> 8);
        app.loraMsg[3]  = (uint8_t)(app.humidity_raw & 0xFFU);

        /* Pressure: 2 bytes, big-endian, 10 Pa per LSB (e.g. 10180 = 101800 Pa) */
        app.loraMsg[4]  = (uint8_t)(app.pressure_raw >> 8);
        app.loraMsg[5]  = (uint8_t)(app.pressure_raw & 0xFFU);

        /* Battery SoC: 1 byte, stored as percent */
        app.loraMsg[6]  = app.batterySoC;

        /* Battery voltage: 2 bytes, big-endian, stored as centivolt */
        app.loraMsg[7]  = (uint8_t)(app.batteryVoltage >> 8);
        app.loraMsg[8]  = (uint8_t)(app.batteryVoltage & 0xFFU);

        /* Charging status: 1 byte, 0 = not charging, 1 = charging */
        app.loraMsg[9]  = app.chargingStatus;

        app.loraMsgLength = 10U;

        /* Reset retry counter and flags BEFORE sending */
        app.loraMsgRetryNum = 0U;
        app.loraMsgSent = false;
        app.loraMsgCallbackError = false;

        /* Send message */
        res = E5Lora_SendMsgHex(app.loraMsg, app.loraMsgLength, App_Lora_Message_Cb);

        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "Failed to queue LoRa message - error code: %d", res);
        }
        else
        {
            /* Wait for send completion with proper timeout handling using TickType_t */
            TickType_t startSendTime_ticks = xTaskGetTickCount();
            TickType_t timeout_ticks = pdMS_TO_TICKS(APP_LORA_MESSAGE_TIMEOUT_MS);
            TickType_t globalStartTime_ticks = xTaskGetTickCount();  /* Global timeout to prevent infinite wait */
            TickType_t globalTimeout_ticks = pdMS_TO_TICKS(APP_LORA_MESSAGE_TIMEOUT_MS * (APP_LORA_MESSAGE_MAX_RETRY + 1));
            
            while ((false == app.loraMsgSent) && (app.loraMsgRetryNum < APP_LORA_MESSAGE_MAX_RETRY))
            {
                TickType_t elapsedTime = xTaskGetTickCount() - startSendTime_ticks;
                TickType_t globalElapsedTime = xTaskGetTickCount() - globalStartTime_ticks;
                
                /* Safety check: break if global timeout exceeded to prevent infinite wait */
                if (globalElapsedTime > globalTimeout_ticks)
                {
                    ESP_LOGW(TAG, "LoRa global timeout exceeded, breaking wait loop");
                    break;
                }
                
                /* Check for callback error - if callback was called with error, attempt retry */
                if (true == app.loraMsgCallbackError)
                {
                    app.loraMsgRetryNum++;
                    if (app.loraMsgRetryNum < APP_LORA_MESSAGE_MAX_RETRY)
                    {
                        ESP_LOGW(TAG, "LoRa callback error detected, retry %d/%d", app.loraMsgRetryNum, APP_LORA_MESSAGE_MAX_RETRY);
                        app.loraMsgSent = false;  /* Reset for retry */
                        app.loraMsgCallbackError = false;
                        res = E5Lora_SendMsgHex(app.loraMsg, app.loraMsgLength, App_Lora_Message_Cb);
                        startSendTime_ticks = xTaskGetTickCount();
                    }
                    else
                    {
                        break;  /* Max retries reached, exit loop */
                    }
                }
                else if (elapsedTime > timeout_ticks)
                {
                    /* Timeout on current send attempt - retry */
                    app.loraMsgRetryNum++;
                    if (app.loraMsgRetryNum < APP_LORA_MESSAGE_MAX_RETRY)
                    {
                        ESP_LOGW(TAG, "LoRa send timeout, retry %d/%d", app.loraMsgRetryNum, APP_LORA_MESSAGE_MAX_RETRY);
                        app.loraMsgSent = false;  /* Reset for retry */
                        res = E5Lora_SendMsgHex(app.loraMsg, app.loraMsgLength, App_Lora_Message_Cb);
                        startSendTime_ticks = xTaskGetTickCount();
                    }
                    else
                    {
                        break;  /* Max retries reached, exit loop */
                    }
                }
                else
                {
                    /* Still waiting for callback, sleep briefly */
                    vTaskDelay(pdMS_TO_TICKS(100U));
                }
            }

            /* Check final result */
            if (true == app.loraMsgSent)
            {
                ESP_LOGI(TAG, "LoRa message sent successfully");
                app.loraMsgSent = false;  /* Reset for next cycle */
            }
            else if (true == app.loraMsgCallbackError)
            {
                ESP_LOGE(TAG, "LoRa message sending failed - callback error after %d retries", app.loraMsgRetryNum);
            }
            else
            {
                ESP_LOGE(TAG, "LoRa message sending failed - timeout after %d retries", app.loraMsgRetryNum);
            }
        }
    }
    #endif
        
    /* ---- POWER SAVING ---- */
    if ((true == HW_CFG_DEEPSLEEP_EN) && (false == HW_CFG_DEV_DEBUG_EN))
    {
        ESP_LOGI(TAG, "Preparing to enter deep sleep...");
        res = App_Shutdown_Peripherals();
        if (RES_OK != res)
        {
            ESP_LOGW(TAG, "Shutdown incomplete (%d), continuing to deep sleep", res);
        }

        ESP_LOGI(TAG, "Entering deep sleep for %d minutes...\n", APP_SLEEP_INTERVAL_S/60);
        /* Go to deep sleep */
        esp_sleep_enable_timer_wakeup((uint64_t)APP_SLEEP_INTERVAL_S * 1000000ULL);
        esp_deep_sleep_start();
    }
    else
    {
        ESP_LOGI(TAG, "Wake cycle completed, staying awake for %lu ms in development mode", HW_CFG_DEV_DEBUG_AWAKE_MS);
        vTaskDelay(pdMS_TO_TICKS(HW_CFG_DEV_DEBUG_AWAKE_MS));
    }

    vTaskDelete(NULL);
}

#if 0
void App_Task_30s(void *arg)
{
    res_t res = RES_ERROR;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = pdMS_TO_TICKS(APP_TASK_30S_TASK_TIMEOUT_MS);
    xLastWakeTime = xTaskGetTickCount();
    
    while(1)
    {
        xTaskDelayUntil(&xLastWakeTime, xFrequency);

        #if HW_CFG_COMM_ENABLE
        
        /* Read sensor data (no averaging, direct value for low-power) */
        res = Bsp_Bme280_Read_Raw(&app.temperature_raw, &app.humidity_raw, &app.pressure_raw);
        
        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "Failed to read BME280 sensor for LoRa transmission - error code: %d", res);
        }
        else
        {
            ESP_LOGI(TAG, "Sensor data read: Temp=%ld, Hum=%ld, Press=%ld", app.temperature_raw, app.humidity_raw, app.pressure_raw);
            
            /* Build the LoRa message with sensor data */
            app.loraMsg[0]  = app.temperature_raw >> 24;
            app.loraMsg[1]  = app.temperature_raw >> 16;
            app.loraMsg[2]  = app.temperature_raw >> 8;
            app.loraMsg[3]  = app.temperature_raw;

            app.loraMsg[4]  = app.humidity_raw >> 24;
            app.loraMsg[5]  = app.humidity_raw >> 16;
            app.loraMsg[6]  = app.humidity_raw >> 8;
            app.loraMsg[7]  = app.humidity_raw;

            app.loraMsg[8]  = app.pressure_raw >> 24;
            app.loraMsg[9]  = app.pressure_raw >> 16;
            app.loraMsg[10] = app.pressure_raw >> 8;
            app.loraMsg[11] = app.pressure_raw;

            /* Append battery SoC (1 byte, stored as tenths of percent) */
            app.loraMsg[12] = app.batterySoC;

            app.loraMsgLength = 13;

            /* Reset retry counter and flags BEFORE sending */
            app.loraMsgRetryNum = 0U;
            app.loraMsgSent = false;
            app.loraMsgCallbackError = false;
            
            /* Send message */
            res = E5Lora_SendMsgHex(app.loraMsg, app.loraMsgLength, App_Lora_Message_Cb);

            if (RES_OK != res)
            {
                ESP_LOGE(TAG, "Failed to queue LoRa message - error code: %d", res);
            }
            else
            {
                /* Wait for send completion with proper timeout handling using TickType_t */
                TickType_t startSendTime_ticks = xTaskGetTickCount();
                TickType_t timeout_ticks = pdMS_TO_TICKS(APP_LORA_MESSAGE_TIMEOUT_MS);
                TickType_t globalStartTime_ticks = xTaskGetTickCount();  /* Global timeout to prevent infinite wait */
                TickType_t globalTimeout_ticks = pdMS_TO_TICKS(APP_LORA_MESSAGE_TIMEOUT_MS * (APP_LORA_MESSAGE_MAX_RETRY + 1));
                
                while ((false == app.loraMsgSent) && (app.loraMsgRetryNum < APP_LORA_MESSAGE_MAX_RETRY))
                {
                    TickType_t elapsedTime = xTaskGetTickCount() - startSendTime_ticks;
                    TickType_t globalElapsedTime = xTaskGetTickCount() - globalStartTime_ticks;
                    
                    /* Safety check: break if global timeout exceeded to prevent infinite wait */
                    if (globalElapsedTime > globalTimeout_ticks)
                    {
                        ESP_LOGW(TAG, "LoRa global timeout exceeded, breaking wait loop");
                        break;
                    }
                    
                    /* Check for callback error - if callback was called with error, attempt retry */
                    if (true == app.loraMsgCallbackError)
                    {
                        app.loraMsgRetryNum++;
                        if (app.loraMsgRetryNum < APP_LORA_MESSAGE_MAX_RETRY)
                        {
                            ESP_LOGW(TAG, "LoRa callback error detected, retry %d/%d", app.loraMsgRetryNum, APP_LORA_MESSAGE_MAX_RETRY);
                            app.loraMsgSent = false;  /* Reset for retry */
                            app.loraMsgCallbackError = false;
                            res = E5Lora_SendMsgHex(app.loraMsg, app.loraMsgLength, App_Lora_Message_Cb);
                            startSendTime_ticks = xTaskGetTickCount();
                        }
                        else
                        {
                            break;  /* Max retries reached, exit loop */
                        }
                    }
                    else if (elapsedTime > timeout_ticks)
                    {
                        /* Timeout on current send attempt - retry */
                        app.loraMsgRetryNum++;
                        if (app.loraMsgRetryNum < APP_LORA_MESSAGE_MAX_RETRY)
                        {
                            ESP_LOGW(TAG, "LoRa send timeout, retry %d/%d", app.loraMsgRetryNum, APP_LORA_MESSAGE_MAX_RETRY);
                            app.loraMsgSent = false;  /* Reset for retry */
                            res = E5Lora_SendMsgHex(app.loraMsg, app.loraMsgLength, App_Lora_Message_Cb);
                            startSendTime_ticks = xTaskGetTickCount();
                        }
                        else
                        {
                            break;  /* Max retries reached, exit loop */
                        }
                    }
                    else
                    {
                        /* Still waiting for callback, sleep briefly */
                        vTaskDelay(pdMS_TO_TICKS(100U));
                    }
                }

                /* Check final result */
                if (true == app.loraMsgSent)
                {
                    ESP_LOGI(TAG, "LoRa message sent successfully");
                    app.loraMsgSent = false;  /* Reset for next cycle */
                }
                else if (true == app.loraMsgCallbackError)
                {
                    ESP_LOGE(TAG, "LoRa message sending failed - callback error after %d retries", app.loraMsgRetryNum);
                }
                else
                {
                    ESP_LOGE(TAG, "LoRa message sending failed - timeout after %d retries", app.loraMsgRetryNum);
                }
            }
        }
        
        #endif
        
        /* ---- POWER SAVING ---- */
        if (true == HW_CFG_DEEPSLEEP_EN)
        {
            /* Power off sensors and other not needed peripherals */
            res = Bsp_Bme280_DeInit();
            if (RES_OK != res)
            {
                ESP_LOGW(TAG, "BME280 deinit returned %d", res);
            }

            #if HW_CFG_COMM_ENABLE
            res = E5Lora_DeInit();
            if (RES_OK != res)
            {
                ESP_LOGW(TAG, "E5Lora deinit returned %d", res);
            }
            #endif

            /* Go to deep sleep */
            esp_sleep_enable_timer_wakeup((uint64_t)APP_SLEEP_INTERVAL_S * 1000000ULL);
            esp_deep_sleep_start();
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
    }
    vTaskDelete(NULL);
}
#endif
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/
res_t App_Shutdown_Peripherals(void)
{
    res_t result = RES_OK;
    res_t res = Bsp_Max1704_Deinit();
    if ((RES_OK != res) && (RES_ERROR_UNINITIALIZED != res))
    {
        ESP_LOGW(TAG, "MAX1704 deinit returned %d", res);
        result = res;
    }

    res = Bsp_Bme280_DeInit();
    if ((RES_OK != res) && (RES_ERROR_UNINITIALIZED != res))
    {
        ESP_LOGW(TAG, "BME280 deinit returned %d", res);
        if (RES_OK == result)
        {
            result = res;
        }
    }

    res = Bsp_I2c_Shutdown();
    if (RES_OK != res)
    {
        ESP_LOGW(TAG, "I2C shutdown returned %d", res);
        if (RES_OK == result)
        {
            result = res;
        }
    }

    #if HW_CFG_COMM_ENABLE
    res = E5Lora_DeInit();
    if ((RES_OK != res) && (RES_ERROR_UNINITIALIZED != res))
    {
        ESP_LOGW(TAG, "E5 shutdown incomplete, sleep unconfirmed (%d)", res);
        if (RES_OK == result)
        {
            result = res;
        }
    }
    #endif
    return result;
}

static void App_Lora_Message_Cb(res_t res, void *userData)
{
    if (RES_OK == res)
    {
        app.loraMsgSent = true;
        ESP_LOGD(TAG, "LoRa callback: message sent successfully");
    }
    else
    {
        ESP_LOGE(TAG, "LoRa callback: send failed with error code %d", res);
        app.loraMsgCallbackError = true;  /* Signal error to retry logic */
    }
}
/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/
