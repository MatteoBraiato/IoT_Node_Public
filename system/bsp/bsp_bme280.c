/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file bsp_bme280.c
 * History:
 *  - 21/03/2025  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "bdi/bdi_i2c.h"
#include "bsp/bsp_bme280.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdatomic.h>

#include "bosch_bme280/bme280.h"
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
#define BSP_BME280_I2C_ADDR                         (0x76)

#define BSP_BME280_SENSOR_DATA_BUFFER_DIM           (8U)
#define BSP_BME280_DATAREAD_TIMEOUT_MS              (5000UL) 

#define SAMPLE_COUNT                                (1U)
#define BSP_BME280_SAMPLE_PERIOD_MS                 (100UL)
/**************************************************************************************************
 *  3. PRIVATE MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4. PRIVATE ENUMERATIONS                                                                       *
 **************************************************************************************************/

/**************************************************************************************************
 *  5. PRIVATE STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/
struct bsp_bme280_s
{
    bool_t init;
    bool_t interfaceReady;

    uint8_t sensorData[BSP_BME280_SENSOR_DATA_BUFFER_DIM];
    atomic_bool sensorDataReadFlag;
    bool_t readTimeoutFlag;

    atomic_bool sensorDataWriteFlag;
    bool_t writeTimeoutFlag;

    uint32_t humidity;      /* Factor 0.1 --> e.g. 542 = 54,2% */
    uint32_t pressure;      /* Factor 1 --> e.g.101798 = 101798Pa */
    int32_t temperature;   /* Factor 0.1 --> e.g. 301 = 30,1°C */

    int8_t result;
    uint32_t period;
    struct bme280_dev dev;
    struct bme280_settings settings;
};
/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/
typedef struct bsp_bme280_s bsp_bme280_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static void Bsp_Bme280_Write_Cb(bdi_i2c_op_result_t res, void *userData);
static void Bsp_Bme280_Read_Cb(bdi_i2c_op_result_t res, void *userData);

static int8_t Get_Humidity_Forced(uint32_t period, struct bme280_dev *dev);
static int8_t Get_Pressure_Forced(uint32_t period, struct bme280_dev *dev);
static int8_t Get_Temperature_Forced(uint32_t period, struct bme280_dev *dev);

static BME280_INTF_RET_TYPE bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr);
static BME280_INTF_RET_TYPE bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr);
BME280_INTF_RET_TYPE bme280_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr);
BME280_INTF_RET_TYPE bme280_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr);
static void bme280_delay_us(uint32_t period, void *intf_ptr);
static void bme280_error_codes_print_result(const char api_name[], int8_t rslt);
static int8_t bme280_interface_selection(struct bme280_dev *dev, uint8_t intf);
static res_t bme280_deinit(void);
/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static bsp_bme280_t bspBme280;

static const char* TAG = "BME280";
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t Bsp_Bme280_Init(void)
{
    res_t res = RES_ERROR;
    int8_t cumulativeResult = BME280_OK;

    bspBme280.result = bme280_interface_selection(&bspBme280.dev, BME280_I2C_INTF);
    bme280_error_codes_print_result("bme280_interface_selection", bspBme280.result);
    cumulativeResult |= bspBme280.result;

    bspBme280.result = bme280_init(&bspBme280.dev);
    bme280_error_codes_print_result("bme280_init", bspBme280.result);
    cumulativeResult |= bspBme280.result;

    bspBme280.result = bme280_get_sensor_settings(&bspBme280.settings, &bspBme280.dev);
    bme280_error_codes_print_result("bme280_get_sensor_settings", bspBme280.result);
    cumulativeResult |= bspBme280.result;

    /* Configuring the over-sampling rate, filter coefficient and standby time */
    /* Overwrite the desired settings */
    bspBme280.settings.filter = BME280_FILTER_COEFF_OFF;

    /* Over-sampling rate for humidity, temperature and pressure */
    bspBme280.settings.osr_h = BME280_OVERSAMPLING_1X;
    bspBme280.settings.osr_p = BME280_OVERSAMPLING_1X;
    bspBme280.settings.osr_t = BME280_OVERSAMPLING_1X;

    /* Setting the standby time */
    bspBme280.settings.standby_time = BME280_STANDBY_TIME_0_5_MS;

    bspBme280.result = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &bspBme280.settings, &bspBme280.dev);
    bme280_error_codes_print_result("bme280_set_sensor_settings", bspBme280.result);
    cumulativeResult |= bspBme280.result;

    /* Always set the power mode after setting the configuration */
    bspBme280.result = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &bspBme280.dev);
    bme280_error_codes_print_result("bme280_set_power_mode", bspBme280.result);
    cumulativeResult |= bspBme280.result;

    /* Calculate measurement time in microseconds */
    bspBme280.result = bme280_cal_meas_delay(&bspBme280.period, &bspBme280.settings);
    bme280_error_codes_print_result("bme280_cal_meas_delay", bspBme280.result);
    ESP_LOGD(TAG, "Meas delay=%" PRIu32 "", bspBme280.period);
    cumulativeResult |= bspBme280.result;

    if (cumulativeResult == BME280_OK)
    {
        /* mark module as initialized so callers can rely on state */
        bspBme280.init = true;
        res = RES_OK;
    }

    return res;
}

