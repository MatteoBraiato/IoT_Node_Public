/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file bsp_max1704.c
 * History:
 *  - 30/12/2025  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "bsp_max1704.h"
#include <types/types.h>
#include <esp_log.h>
#include <bsp/hw_cfg.h>
#include <bdi/bdi_i2c.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
#define BSP_MAX1704_I2C_ADDR            (0x36U)
#define BSP_MAX1704_REG_VCELL           (0x02U)
#define BSP_MAX1704_REG_SOC             (0x04U)
#define BSP_MAX1704_REG_MODE            (0x06U)
#define BSP_MAX1704_REG_VERSION         (0x08U)
#define BSP_MAX1704_REG_CONFIG          (0x0CU)
#define BSP_MAX1704_REG_CRATE           (0x16U)
#define BSP_MAX1704_REG_STATUS          (0x1AU)

#define BSP_MAX1704_QUICKSTART_MODE     (0x4000U)
#define BSP_MAX1704_I2C_TIMEOUT_MS      (1000UL)
#define BSP_MAX1704_MV_PRECISION        (0.078125f)  // MAX17048/MAX17049 precision

/**************************************************************************************************
 *  3. PRIVATE MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4. PRIVATE ENUMERATIONS                                                                       *
 **************************************************************************************************/

/**************************************************************************************************
 *  5. PRIVATE STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/

/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/
typedef struct
{
	bool_t init;
	bool_t i2c_read_done;
	bool_t i2c_write_done;
	bool_t i2c_timeout;
    
    uint16_t version;
    float voltage;
    float soc_percent;
    float rate_change;
    
    uint8_t reg_buffer[2];

} bsp_max1704_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static void Bsp_Max1704_I2c_Read_Cb(bdi_i2c_op_result_t res, void *userData);
static void Bsp_Max1704_I2c_Write_Cb(bdi_i2c_op_result_t res, void *userData);
static res_t Bsp_Max1704_Read_Register(uint8_t reg_addr, uint8_t *data, uint16_t len);
static res_t Bsp_Max1704_Write_Register(uint8_t reg_addr, uint8_t *data, uint16_t len);
static res_t Bsp_Max1704_Quickstart(void);
static res_t Bsp_Max1704_Get_Version(void);
static res_t Bsp_Max1704_Get_Config(void);
static res_t Bsp_Max1704_Get_Status(void);
static res_t Bsp_Max1704_Set_Status(void);

/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static bsp_max1704_t bspMax1704;

static const char* TAG = "BSP_MAX1704";
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t Bsp_Max1704_Init(void)
{
    res_t res = RES_ERROR;

    if (bspMax1704.init == true)
    {
        res = RES_ALREADY_INITIALIZED;
    }
    else
    {
        /* Initialize all fields */
        bspMax1704.i2c_read_done = false;
        bspMax1704.i2c_write_done = false;
        bspMax1704.i2c_timeout = false;
        bspMax1704.version = 0;
        bspMax1704.voltage = 0;
        bspMax1704.soc_percent = 0;
        bspMax1704.rate_change = 0;
        memset(bspMax1704.reg_buffer, 0, sizeof(bspMax1704.reg_buffer));

        /* Quickstart the MAX1704x device */
        // res = Bsp_Max1704_Quickstart();
        // if (RES_OK != res)
        // {
        //     ESP_LOGE(TAG, "Failed to quickstart MAX1704x");
        // }

        /* Get and log MAX1704x version */
        // if (RES_OK == res)
        // {
        //     res = Bsp_Max1704_Get_Version();
        //     if (RES_OK == res)
        //     {
        //         ESP_LOGD(TAG, "MAX1704x version: 0x%04X", bspMax1704.version);
        //     }
        //     else
        //     {
        //         ESP_LOGE(TAG, "Failed to get MAX1704x version");
        //     }
        // }

        /* Get and log MAX1704x configuration */
        // if (RES_OK == res)
        // {
        //     res = Bsp_Max1704_Get_Config();
        //     if (RES_OK == res)
        //     {
        //         ESP_LOGD(TAG, "--- MAX1704X config register ---");
        //         ESP_LOGD(TAG, "Configuration retrieved successfully");
        //         ESP_LOGD(TAG, "--- End Configuration ---\n");
        //     }
        //     else
        //     {
        //         ESP_LOGE(TAG, "Failed to get MAX1704x config");
        //     }
        // }

        /* Get and log MAX1704x status */
        // if (RES_OK == res)
        // {
        //     res = Bsp_Max1704_Get_Status();
        //     if (RES_OK == res)
        //     {
        //         ESP_LOGD(TAG, "--- MAX1704X status register ---");
        //         ESP_LOGD(TAG, "Status retrieved successfully");
        //         ESP_LOGD(TAG, "--- End Status ---\n");
        //     }
        //     else
        //     {
        //         ESP_LOGE(TAG, "Failed to get MAX1704x status");
        //     }
        // }

        /* Set status to clear reset indicator */
        // if (RES_OK == res)
        // {
        //     ESP_LOGD(TAG, "Setting the status register to clear reset indicator");
        //     res = Bsp_Max1704_Set_Status();
        //     if (RES_OK == res)
        //     {
        //         ESP_LOGD(TAG, "Status register updated successfully");
        //         ESP_LOGD(TAG, "--- MAX1704X status register after updating status register ---");
                
        //         res = Bsp_Max1704_Get_Status();
        //         if (RES_OK == res)
        //         {
        //             ESP_LOGD(TAG, "Status retrieved after update");
        //             ESP_LOGD(TAG, "--- End Status ---\n");
        //         }
        //     }
        //     else
        //     {
        //         ESP_LOGE(TAG, "Failed to set MAX1704x status");
        //     }
        // }

        res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_SOC, bspMax1704.reg_buffer, 1);
        memset(bspMax1704.reg_buffer, 0, sizeof(bspMax1704.reg_buffer));

        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "Failed to read data MAX1704x");
        }

        if (RES_OK == res)
        {
            bspMax1704.init = true;
        }
        //bspMax1704.init = true;     // TODO: Remove this line after testing, as it forces init to true even if errors occurred.
    }

    return res;
}

