/**************************************************************************************************
 * Copyright (c) 2023 - Matteo Braiato - All rights reserved.                                    *
 **************************************************************************************************/
/* @file bsp_adc.c
 * History:
 *  - 18 gen 2023  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "bsp_adc.h"
//#include "bsp_do.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_err.h"
//#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <pthread.h>
#include "freertos/semphr.h"
//#include <perfcnt/perfcnt.h>
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
// CONFIGURABLE CHANNELS
#define BSP_ADC_IN0_GPIO_PIN			(8UL)
#define BSP_ADC_IN0_CHANNEL				(ADC_CHANNEL_7)
#define BSP_ADC_IN0_ADC_UNIT			(ADC_UNIT_1)
#define BSP_ADC_IN0_CH_CFG				(BPS_ADC_CH_CFG_DEVBOARD)

#define BSP_ADC_IN1_GPIO_PIN			(3UL)
#define BSP_ADC_IN1_CHANNEL				(ADC_CHANNEL_2)
#define BSP_ADC_IN1_ADC_UNIT			(ADC_UNIT_1)
#define BSP_ADC_IN1_CH_CFG				(BPS_ADC_CH_CFG_DEVBOARD)

#define BSP_ADC_IN2_GPIO_PIN			(9UL)
#define BSP_ADC_IN2_CHANNEL				(ADC_CHANNEL_8)
#define BSP_ADC_IN2_ADC_UNIT			(ADC_UNIT_1)
#define BSP_ADC_IN2_CH_CFG				(BPS_ADC_CH_CFG_DEVBOARD)

#define BSP_ADC_CHANNEL_MAX				(10U)

#define BSP_ADC_VMAX_MV					(3151UL)
#define BSP_BOARD_VMAX_MV				(3300UL)

// Scaling parameters
#define BSP_ADC_CALC_5V_NUM				(402UL)		// 40K2
#define BSP_ADC_CALC_5V_DEN				(672UL)		// 40K2 + 27K
#define BSP_ADC_CALC_12V_NUM			(1UL)		// TODO
#define BSP_ADC_CALC_12V_DEN			(1UL)		// TODO
#define BSP_ADC_CALC_30V_NUM			(27UL)		// 2K7
#define BSP_ADC_CALC_30V_DEN			(247UL)		// 2K7 + 22K
#define BSP_ADC_CALC_CS_NUM				(1UL)		// TODO
#define BSP_ADC_CALC_CS_DEN				(1UL)		// TODO
#define BSP_ADC_CALC_NOCONV_NUM			(1UL)		// TODO
#define BSP_ADC_CALC_NOCONV_DEN			(1UL)		// TODO
#define BSP_ADC_CALC_DEVBOARD_NUM		(BSP_ADC_VMAX_MV)
#define BSP_ADC_CALC_DEVBOARD_DEN		(BSP_BOARD_VMAX_MV)

#define BSP_ADC_RES_BUFF_CNT            (2UL)

#define GET_UNIT(x)        				((x>>3) & 0x1)		// resituisce ADC unit

#define BSP_ADC_ALFA					(0.8)		// 0.8

#define BSP_ADC_TASK_PERIOD_MS			(1U)

#define BSP_ADC_CONV_LEN                (12UL) /*4 byte per canale. 3x4= 12Byte*/

/* Configurazione ADC
 * BSP_ADC_FREQ_HZ frequenza di conversione. Ho la conversione di un canale ogni 1/BSP_ADC_FREQ_HZ.
 * Frequenza minima 611Hz, frequenza massima 83333Hz.
 * ci sono 3 canali sempre attivi KL30, KL15 e CS delle uscite.
 * ci sono 3 canali che possono essere attivati o meno dall'applicazione.
 * Viene generato un interrupt alla fine della conversione di tutti i canali attivi, minimo 3 canali massimo 6.
 * Tempo di interrupt minimo 3 * periodo di campionamento, massimo 6 per periodo di campionamento.
 *
 * Per i canali CS ho un valore valido ogni 4 tempi di interrupt.
 * 1) leggo canale A e configuro la lettura per il canale B 2) ignoro i valori 3) leggo canale B e configuro per il canale A 4) ignoro i valori
 *
 * Nella seguente tabella sono indicati i tempi di interrupt in ms
 *
 *            |   FREQUENZA DI CAMPIONAMETO Hz  |
 * Ch. Active | 500 | 1000 | 1500 | 2000 | 5000 |
 * ----------------------------------------------
 *      3     |  6  |   3  |  2   |  1.5 |  0.6 |
 *  ---------------------------------------------
 *      4     |  8  |   4  |  2.6 |  2.0 |  0.8 |
 *  ---------------------------------------------
 *      5     | 10  |   5  |  3.3 |  2.5 |  1.0 |
 *  ---------------------------------------------
 *      6     | 12  |   6  |  4   |  3.0 |  1.2 |
 * ----------------------------------------------
 */