res_t Bsp_Bme280_DeInit(void)
{
    res_t res = RES_ERROR_UNINITIALIZED;
    /* Interface readiness survives a failed sensor initialization. */
    if (true == bspBme280.interfaceReady)
    {
        bspBme280.result = bme280_set_sensor_mode(BME280_POWERMODE_SLEEP, &bspBme280.dev);
        if (BME280_OK == bspBme280.result)
        {
            res = RES_OK;
        }
        else
        {
            ESP_LOGW(TAG, "BME280 sleep unconfirmed, err=%d", bspBme280.result);
            res = RES_ERROR;
        }
        /* The application shuts down the shared bus after both sensors. */
        bspBme280.interfaceReady = false;
    }
    bspBme280.init = false;
    return res;
}

res_t Bsp_Bme280_Read_Raw(int32_t *temperature_raw, uint32_t *humidity_raw, uint32_t *pressure_raw)
{
    res_t res = RES_ERROR;
    int8_t rslt = BME280_OK;

    /* Check if sensor is initialized */
    if (bspBme280.init == false)
    {
        res = RES_ERROR_UNINITIALIZED;
        ESP_LOGE(TAG, "BME280 not initialized");
    }
    /* Validate input parameters */
    else if ((NULL == temperature_raw) || (NULL == humidity_raw) || (NULL == pressure_raw))
    {
        res = RES_ERROR_INVALID_PARAMETER;
        ESP_LOGE(TAG, "Invalid parameters - NULL pointer detected");
    }
    else
    {
        /* Read humidity */
        rslt = Get_Humidity_Forced(bspBme280.period, &bspBme280.dev);
        bme280_error_codes_print_result("get_humidity", rslt);
        
        if (BME280_OK != rslt)
        {
            res = RES_ERROR;
            ESP_LOGE(TAG, "Failed to read humidity");
        }
        else
        {
            *humidity_raw = bspBme280.humidity;

            /* Read pressure */
            rslt = Get_Pressure_Forced(bspBme280.period, &bspBme280.dev);
            bme280_error_codes_print_result("get_pressure", rslt);
            
            if (BME280_OK != rslt)
            {
                res = RES_ERROR;
                ESP_LOGE(TAG, "Failed to read pressure");
            }
            else
            {
                *pressure_raw = bspBme280.pressure;

                /* Read temperature */
                rslt = Get_Temperature_Forced(bspBme280.period, &bspBme280.dev);
                bme280_error_codes_print_result("get_temperature", rslt);
                
                if (BME280_OK != rslt)
                {
                    res = RES_ERROR;
                    ESP_LOGE(TAG, "Failed to read temperature");
                }
                else
                {
                    *temperature_raw = bspBme280.temperature;
                    res = RES_OK;
                }
            }
        }
    }

    return res;
}
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/
static void Bsp_Bme280_Write_Cb(bdi_i2c_op_result_t res, void *userData)
{
    if (BDI_I2C_RES_DONE == res)
    {
        bspBme280.sensorDataWriteFlag = true;
    }
    else
    {
        bspBme280.sensorDataWriteFlag = false;
    }
}

static void Bsp_Bme280_Read_Cb(bdi_i2c_op_result_t res, void *userData)
{
    if (BDI_I2C_RES_DONE == res)
    {
        bspBme280.sensorDataReadFlag = true;
    }
    else
    {
        bspBme280.sensorDataReadFlag = false;
    }
}