res_t Bsp_Max1704_Deinit(void)
{
    res_t res = RES_ERROR_NOT_IMPLEMENTED;

    if (false == bspMax1704.init)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else
    {
        /* Reset all fields */
        bspMax1704.i2c_read_done = false;
        bspMax1704.i2c_write_done = false;
        bspMax1704.i2c_timeout = false;
        bspMax1704.version = 0;
        bspMax1704.voltage = 0;
        bspMax1704.soc_percent = 0;
        bspMax1704.rate_change = 0;
        memset(bspMax1704.reg_buffer, 0, sizeof(bspMax1704.reg_buffer));

        bspMax1704.init = false;
        res = RES_OK;
    }

    return res;
}
res_t Bsp_Max1704_Read_Soc(uint8_t *soc_prcnt)
{
    res_t res = RES_ERROR;

    if (bspMax1704.init == false)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else if (NULL == soc_prcnt)
    {
        res = RES_ERROR;
        ESP_LOGE(TAG, "Invalid argument: soc_prcnt is NULL");
    }
    else
    {
        /* Read only the high byte (MSB) of SOC register which contains the integer percentage */
        res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_SOC, bspMax1704.reg_buffer, 1);
        
        if (RES_OK == res)
        {
            /* High byte contains the integer SOC percentage directly */
            *soc_prcnt = bspMax1704.reg_buffer[0];
            
            if (*soc_prcnt > 100U)
            {
                *soc_prcnt = 100U;
            }
            
            ESP_LOGD(TAG, "SOC: %u%%", *soc_prcnt);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read SOC percent");
            res = RES_ERROR;
        }
    }

    return res;
}

