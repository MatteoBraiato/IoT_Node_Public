/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file bsp_di.c
 * History:
 *  - 22/01/2025  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "bsp_di.h"
#include "driver/gpio.h"
#include "hw_cfg.h"
#include <esp_log.h>
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/

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
} bsp_di_t;

typedef uint16_t bsp_di_gpio_pin_num_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/

/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static bsp_di_t bspDi;

static const bsp_di_gpio_pin_num_t bspDi_gpioPinNum[BSP_DI_IN_NUM] =
{
	HW_CFG_BSP_DI_CHG_GPIO
};

static const char* TAG = "Bsp_DI";
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t Bsp_Di_InitPin(bsp_di_pin_t pinNum, bsp_di_pin_pull_type_t pullType)
{
	res_t res = RES_OK;
	gpio_pulldown_t pullDownCfg = GPIO_PULLDOWN_DISABLE;
	gpio_pullup_t pullUpCfg = GPIO_PULLUP_DISABLE;
	esp_err_t esp_res = ESP_OK;

	/* Check if already initialized */
	if (true == bspDi.init)
	{
		ESP_LOGE(TAG, "Init Error - Already initialized");
		res = RES_ALREADY_INITIALIZED;
	}
	/* Check pin number validity */
	else if (pinNum >= BSP_DI_IN_NUM)
	{
		ESP_LOGE(TAG, "Init Error - Invalid pin number: %d", pinNum);
		res = RES_ERROR_INVALID_PARAMETER;
	}
	/* Configure pull resistors based on pull type */
	else if (pullType == BSP_DI_PULLUP)
	{
		pullUpCfg = GPIO_PULLUP_ENABLE;
		res = RES_OK;
	}
	else if (pullType == BSP_DI_PULLDOWN)
	{
		pullDownCfg = GPIO_PULLDOWN_ENABLE;
		res = RES_OK;
	}
	else
	{
		ESP_LOGE(TAG, "Init Error - Invalid pull type: %d", pullType);
		res = RES_ERROR_INVALID_PARAMETER;
	}

	/* Configure GPIO if no errors detected */
	if (RES_OK == res)
	{
		gpio_config_t gpioConfig = {};
		gpioConfig.intr_type = GPIO_INTR_DISABLE;
		gpioConfig.mode = GPIO_MODE_INPUT;
		gpioConfig.pull_down_en = pullDownCfg;
		gpioConfig.pull_up_en = pullUpCfg;
		gpioConfig.pin_bit_mask = (1UL << bspDi_gpioPinNum[pinNum]);

		esp_res = gpio_config(&gpioConfig);
		if (ESP_OK != esp_res)
		{
			ESP_LOGE(TAG, "Init Error - gpio_config failed with error code: %d", esp_res);
			res = RES_ERROR;
		}
		else
		{
			bspDi.init = true;
			ESP_LOGI(TAG, "GPIO pin %d initialized successfully with pull type %d", 
					 bspDi_gpioPinNum[pinNum], pullType);
		}
	}

	return res;
}
res_t Bsp_Di_GetState(bsp_di_pin_t pinNum, bsp_di_pin_status_t *pinStatus)
{
	res_t res = RES_ERROR_UNINITIALIZED;
	bsp_di_pin_status_t status = BSP_DI_LOW;
	if (true == bspDi.init)
	{
		uint32_t val;
		val = gpio_get_level(bspDi_gpioPinNum[pinNum]);
		if (val > 0UL)
		{
			status = BSP_DI_HIGH;
		}
		else
		{
			status = BSP_DI_LOW;
		}
		res = RES_OK;
	}
	else
	{
		ESP_LOGE(TAG, "Uninitialized");
	}

	*pinStatus = status;
	return res;
}
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/

/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/