/*!
 * I2C read function map to ESP32S3 platform
 */
static BME280_INTF_RET_TYPE bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr)
{
    EUNUSED(intf_ptr);
    res_t res;
    BME280_INTF_RET_TYPE ret;
    TickType_t startReadTick = 0U;

    bspBme280.sensorDataReadFlag = false;
    bspBme280.readTimeoutFlag = false;

    res = Bdi_I2c_ReadData(BSP_BME280_I2C_ADDR, reg_addr, 1U, reg_data, length, Bsp_Bme280_Read_Cb);

    startReadTick = xTaskGetTickCount();
    while ((RES_OK == res) && (false == bspBme280.sensorDataReadFlag) && (false == bspBme280.readTimeoutFlag))
    {
        vTaskDelay(pdMS_TO_TICKS(20U));
        if ((xTaskGetTickCount() - startReadTick) >= pdMS_TO_TICKS(BSP_BME280_DATAREAD_TIMEOUT_MS))
        {
            bspBme280.readTimeoutFlag = true;
            res = RES_ERROR_TIMEOUT;
            ret = BME280_E_COMM_FAIL;
            ESP_LOGE(TAG, "Timeout reading BME280 data");
        }
    }

    if(RES_OK == res)
    {
        ret = BME280_OK;
    }

    return ret;
}

/*!
 * I2C write function map to ESP32S3 platform
 */
static BME280_INTF_RET_TYPE bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr)
{
    EUNUSED(intf_ptr);
    res_t res;
    BME280_INTF_RET_TYPE ret;
    TickType_t startWriteTick = 0U;

    ESP_LOGD(TAG, "reg_addr:%" PRIu8 ", reg_data=%" PRIu8 ", length=%" PRIu32 "", reg_addr, *reg_data, length);

    bspBme280.sensorDataWriteFlag = false;
    bspBme280.writeTimeoutFlag = false;

    res = Bdi_I2c_WriteData(BSP_BME280_I2C_ADDR, reg_addr, 1U, reg_data, length, Bsp_Bme280_Write_Cb);

    startWriteTick = xTaskGetTickCount();
    while ((RES_OK == res) && (false == bspBme280.sensorDataWriteFlag) && (false == bspBme280.writeTimeoutFlag))
    {
        vTaskDelay(pdMS_TO_TICKS(20U));
        if ((xTaskGetTickCount() - startWriteTick) >= pdMS_TO_TICKS(BSP_BME280_DATAREAD_TIMEOUT_MS))
        {
            bspBme280.writeTimeoutFlag = true;
            res = RES_ERROR_TIMEOUT;
            ret = BME280_E_COMM_FAIL;
            ESP_LOGE(TAG, "Timeout writing BME280 data");
        }
    }

    if(RES_OK == res)
    {
        ret = BME280_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Esp_err:%d", res);
        ret = BME280_E_NULL_PTR;
    }

    return ret;
}

/*!
 * SPI read function map to ESP32S3 platform
 */
BME280_INTF_RET_TYPE bme280_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t length, void *intf_ptr)
{

    return BME280_E_DEV_NOT_FOUND;
}

/*!
 * SPI write function map to ESP32S3 platform
 */
BME280_INTF_RET_TYPE bme280_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t length, void *intf_ptr)
{
    return BME280_E_DEV_NOT_FOUND;
}

/*!
 * Delay function map to ESP32S3 platform
 */
static void bme280_delay_us(uint32_t period, void *intf_ptr)
{
    EUNUSED(intf_ptr);
    uint32_t period_ms = (period/1000UL) + 1UL;
    ESP_LOGD(TAG, "Waiting for %"PRIu32" us (%"PRIu32" ms) delay", period, period_ms);
    vTaskDelay(pdMS_TO_TICKS(period_ms));
}

/*!
 *  @brief Prints the execution status of the APIs.
 */