res_t Bsp_Max1704_Read_Voltage(uint16_t *voltage_cV)
{
    res_t res = RES_ERROR;

    if (bspMax1704.init == false)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else if (NULL == voltage_cV)
    {
        res = RES_ERROR;
        ESP_LOGE(TAG, "Invalid argument: voltage_cV is NULL");
    }
    else
    {
        res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_VCELL, bspMax1704.reg_buffer, 2);

        if (RES_OK == res)
        {
            /* Voltage is calculated from 16-bit value with 0.078125 mV precision per bit */
            uint16_t adc_val = ((uint16_t)bspMax1704.reg_buffer[0] << 8) | bspMax1704.reg_buffer[1];
            bspMax1704.voltage = (float)adc_val * BSP_MAX1704_MV_PRECISION / 1000.0f;
            
            if (bspMax1704.voltage < 65.535f)
            {
                /* Return voltage in centivolt (0.01V precision) for 2 decimal places */
                *voltage_cV = (uint16_t)(bspMax1704.voltage * 100);
            }
            else
            {
                /* voltage outside range */
                *voltage_cV = 65535U;
            }
            
            ESP_LOGD(TAG, "Voltage: %.3f V (raw: 0x%04X)", bspMax1704.voltage, adc_val);
            res = RES_OK;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read voltage");
            res = RES_ERROR;
        }
    }

    return res;
}

res_t Bsp_Max1704_Read_CRate(uint8_t *cRate_percent)
{
    res_t res = RES_ERROR;

    if (bspMax1704.init == false)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else if (NULL == cRate_percent)
    {
        res = RES_ERROR;
        ESP_LOGE(TAG, "Invalid argument: cRate_percent is NULL");
    }
    else
    {
        res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_CRATE, bspMax1704.reg_buffer, 2);

        if (RES_OK == res)
        {
            /* C-Rate is a signed 16-bit value with 0.208% precision per LSB */
            int16_t crate_raw = ((int16_t)bspMax1704.reg_buffer[0] << 8) | bspMax1704.reg_buffer[1];
            bspMax1704.rate_change = (float)crate_raw * 0.208f;
            
            *cRate_percent = (uint8_t)bspMax1704.rate_change;
            ESP_LOGD(TAG, "C-Rate: %.2f%% (raw: 0x%04X)", bspMax1704.rate_change, (uint16_t)crate_raw);
            res = RES_OK;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read C-Rate");
            res = RES_ERROR;
        }
    }

    return res;
}
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/
/**
 * @brief I2C read callback - sets flag when operation completes
 */
static void Bsp_Max1704_I2c_Read_Cb(bdi_i2c_op_result_t res, void *userData)
{
    if (BDI_I2C_RES_DONE == res)
    {
        bspMax1704.i2c_read_done = true;
    }
    else
    {
        bspMax1704.i2c_read_done = false;
        ESP_LOGE(TAG, "I2C read operation failed");
    }
}

/**
 * @brief I2C write callback - sets flag when operation completes
 */
static void Bsp_Max1704_I2c_Write_Cb(bdi_i2c_op_result_t res, void *userData)
{
    if (BDI_I2C_RES_DONE == res)
    {
        bspMax1704.i2c_write_done = true;
    }
    else
    {
        bspMax1704.i2c_write_done = false;
        ESP_LOGE(TAG, "I2C write operation failed");
    }
}

/**
 * @brief Synchronous I2C read register - blocks until data received or timeout
 */
static res_t Bsp_Max1704_Read_Register(uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    res_t res = RES_ERROR;
    uint16_t start_time_ms;
    uint16_t current_time_ms;

    if (NULL == data || len == 0)
    {
        return RES_ERROR;
    }

    bspMax1704.i2c_read_done = false;
    bspMax1704.i2c_timeout = false;

    res = Bdi_I2c_ReadData(BSP_MAX1704_I2C_ADDR, reg_addr, 1U, data, len, Bsp_Max1704_I2c_Read_Cb);
    
    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "Failed to queue I2C read operation");
        return res;
    }

    start_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    /* Wait for I2C operation to complete or timeout */
    while ((false == bspMax1704.i2c_read_done) && (false == bspMax1704.i2c_timeout))
    {
        vTaskDelay(pdMS_TO_TICKS(10U));
        current_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if ((current_time_ms - start_time_ms) > BSP_MAX1704_I2C_TIMEOUT_MS)
        {
            bspMax1704.i2c_timeout = true;
            res = RES_ERROR_TIMEOUT;
            ESP_LOGE(TAG, "Timeout reading MAX1704 register 0x%02X", reg_addr);
        }
    }

    if (true == bspMax1704.i2c_read_done)
    {
        res = RES_OK;
    }

    return res;
}