#define BSP_ADC_FREQ_HZ					(1000UL)
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
	    uint8_t enabledCh_num;
	    uint16_t freq;
	    bool_t run;

	    adc_continuous_handle_t handle;

	    uint32_t adcLenRequested;
	    uint8_t buffIndex;
		uint8_t result[BSP_ADC_CONV_LEN];
	    uint16_t valRaw[BSP_ADC_CH_ID_NUM];
	    pthread_mutex_t mutex;
	    adc_cali_handle_t caliHandle;
	    //perfcnt_t perfCnt;
	    //perfcnt_t perfCntCyclic;
	    //perfcnt_t perfCntOverflow;
} bsp_adc_t;

typedef enum
{
    BSP_ADC_CH_CFG_5V = 0,
    BSP_ADC_CH_CFG_12V,
    BSP_ADC_CH_CFG_30V,
    BPS_ADC_CH_CFG_CS,
    BPS_ADC_CH_CFG_NOCONFV,
    BPS_ADC_CH_CFG_DEVBOARD,
    BSP_ADC_CH_CFG_NUM
} bsp_adc_ch_cfg;

typedef struct
{
    uint16_t gpioPin;
    adc_channel_t channel;
    adc_unit_t adcUnit;
} bsp_adc_hw_t;

typedef struct
{
    uint32_t num;
    uint32_t den;
} bsp_adc_calc_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static bool Bsp_Adc_EventCb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data);
//static bool Bsp_Adc_OverflowCb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data);
/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static bsp_adc_t bspAdc;
static const bsp_adc_hw_t bspAdcHw[] =
{
		{ BSP_ADC_IN0_GPIO_PIN   , BSP_ADC_IN0_CHANNEL   , BSP_ADC_IN0_ADC_UNIT},
		{ BSP_ADC_IN1_GPIO_PIN   , BSP_ADC_IN1_CHANNEL   , BSP_ADC_IN1_ADC_UNIT},
		{ BSP_ADC_IN2_GPIO_PIN   , BSP_ADC_IN2_CHANNEL   , BSP_ADC_IN2_ADC_UNIT},
};

static bsp_adc_ch_id_t bspAdcChMap[BSP_ADC_CHANNEL_MAX] =
{
		BSP_ADC_CH_ID_NUM,		// ch0 : no input associated
		BSP_ADC_CH_ID_NUM,		// ch1 : no input associated
		BSP_ADC_IN1,			// ch2
		BSP_ADC_CH_ID_NUM,      // ch3 : no input associated
		BSP_ADC_CH_ID_NUM,      // ch4 : no input associated
		BSP_ADC_CH_ID_NUM,      // ch5 : no input associated
		BSP_ADC_CH_ID_NUM,      // ch6 : no input associated
		BSP_ADC_IN0,            // ch7
		BSP_ADC_IN2,		    // ch8
		BSP_ADC_CH_ID_NUM		// ch9 : no input associated
};