static void bme280_error_codes_print_result(const char api_name[], int8_t rslt)
{
    if (rslt == BME280_OK)
    {
        ESP_LOGD(TAG, "%s OK\t", api_name);
    }
    else if(rslt != BME280_OK)
    {
        ESP_LOGE(TAG, "%s\t", api_name);


        switch (rslt)
        {
            case BME280_E_NULL_PTR:
            {
                ESP_LOGE(TAG, "Error [%d] : Null pointer error.", rslt);
                ESP_LOGE(TAG, "It occurs when the user tries to assign value (not address) to a pointer, which has been initialized to NULL.");
                break;
            }

            case BME280_E_COMM_FAIL:
            {
                ESP_LOGE(TAG, "Error [%d] : Communication failure error.", rslt);
                ESP_LOGE(TAG, "It occurs due to read/write operation failure and also due to power failure during communication");
                break;
            }
        
            case BME280_E_DEV_NOT_FOUND:
            {
                ESP_LOGE(TAG, "Error [%d] : Device not found error. It occurs when the device chip id is incorrectly read", rslt);
                break;
            }
            case BME280_E_INVALID_LEN:
            {
                ESP_LOGE(TAG, "Error [%d] : Invalid length error. It occurs when write is done with invalid length", rslt);
                break;
            }
            default:
            {
                ESP_LOGE(TAG, "Error [%d] : Unknown error code", rslt);
                break;
            }
        }
    }
}

/*!
 *  @brief Function to select the interface between SPI and I2C.
 */
static int8_t bme280_interface_selection(struct bme280_dev *dev, uint8_t intf)
{
    int8_t rslt = BME280_OK;
    res_t res;
    static uint8_t devAddr = BME280_I2C_ADDR_PRIM;

    if (NULL != dev)
    {
        /* I2C */
        if (BME280_I2C_INTF == intf)
        {
            res = Bdi_I2c_Init();

            if ((RES_OK == res) || (RES_ALREADY_INITIALIZED == res))
            {
                res = Bdi_I2c_Start();
            }

            if (RES_OK == res)
            {
                devAddr = BME280_I2C_ADDR_PRIM;
                dev->read = bme280_i2c_read;
                dev->write = bme280_i2c_write;
                dev->intf = BME280_I2C_INTF;
                bspBme280.interfaceReady = true;

                rslt = BME280_OK;

            }
            else
            {
                rslt = BME280_E_NULL_PTR;
            }
        }
        /* SPI */
        else if (BME280_I2C_INTF == intf)
        {
            rslt = BME280_E_DEV_NOT_FOUND;
        }
        else
        {
            rslt = BME280_E_DEV_NOT_FOUND;
        }
        
        /* Holds the I2C device addr or SPI chip selection */
        dev->intf_ptr = &devAddr;

        /* Configure delay in microseconds */
        dev->delay_us = bme280_delay_us;
    }
    else
    {
        rslt = BME280_E_NULL_PTR;
    }

    return rslt;
}

/*!
 *  @brief Function deinitializes ESP32S3 platform.
 */
static res_t bme280_deinit(void)
{
    res_t res = RES_ERROR;

    res = Bdi_I2c_Stop();

    if (RES_OK == res)
    {
        res = Bdi_I2c_Deinit();
    }

    return res;
}

/*!
 *  @brief This internal API is used to get compensated temperature data using Forced Mode.
 */
static int8_t Get_Temperature_Forced(uint32_t period, struct bme280_dev *dev)
{
    int8_t rslt = BME280_E_NULL_PTR;
    int8_t idx = 0;
    uint8_t status_reg;
    struct bme280_data comp_data;

    while (idx < SAMPLE_COUNT)
    {
        /* Start measurement */
        rslt = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &bspBme280.dev);
        bme280_error_codes_print_result("bme280_set_power_mode", rslt);

        /* Measurement time delay given to read sample */
        dev->delay_us(period, dev->intf_ptr);

        rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, dev);
        bme280_error_codes_print_result("bme280_get_regs", rslt);

        /* Wait until measurement is done (MEAS_DONE bit becomes 0) */
        while (status_reg & BME280_STATUS_MEAS_DONE)
        {
            /* Measurement time delay given to read sample */
            dev->delay_us(period, dev->intf_ptr);

            rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, dev);
            bme280_error_codes_print_result("bme280_get_regs", rslt);
        }

        /* Read compensated data */
        rslt = bme280_get_sensor_data(BME280_TEMP, &comp_data, dev);
        bme280_error_codes_print_result("bme280_get_sensor_data", rslt);