/**
 * @brief Synchronous I2C write register - blocks until data sent or timeout
 */
static res_t Bsp_Max1704_Write_Register(uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    res_t res = RES_ERROR;
    uint16_t start_time_ms;
    uint16_t current_time_ms;

    if (NULL == data || len == 0)
    {
        return RES_ERROR;
    }

    bspMax1704.i2c_write_done = false;
    bspMax1704.i2c_timeout = false;

    res = Bdi_I2c_WriteData(BSP_MAX1704_I2C_ADDR, reg_addr, 1U, data, len, Bsp_Max1704_I2c_Write_Cb);
    
    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "Failed to queue I2C write operation");
        return res;
    }

    start_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    /* Wait for I2C operation to complete or timeout */
    while ((false == bspMax1704.i2c_write_done) && (false == bspMax1704.i2c_timeout))
    {
        vTaskDelay(pdMS_TO_TICKS(10U));
        current_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if ((current_time_ms - start_time_ms) > BSP_MAX1704_I2C_TIMEOUT_MS)
        {
            bspMax1704.i2c_timeout = true;
            res = RES_ERROR_TIMEOUT;
            ESP_LOGE(TAG, "Timeout writing MAX1704 register 0x%02X", reg_addr);
        }
    }

    if (true == bspMax1704.i2c_write_done)
    {
        res = RES_OK;
    }

    return res;
}

/**
 * @brief Quickstart the MAX1704x device
 */
static res_t Bsp_Max1704_Quickstart(void)
{
    uint8_t data[2] = {0x40, 0x00};
    return Bsp_Max1704_Write_Register(BSP_MAX1704_REG_MODE, data, 2);
}

/**
 * @brief Get MAX1704x version
 */
static res_t Bsp_Max1704_Get_Version(void)
{
    res_t res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_VERSION, bspMax1704.reg_buffer, 2);
    
    if (RES_OK == res)
    {
        bspMax1704.version = ((uint16_t)bspMax1704.reg_buffer[0] << 8) | bspMax1704.reg_buffer[1];
    }
    
    return res;
}

/**
 * @brief Get MAX1704x configuration register
 */
static res_t Bsp_Max1704_Get_Config(void)
{
    res_t res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_CONFIG, bspMax1704.reg_buffer, 2);
    
    if (RES_OK == res)
    {
        ESP_LOGD(TAG, "Config register: 0x%02X%02X", bspMax1704.reg_buffer[0], bspMax1704.reg_buffer[1]);
    }
    
    return res;
}

/**
 * @brief Get MAX1704x status register
 */
static res_t Bsp_Max1704_Get_Status(void)
{
    res_t res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_STATUS, bspMax1704.reg_buffer, 2);
    
    if (RES_OK == res)
    {
        ESP_LOGD(TAG, "Status register: 0x%02X%02X", bspMax1704.reg_buffer[0], bspMax1704.reg_buffer[1]);
    }
    
    return res;
}

/**
 * @brief Set MAX1704x status register (e.g., to clear reset indicator)
 */
static res_t Bsp_Max1704_Set_Status(void)
{
    /* Read current status, clear reset indicator bit, and write back */
    res_t res = Bsp_Max1704_Read_Register(BSP_MAX1704_REG_STATUS, bspMax1704.reg_buffer, 2);
    
    if (RES_OK == res)
    {
        /* Clear reset indicator (bit 0) */
        bspMax1704.reg_buffer[0] &= ~(1U << 0);
        res = Bsp_Max1704_Write_Register(BSP_MAX1704_REG_STATUS, bspMax1704.reg_buffer, 2);
    }
    
    return res;
}