static const bsp_adc_calc_t bspAdcCalc[BSP_ADC_CH_ID_NUM] =
{
	[BSP_ADC_IN0] = 	{ BSP_ADC_CALC_DEVBOARD_NUM , BSP_ADC_CALC_DEVBOARD_DEN  },
	[BSP_ADC_IN1] = 	{ BSP_ADC_CALC_DEVBOARD_NUM , BSP_ADC_CALC_DEVBOARD_DEN  },
	[BSP_ADC_IN2] = 	{ BSP_ADC_CALC_DEVBOARD_NUM , BSP_ADC_CALC_DEVBOARD_DEN  }
};
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t Bsp_Adc_Init(const bsp_adc_ch_en adcChEn[BSP_ADC_CFGBL_CH_NUM])
{
	res_t res = RES_ERROR;
	esp_err_t ret;

	if (false == bspAdc.init)
	{
        uint32_t adc1ChanMask = 0x0UL;
        uint32_t adc2ChanMask = 0x0UL;
        bspAdc.enabledCh_num = 0;
        // CONFIGURABLE CHANNELS
        for (uint8_t idx = 0; idx < BSP_ADC_CFGBL_CH_NUM; idx++)
        {
            if (true == adcChEn[idx])
            {
                bspAdc.enabledCh_num++;
                ESP_LOGD("ADC", "Init ch:%d OK", bspAdcHw[idx].channel);
                if (ADC_UNIT_1 == bspAdcHw[idx].adcUnit)
                {
                    adc1ChanMask |= (1UL << (bspAdcHw[idx].channel));
                }
                else if (ADC_UNIT_2 == bspAdcHw[idx].adcUnit)
                {
                    adc2ChanMask |= (1UL << (bspAdcHw[idx].channel));
                }
                else
                {
                    // do nothing
                }
            }
            else
            {
                // do nothing
            }
        }

        bspAdc.adcLenRequested = BSP_ADC_CONV_LEN; //SOC_ADC_DIGI_RESULT_BYTES * (bspAdc.enabledCh_num + 3U); // Configurable channel + 3 fixed channel
	    adc_continuous_handle_cfg_t adc_config = {
	        .max_store_buf_size = 1024,
	        .conv_frame_size = bspAdc.adcLenRequested,
	    };
	    ret = adc_continuous_new_handle(&adc_config, &bspAdc.handle);

	    if (ESP_OK == ret)
	    {
	    	ESP_LOGD("ADC", "Handle OK");
	    }
	    else
	    {
	    	ESP_LOGE("ADC", "Handle error");
	    }

	    adc_digi_convert_mode_t bspAdc_convMode;
		if ((adc1ChanMask & adc2ChanMask) != 0UL)
		{
			bspAdc_convMode = ADC_CONV_BOTH_UNIT;
			ESP_LOGD("ADC", "Using ADC1 and ADC2");
		}
		else if (adc1ChanMask != 0x0UL)
		{
			bspAdc_convMode = ADC_CONV_SINGLE_UNIT_1;
			ESP_LOGD("ADC", "Using ADC1");
		}
		else if (adc2ChanMask != 0x0UL)
		{
			bspAdc_convMode = ADC_CONV_SINGLE_UNIT_2;
			ESP_LOGD("ADC", "Using ADC2");
		}
		else
		{
			bspAdc_convMode = ADC_CONV_BOTH_UNIT; // da inizializzare per evitare warning
		}

	    adc_digi_pattern_config_t adcPattern[SOC_ADC_PATT_LEN_MAX] = { 0U };
	    uint8_t adcPatternIdx = 0U;
	    // CONFIGURABLE CHANNELS
	    for (uint8_t idx = 0; idx < BSP_ADC_CFGBL_CH_NUM; idx++)
	    {
	    	if (true == adcChEn[idx])
	    	{
	    		adcPattern[adcPatternIdx].atten = ADC_ATTEN_DB_12;
	    		adcPattern[adcPatternIdx].channel = bspAdcHw[idx].channel & 0x7;
	    		adcPattern[adcPatternIdx].unit = GET_UNIT(bspAdcHw[idx].channel);
	    		adcPattern[adcPatternIdx].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

	    		ESP_LOGD("ADC", "adc_pattern[%d].atten is :%d", adcPatternIdx, adcPattern[adcPatternIdx].atten);
	    		ESP_LOGD("ADC", "adc_pattern[%d].channel is :%d", adcPatternIdx, adcPattern[adcPatternIdx].channel);
	    		ESP_LOGD("ADC", "adc_pattern[%d].unit is :%d", adcPatternIdx, adcPattern[adcPatternIdx].unit);
	    		ESP_LOGD("ADC", "adc_pattern[%d].bit_width is :%d", adcPatternIdx, adcPattern[adcPatternIdx].bit_width);

	    		adcPatternIdx++;
	    	}
	    	else
	    	{
	    		// do nothing
	    	}
	    }

	    adc_continuous_config_t dig_cfg =
	    {
	    		.adc_pattern = adcPattern,
	    		.pattern_num = adcPatternIdx,
				.sample_freq_hz = BSP_ADC_FREQ_HZ,
				.conv_mode = bspAdc_convMode,
				.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
	    };

	    ret = adc_continuous_config(bspAdc.handle, &dig_cfg);

	    if (ESP_OK == ret)
	    {
	    	ESP_LOGD("ADC", "Config OK");
	    }
	    else
	    {
	    	ESP_LOGE("ADC", "Config error - %d", ret);
	    }

	    adc_continuous_evt_cbs_t cbs = {
	        .on_conv_done = Bsp_Adc_EventCb
	    };
	    ret = adc_continuous_register_event_callbacks(bspAdc.handle, &cbs, NULL);

	    if (ESP_OK == ret)
	    {
	    	ESP_LOGD("ADC", "Callback registration OK");
	    }
	    else
	    {
	    	ESP_LOGE("ADC", "Callback registration error");
	    }

	    if (0 != pthread_mutex_init(&bspAdc.mutex, NULL))
	    {
	    	ESP_LOGE("ADC", "Init ADC Mutex Error");
	    	res = RES_ERROR;
	    }
	    else
	    {
	    	res = RES_OK;
	    }

	    if(RES_OK == res)
	    {
            // CALIBRATION
            if (ADC_CONV_SINGLE_UNIT_1 == bspAdc_convMode)
            {
                adc_cali_curve_fitting_config_t caliCfg =
                {
                        .unit_id = ADC_UNIT_1,
                        .atten = ADC_ATTEN_DB_12,
                        .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH
                };
                ret = adc_cali_create_scheme_curve_fitting(&caliCfg, &bspAdc.caliHandle);
                if (ESP_OK == ret)
                {
                    res = RES_OK;
                    ESP_LOGD("ADC", "Calibration completed");
                }
                else
                {
                    res = RES_ERROR;
                    ESP_LOGE("ADC", "Calibration error");
                }
            }
            else
            {
                ESP_LOGE("ADC", "Not implemented");
                res = RES_ERROR_NOT_IMPLEMENTED;
            }
	    }
	}
    else
    {
        res = RES_ALREADY_INITIALIZED;
        ESP_LOGE("ADC", "Already Initialized");
    }

	if(RES_OK == res)
	{

        //ESP_LOGI("ADC", "Init ADC OK");
        //Bsp_Do_Set(BSP_DO_CS_SEL, BSP_DO_PIN_LOW);
        bspAdc.init = true;
	    //res = PerfCnt_Init(&bspAdc.perfCnt);
	    //res |= PerfCnt_Init(&bspAdc.perfCntOverflow);
        //res |= PerfCnt_Init(&bspAdc.perfCntCyclic);
	}
	return res;
}