#ifndef BME280_DOUBLE_ENABLE
        bspBme280.temperature = comp_data.temperature / 10;  /* factor 0.1 */
        comp_data.temperature = comp_data.temperature / 100;
#endif

#ifdef BME280_DOUBLE_ENABLE
        ESP_LOGI(TAG, "Temperature[%d]:   %lf deg C\n", idx, comp_data.temperature);
#else
        ESP_LOGD(TAG, "Temperature[%d]:   %ld deg C\n", idx, (long int)comp_data.temperature);
#endif
        idx++;
    }

    return rslt;
}


/*!
 *  @brief This internal API is used to get compensated humidity data using Forced Mode.
 */
static int8_t Get_Humidity_Forced(uint32_t period, struct bme280_dev *dev)
{
    int8_t rslt = BME280_E_NULL_PTR;
    int8_t idx = 0;
    uint8_t status_reg;
    struct bme280_data comp_data;

    while (idx < SAMPLE_COUNT)
    {
        /* Start measurement */
        rslt = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &bspBme280.dev);
        bme280_error_codes_print_result("bme280_set_power_mode", rslt);

        /* Measurement time delay given to read sample */
        dev->delay_us(period, dev->intf_ptr);

        rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, dev);
        bme280_error_codes_print_result("bme280_get_regs", rslt);

        /* Wait until measurement is done (MEAS_DONE bit becomes 0) */
        while (status_reg & BME280_STATUS_MEAS_DONE)
        {
            /* Measurement time delay given to read sample */
            dev->delay_us(period, dev->intf_ptr);

            rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, dev);
            bme280_error_codes_print_result("bme280_get_regs", rslt);
        }

        /* Read compensated data */
        rslt = bme280_get_sensor_data(BME280_HUM, &comp_data, dev);
        bme280_error_codes_print_result("bme280_get_sensor_data", rslt);

#ifndef BME280_DOUBLE_ENABLE
        bspBme280.humidity = comp_data.humidity / 100;  /* factor 0.1 */
        comp_data.humidity = comp_data.humidity / 1000;
#endif

#ifdef BME280_DOUBLE_ENABLE
        ESP_LOGD(TAG, "Humidity[%d]:   %lf %%RH\n", idx, comp_data.humidity);
#else
        ESP_LOGD(TAG, "Humidity[%d]:   %lu %%RH\n", idx, (long unsigned int)comp_data.humidity);
#endif
        idx++;
    }

    return rslt;
}


/*!
 *  @brief This internal API is used to get compensated pressure data using Forced Mode.
 */
static int8_t Get_Pressure_Forced(uint32_t period, struct bme280_dev *dev)
{
    int8_t rslt = BME280_E_NULL_PTR;
    int8_t idx = 0;
    uint8_t status_reg;
    struct bme280_data comp_data;

    while (idx < SAMPLE_COUNT)
    {
        /* Start measurement */
        rslt = bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &bspBme280.dev);
        bme280_error_codes_print_result("bme280_set_power_mode", rslt);

        /* Measurement time delay given to read sample */
        dev->delay_us(period, dev->intf_ptr);

        rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, dev);
        bme280_error_codes_print_result("bme280_get_regs", rslt);

        /* Wait until measurement is done (MEAS_DONE bit becomes 0) */
        while (status_reg & BME280_STATUS_MEAS_DONE)
        {
            /* Measurement time delay given to read sample */
            dev->delay_us(period, dev->intf_ptr);

            rslt = bme280_get_regs(BME280_REG_STATUS, &status_reg, 1, dev);
            bme280_error_codes_print_result("bme280_get_regs", rslt);
        }

        /* Read compensated data */
        rslt = bme280_get_sensor_data(BME280_PRESS, &comp_data, dev);
        bme280_error_codes_print_result("bme280_get_sensor_data", rslt);

#ifdef BME280_64BIT_ENABLE
        comp_data.pressure = comp_data.pressure / 100;
#endif

#ifdef BME280_DOUBLE_ENABLE
        ESP_LOGD(TAG, "Pressure[%d]:  %lf Pa\n", idx, comp_data.pressure);
#else
        bspBme280.pressure = comp_data.pressure;  /* factor 1 */
        ESP_LOGD(TAG, "Pressure[%d]:   %lu Pa\n", idx, (long unsigned int)comp_data.pressure);
#endif
        idx++;
    }

    return rslt;
}
/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/