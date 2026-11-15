/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file bdi_i2c.c
 * History:
 *  - 19/01/2025  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "bdi_i2c.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include <freertos/queue.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "string.h"
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
#define BDI_I2C_SCL_GPIO				(13U)
#define BDI_I2C_SDA_GPIO				(12U)
#define BDI_I2C_NORMAL_SPEED_CLK_HZ		(100000UL)
#define BDI_I2C_FAST_SPEED_CLK_HZ		(400000UL)
#define BDI_I2C_MASTER_NUM				(0U)
#define BDI_I2C_MASTER_RX_BUF_DISABLE	(0U)
#define BDI_I2C_MASTER_TX_BUF_DISABLE	(0U)
#define BDI_I2C_MASTER_TIMEOUT_MS		(1000UL)

#define BDI_I2C_STOP_REQUEST BIT0
#define BDI_I2C_STOPPED      BIT1
#define BDI_I2C_STOP_TIMEOUT_MS (1100U) /* One in-flight 1 s bus operation plus scheduling. */

#define BDI_I2C_TASK_TIME_MS			(10U)

#define BDI_I2C_MAX_OP_NUM				(50U)
#define BDI_I2C_OP_SIZE					(28U)		//sizeof(bdi_i2c_operation_t)
#define BDI_I2C_OP_BUFF_SIZE			(BDI_I2C_MAX_OP_NUM * BDI_I2C_OP_SIZE)

#define BDI_I2C_TASK_PRIORITY			(9U)

#define BDI_I2C_WRITE_BUFF_SIZE         (64U)
/**************************************************************************************************
 *  3. PRIVATE MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4. PRIVATE ENUMERATIONS                                                                       *
 **************************************************************************************************/