res_t Bsp_Adc_Start(void)
{
	res_t res = RES_ERROR;
	if (true == bspAdc.init)
	{
		if (bspAdc.run != true)
		{
			esp_err_t ret = adc_continuous_start(bspAdc.handle);
			if (ret == ESP_OK)
			{
				bspAdc.run = true;
				res = RES_OK;
				ESP_LOGI("ADC", "Start");
				//xTaskCreatePinnedToCore(Bsp_Adc_Task, "ADC_TASK", 4096, NULL, 15, &bspAdc.taskHandle, tskNO_AFFINITY);
			    //bspAdc.taskHandle = xTaskGetHandle("ADC_TASK");
			}
			else
			{
				res = RES_ERROR;
				ESP_LOGE("ADC", "Start error");
			}
		}
		else
		{
			res = RES_ALREADY_INITIALIZED;
			ESP_LOGW("ADC", "Already running");
		}
	}
	else
	{
        res = RES_ERROR;
        ESP_LOGE("ADC", "Start error");
	}

	return res;
}

res_t Bsp_Adc_Stop(void)
{
	res_t res = RES_ERROR;
	if (true == bspAdc.init)
	{
		if (bspAdc.run == true)
		{
			esp_err_t ret = adc_continuous_stop(bspAdc.handle);
			if (ret == ESP_OK)
			{
				bspAdc.run = false;
				res = RES_OK;
				ESP_LOGI("ADC", "Stop");
			}
		}
	}
    else
    {
        res = RES_ERROR;
        ESP_LOGE("ADC", "Stop Error");
    }
	return res;
}