enum bdi_i2c_opst_e
{
    BDI_I2C_OPST_IDLE,
	BDI_I2C_OPST_READWRITE,
    BDI_I2C_OPST_COMPLETE,
    BDI_I2C_OPST_ERROR
};
/**************************************************************************************************
 *  5. PRIVATE STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/
struct bdi_i2c_s
{
	bool_t init;
	bool_t run;
    bool_t driverInstalled;
    bool_t workerCreated;
    StaticEventGroup_t eventsStorage;
    EventGroupHandle_t events;
    QueueHandle_t opQueueHandle;
    StaticQueue_t opQueue;
    uint8_t queueBuffer[BDI_I2C_OP_BUFF_SIZE];
    uint8_t tmpWriteBuff[BDI_I2C_WRITE_BUFF_SIZE];
	enum bdi_i2c_opst_e opSt;
};
/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/
typedef struct bdi_i2c_s bdi_i2c_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static void Bdi_I2c_Task(void *args);
/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static  bdi_i2c_t bdiI2c;
static const char* TAG = "Bdi_I2C";
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t Bdi_I2c_Init(void)
{
    res_t res = RES_ERROR;
    if ((true == bdiI2c.init) || (true == bdiI2c.driverInstalled))
    {
        res = RES_ALREADY_INITIALIZED;
    }
    else
    {
        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = BDI_I2C_SDA_GPIO,
            .scl_io_num = BDI_I2C_SCL_GPIO,
            .sda_pullup_en = GPIO_PULLUP_DISABLE,
            .scl_pullup_en = GPIO_PULLUP_DISABLE,
            .master.clk_speed = BDI_I2C_NORMAL_SPEED_CLK_HZ,
        };
        bdiI2c.events = xEventGroupCreateStatic(&bdiI2c.eventsStorage);
        esp_err_t ret = i2c_param_config(BDI_I2C_MASTER_NUM, &conf);
        if (ESP_OK == ret)
        {
            ret = i2c_driver_install(BDI_I2C_MASTER_NUM, conf.mode,
                    BDI_I2C_MASTER_RX_BUF_DISABLE, BDI_I2C_MASTER_TX_BUF_DISABLE, 0);
            bdiI2c.driverInstalled = (ESP_OK == ret);
        }
        if ((ESP_OK == ret) && (NULL != bdiI2c.events))
        {
            bdiI2c.opQueueHandle = xQueueCreateStatic(BDI_I2C_MAX_OP_NUM,
                    sizeof(bdi_i2c_operation_t), bdiI2c.queueBuffer, &bdiI2c.opQueue);
            if (NULL != bdiI2c.opQueueHandle)
            {
                bdiI2c.opSt = BDI_I2C_OPST_IDLE;
                bdiI2c.workerCreated = (pdPASS == xTaskCreatePinnedToCore(Bdi_I2c_Task,
                        "BDI_I2C_Task", 4096UL, NULL, BDI_I2C_TASK_PRIORITY, NULL, tskNO_AFFINITY));
                if (true == bdiI2c.workerCreated)
                {
                    bdiI2c.init = true;
                    bdiI2c.run = true;
                    res = RES_OK;
                }
            }
        }
        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "I2C initialization incomplete; resources retained for shutdown");
        }
    }
    return res;
}

res_t Bdi_I2c_Deinit(void)
{
    res_t res = Bdi_I2c_Stop();
    if ((RES_OK == res) && (true == bdiI2c.driverInstalled))
    {
        if (ESP_OK == i2c_driver_delete(BDI_I2C_MASTER_NUM))
        {
            bdiI2c.driverInstalled = false;
            bdiI2c.init = false;
            bdiI2c.opQueueHandle = NULL;
        }
        else
        {
            ESP_LOGE(TAG, "I2C driver deletion failed");
            res = RES_ERROR;
        }
    }
    return res;
}



res_t Bdi_I2c_Start(void)
{
    res_t res = RES_ERROR_UNINITIALIZED;
    if ((true == bdiI2c.init) && (true == bdiI2c.workerCreated) &&
        (0U == (xEventGroupGetBits(bdiI2c.events) & BDI_I2C_STOP_REQUEST)))
    {
        bdiI2c.run = true;
        res = RES_OK;
    }
    return res;
}

res_t Bdi_I2c_Stop(void)
{
    res_t res = RES_OK;
    bdiI2c.run = false;
    if (NULL != bdiI2c.events)
    {
        xEventGroupSetBits(bdiI2c.events, BDI_I2C_STOP_REQUEST);
    }
    if (true == bdiI2c.workerCreated)
    {
        EventBits_t bits = xEventGroupWaitBits(bdiI2c.events, BDI_I2C_STOPPED,
                pdFALSE, pdTRUE, pdMS_TO_TICKS(BDI_I2C_STOP_TIMEOUT_MS));
        if (0U == (bits & BDI_I2C_STOPPED))
        {
            ESP_LOGE(TAG, "I2C worker stop timed out; driver retained");
            res = RES_ERROR_TIMEOUT;
        }
        else
        {
            bdiI2c.workerCreated = false;
        }
    }
    return res;
}

res_t Bdi_I2c_WriteData(uint8_t devAddr, uint8_t regAddr, uint8_t regAddrSize, uint8_t *data, size_t len, opResultCb_t cb)
{
	res_t res = RES_ERROR_UNINITIALIZED;

	if ((true == bdiI2c.init) && (true == bdiI2c.run))
	{
	    res = RES_ERROR_INVALID_PARAMETER;
	    if((NULL != data) && ((regAddrSize + len) <= BDI_I2C_WRITE_BUFF_SIZE))
	    {
            bdi_i2c_operation_t op = {0};
            op.type = BDI_I2C_OP_WRITE;
            op.writeBufferSize = len;
            op.writeBuffer = data;
            op.regAddr = regAddr;
            op.regAddrSize = regAddrSize;
            op.devAddr = devAddr;
            op.readBuffer = NULL;
            op.readBufferSize = 0U;
            op.cb = cb;

            if(pdTRUE == xQueueSend(bdiI2c.opQueueHandle, (void*) &op, 0U))
            {
                res = RES_OK;
            }
            else
            {
                res = RES_BUFFER_FULL;
            }
	    }
	}
	return res;
}

res_t Bdi_I2c_ReadData(uint8_t devAddr, uint8_t regAddr, uint8_t regAddrSize, uint8_t *data, size_t len, opResultCb_t cb)
{
	res_t res = RES_ERROR_UNINITIALIZED;

	if ((true == bdiI2c.init) && (true == bdiI2c.run))
	{
        res = RES_ERROR_INVALID_PARAMETER;
	    if(NULL != data)
	    {
            bdi_i2c_operation_t op = {0};
            op.regAddr = regAddr,
            op.type = BDI_I2C_OP_READ,
            op.readBuffer = data,
            op.readBufferSize = len;
            op.regAddrSize = regAddrSize;
            op.devAddr = devAddr;
            op.writeBuffer = NULL;
            op.writeBufferSize = 0U;
            op.cb = cb;

            if(pdTRUE == xQueueSend(bdiI2c.opQueueHandle, (void*) &op, 0U))
            {
                res = RES_OK;
            }
            else
            {
                res = RES_BUFFER_FULL;
            }
	    }
	}
	return res;
}

res_t Bdi_I2c_WriteReadData(uint8_t devAddr, uint8_t regAddr, uint8_t regAddrSize, uint8_t *writeBuff, size_t writeBuffLen, uint8_t *readBuff, size_t readBuffLen, opResultCb_t cb)
{
    res_t res = RES_ERROR_UNINITIALIZED;

    if ((true == bdiI2c.init) && (true == bdiI2c.run))
    {
        res = RES_ERROR_INVALID_PARAMETER;
        if((NULL != writeBuff) && (NULL != readBuff) && ((regAddrSize + writeBuffLen) <= BDI_I2C_WRITE_BUFF_SIZE))
        {
            bdi_i2c_operation_t op = {0};
            op.regAddr = regAddr,
            op.type = BDI_I2C_OP_READWRITE,
            op.readBuffer = readBuff,
            op.readBufferSize = readBuffLen;
            op.regAddrSize = regAddrSize;
            op.devAddr = devAddr;
            op.writeBuffer = writeBuff;
            op.writeBufferSize = writeBuffLen;
            op.cb = cb;

            if(pdTRUE == xQueueSend(bdiI2c.opQueueHandle, (void*) &op, 0U))
            {
                res = RES_OK;
            }
            else
            {
                res = RES_BUFFER_FULL;
            }
        }
    }
    return res;
}
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/
static void Bdi_I2c_Task(void *args)
{
	esp_err_t ret = ESP_FAIL;	// devo inizializzarlo senno' mi rompe il cazzo
	bdi_i2c_operation_t op = {0};
    TickType_t lastWakeTime;
    const TickType_t taskTime = pdMS_TO_TICKS(BDI_I2C_TASK_TIME_MS);
    lastWakeTime = xTaskGetTickCount();
    while (0U == (xEventGroupGetBits(bdiI2c.events) & BDI_I2C_STOP_REQUEST))
    {
		//ESP_LOGD(TAG, "Task tack");
        xTaskDelayUntil(&lastWakeTime, taskTime);
        if (0U != (xEventGroupGetBits(bdiI2c.events) & BDI_I2C_STOP_REQUEST))
        {
            break;
        }
    	switch (bdiI2c.opSt)
    	{
			case BDI_I2C_OPST_IDLE:
			{
				if (pdTRUE == xQueueReceive(bdiI2c.opQueueHandle, (void*)&op, 0UL))
				{
					bdiI2c.opSt = BDI_I2C_OPST_READWRITE;
				}
				break;
			}
			case BDI_I2C_OPST_READWRITE:
			{
				switch(op.type)
				{
					case BDI_I2C_OP_READ:
					{
						// TODO devo verificare che il dispositivo sia ready (ho già un timeout)?
					    //ret = i2c_master_read_from_device(BDI_I2C_MASTER_NUM, op.devAddr, op.readBuffer, op.readBufferSize, pdMS_TO_TICKS(BDI_I2C_MASTER_TIMEOUT_MS));
						
						memcpy(bdiI2c.tmpWriteBuff, &op.regAddr, op.regAddrSize);
                        if (op.writeBufferSize > 0U)
                        {
                            memcpy(&bdiI2c.tmpWriteBuff[op.regAddrSize], op.writeBuffer, op.writeBufferSize);
                        }
                        size_t byteToSend = op.regAddrSize + op.writeBufferSize;
						ret = i2c_master_write_read_device(
							BDI_I2C_MASTER_NUM, op.devAddr, bdiI2c.tmpWriteBuff, byteToSend,
							op.readBuffer, op.readBufferSize,
							pdMS_TO_TICKS(BDI_I2C_MASTER_TIMEOUT_MS));

						if (ESP_OK == ret)
						{
						    if(NULL != op.cb)
						    {
						        op.cb(BDI_I2C_RES_DONE, op.userData);
						    }
							bdiI2c.opSt = BDI_I2C_OPST_COMPLETE;
						}
						else
						{
							ESP_LOGE(TAG,
								"I2C READ FAILED: dev=0x%02X, reg=0x%02X, "
								"len=%u, esp_err=%s (%d)",
								op.devAddr,
								op.regAddr,
								(unsigned int)op.readBufferSize,
								esp_err_to_name(ret),
								ret
							);

							if (NULL != op.cb)
							{
								op.cb(BDI_I2C_RES_FAIL, op.userData);
							}

							bdiI2c.opSt = BDI_I2C_OPST_ERROR;
						}
						break;
					}
					case BDI_I2C_OP_WRITE:
					{
						// TODO devo verificare che il dispositivo sia ready (ho già un timeout)?

					    /* Size checked in enqueue function */
					    memcpy(bdiI2c.tmpWriteBuff, &op.regAddr, op.regAddrSize);
					    memcpy(&bdiI2c.tmpWriteBuff[op.regAddrSize], op.writeBuffer, op.writeBufferSize);
					    size_t byteToSend = op.regAddrSize + op.writeBufferSize;

						ret = i2c_master_write_to_device(BDI_I2C_MASTER_NUM,
						        op.devAddr,
						        bdiI2c.tmpWriteBuff,
						        byteToSend,
								pdMS_TO_TICKS(BDI_I2C_MASTER_TIMEOUT_MS));
						if (ESP_OK == ret)
						{
                            if(NULL != op.cb)
                            {
                                op.cb(BDI_I2C_RES_DONE, op.userData);
                            }
                            bdiI2c.opSt = BDI_I2C_OPST_COMPLETE;
						}
						else
						{
                            if(NULL != op.cb)
                            {
                                op.cb(BDI_I2C_RES_FAIL, op.userData);
                            }
                            bdiI2c.opSt = BDI_I2C_OPST_ERROR;
						}
						break;
					}
					case BDI_I2C_OP_READWRITE:
					{
					    /* Size checked in enqueue function */
                        memcpy(bdiI2c.tmpWriteBuff, &op.regAddr, op.regAddrSize);
                        if (op.writeBufferSize > 0U)
                        {
                            memcpy(&bdiI2c.tmpWriteBuff[op.regAddrSize], op.writeBuffer, op.writeBufferSize);
                        }
                        size_t byteToSend = op.regAddrSize + op.writeBufferSize;
                        ret = i2c_master_write_read_device(
                                BDI_I2C_MASTER_NUM, op.devAddr, bdiI2c.tmpWriteBuff, byteToSend,
                                op.readBuffer, op.readBufferSize,
                                pdMS_TO_TICKS(BDI_I2C_MASTER_TIMEOUT_MS));
                        if (ESP_OK == ret)
                        {
                            if(NULL != op.cb)
                            {
                                op.cb(BDI_I2C_RES_DONE, op.userData);
                            }
                            bdiI2c.opSt = BDI_I2C_OPST_COMPLETE;
                        }
                        else
                        {
                            if(NULL != op.cb)
                            {
                                op.cb(BDI_I2C_RES_FAIL, op.userData);
                            }
                            bdiI2c.opSt = BDI_I2C_OPST_ERROR;
                        }
					    break;
					}
					default:
					{
						break;
					}
				}
				break;
			}
			case BDI_I2C_OPST_COMPLETE:
			{
				if (pdTRUE == xQueueReceive(bdiI2c.opQueueHandle, (void*)&op, 0UL))
				{
					bdiI2c.opSt = BDI_I2C_OPST_READWRITE;
				}
				else
				{
					bdiI2c.opSt = BDI_I2C_OPST_IDLE;
				}
				break;
			}
			case BDI_I2C_OPST_ERROR:
			{
				ESP_LOGE(TAG, "Error: %d", ret);
				bdiI2c.opSt = BDI_I2C_OPST_IDLE;
				break;
			}
			default:
			{
				/* Do Nothing */
				break;
			}
    	}
    }
    /* No bus accesses or callbacks after this acknowledgment. */
    xEventGroupSetBits(bdiI2c.events, BDI_I2C_STOPPED);
    vTaskDelete(NULL);
}
/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/