res_t Bsp_Adc_GetVal_Raw(bsp_adc_ch_id_t chId, uint16_t *adcVal_raw)
{
    res_t res = RES_ERROR;
    if (false == bspAdc.init)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else if ((chId > BSP_ADC_CH_ID_NUM) || (NULL == adcVal_raw))
    {
        res = RES_ERROR_INVALID_PARAMETER;
    }
    else
    {
        if (0 == pthread_mutex_lock(&bspAdc.mutex))
        {
            *adcVal_raw = bspAdc.valRaw[chId];
            pthread_mutex_unlock(&bspAdc.mutex);
            res = RES_OK;
        }
        else
        {
			ESP_LOGE("ADC", "Mutex stopped get raw");
            res = RES_OPERATION_IN_PROGRESS;
        }
    }
    return res;
}

res_t Bsp_Adc_GetVal_mV(bsp_adc_ch_id_t chId, uint16_t *adcVal_mV)
{
    res_t res = RES_ERROR;
    uint16_t rawVal = 0;
    int valSigned_mV = 0;
    uint32_t val_mV = 0UL;
    if (false == bspAdc.init)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else if ((chId > BSP_ADC_CH_ID_NUM) || (NULL == adcVal_mV))
    {
        res = RES_ERROR_INVALID_PARAMETER;
    }
    else
    {
    	if (0 == pthread_mutex_lock(&bspAdc.mutex))
		{
            rawVal = bspAdc.valRaw[chId];
            pthread_mutex_unlock(&bspAdc.mutex);
            adc_cali_raw_to_voltage(bspAdc.caliHandle, rawVal, &valSigned_mV);
            val_mV = (((uint32_t)valSigned_mV * bspAdcCalc[chId].den)
                                  / bspAdcCalc[chId].num);
            *adcVal_mV = (uint16_t)val_mV;
            res = RES_OK;
		}
		else
		{
			ESP_LOGE("ADC", "Mutex stopped get mv");
			res = RES_OPERATION_IN_PROGRESS;
		}
    }
    return res;
}

static bool Bsp_Adc_EventCb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    //PerfCnt_StartMeas(&bspAdc.perfCnt);
    esp_err_t ret;
    uint32_t byteRead = 0;
    uint32_t tmpByteRead = 0;
    BaseType_t mustYield = pdFALSE;
    float average = 0.;
    uint32_t rawVal = 0L;

    ret = adc_continuous_read(bspAdc.handle, &bspAdc.result[byteRead], bspAdc.adcLenRequested, &byteRead, 0);
    while((byteRead < bspAdc.adcLenRequested) && (ESP_OK == ret))
    {
        ret = adc_continuous_read(bspAdc.handle, &bspAdc.result[byteRead], bspAdc.adcLenRequested, &tmpByteRead, 0);
        if(ESP_OK == ret)
        {
            byteRead += tmpByteRead;
        }
    }

    if (ESP_OK == ret)
    {
        //PerfCnt_CyclicMeas(&bspAdc.perfCntCyclic);
        for (uint16_t idx = 0; idx < byteRead; idx += SOC_ADC_DIGI_RESULT_BYTES)//vedi adc_digi_output_data_t
        {
            adc_digi_output_data_t *result = (void*) &bspAdc.result[idx];
            static volatile bsp_adc_ch_id_t chId = 0;
            chId = bspAdcChMap[result->type2.channel];
            
            rawVal = result->type2.data;
            average = bspAdc.valRaw[chId] * BSP_ADC_ALFA;
            average += (1 -BSP_ADC_ALFA) *rawVal;
            if (0 == pthread_mutex_trylock(&bspAdc.mutex))
            {
              bspAdc.valRaw[chId] = (uint16_t) rawVal;
              pthread_mutex_unlock(&bspAdc.mutex);
            }
        }
    }


    //PerfCnt_StopMeas(&bspAdc.perfCnt);
    return (mustYield == pdTRUE);
}

// static bool Bsp_Adc_OverflowCb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
// {
// 	PerfCnt_CyclicMeas(&bspAdc.perfCntOverflow);
// 	return false;
// }
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/

/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/
