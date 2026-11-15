/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file bsp_uart.c
 * History:
 *  - 18/01/2025  1.0 MatteoBraiato - First release
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "bsp_uart.h"
#include "esp_mac.h"
#include "driver/uart.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <stdatomic.h>
#include "esp_log.h"
#include <string.h>
#include "bsp_do.h"
#include "hw_cfg.h"
#include "driver/gpio.h"
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
#define BSP_UART_STOP_REQUEST BIT0
#define BSP_UART_STOPPED      BIT1
#define BSP_UART_STOP_TIMEOUT_MS (500U)
#define BSP_UART_TX_DONE_TIMEOUT_MS (500U)

#define BSP_UART_DEVICE                    (UART_NUM_1)
#define BSP_UART_BUFFER_SIZE               (2048U)
#define BSP_UART_QUEUE_LARGE_SIZE          (2U)
#define BSP_UART_QUEUE_LARGE_ELEMENT_SIZE  (1024U)
#define BSP_UART_QUEUE_SMALL_SIZE          (16U)
#define BSP_UART_QUEUE_SMALL_ELEMENT_SIZE  (128U)
/**************************************************************************************************
 *  3. PRIVATE MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4. PRIVATE ENUMERATIONS                                                                       *
 **************************************************************************************************/

/**************************************************************************************************
 *  5. PRIVATE STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/
struct bsp_uart_small_element_s
{
    uint8_t buffer[BSP_UART_QUEUE_SMALL_ELEMENT_SIZE];
    size_t byteNum;
};

struct bsp_uart_large_element_s
{
    uint8_t buffer[BSP_UART_QUEUE_LARGE_ELEMENT_SIZE];
    size_t byteNum;
};

struct bsp_uart_hex_element_s
{
    uint8_t buffer[BSP_UART_QUEUE_SMALL_ELEMENT_SIZE];
    size_t byteNum;
};

struct bsp_uart_s
{
    atomic_bool init;
    atomic_bool ioFailed;
    bool_t driverInstalled;
    bool_t workerCreated;
    bool_t txConfigured;
    StaticEventGroup_t eventsStorage;
    EventGroupHandle_t events;
    QueueHandle_t evtQueue;
    QueueHandle_t rxSmallQueueHnd;
    QueueHandle_t txSmallQueueHnd;
    QueueHandle_t rxLargeQueueHnd;
    QueueHandle_t txLargeQueueHnd;
    StaticQueue_t rxSmallQueue;
    StaticQueue_t txSmallQueue;
    StaticQueue_t rxLargeQueue;
    StaticQueue_t txLargeQueue;

    QueueHandle_t txHexQueueHnd;
    StaticQueue_t txHexQueue;

    uint8_t rxSmallBuff[BSP_UART_QUEUE_SMALL_SIZE*sizeof(struct bsp_uart_small_element_s)];
    uint8_t txSmallBuff[BSP_UART_QUEUE_SMALL_SIZE*BSP_UART_QUEUE_SMALL_ELEMENT_SIZE];
    uint8_t rxLargeBuff[BSP_UART_QUEUE_LARGE_SIZE*sizeof(struct bsp_uart_large_element_s)];
    uint8_t txLargeBuff[BSP_UART_QUEUE_LARGE_SIZE*BSP_UART_QUEUE_LARGE_ELEMENT_SIZE];

    uint8_t txHexBuff[BSP_UART_QUEUE_SMALL_SIZE*sizeof(struct bsp_uart_hex_element_s)];

    uint8_t sendtmpSmallBuff[BSP_UART_QUEUE_SMALL_ELEMENT_SIZE];
    uint8_t sendtmpLargeBuff[BSP_UART_QUEUE_LARGE_ELEMENT_SIZE];

    uint8_t sendtmpHexBuff[BSP_UART_QUEUE_SMALL_ELEMENT_SIZE];
} ;
/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static void Bsp_Uart_Task(void *args);
/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static const char *TAG = "bsp_uart";
static struct bsp_uart_s bspUart;
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t Bsp_Uart_Init(uint32_t baudrate)
{
    res_t res = RES_ERROR;
    esp_err_t retVal = ESP_OK;
    if ((true == bspUart.init) || (true == bspUart.driverInstalled))
    {
        res = RES_ALREADY_INITIALIZED;
    }
    else
    {
        /* Prepare the high GPIO latch before releasing the previous sleep hold. */
        const gpio_config_t txConfig = {
            .pin_bit_mask = (1ULL << BSP_UART_PIN_TX),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        retVal = gpio_set_level(BSP_UART_PIN_TX, 1U);
        if (ESP_OK == retVal)
        {
            retVal = gpio_config(&txConfig);
        }
        if (ESP_OK == retVal)
        {
            retVal = gpio_hold_dis(BSP_UART_PIN_TX);
            gpio_deep_sleep_hold_dis();
        }
        bspUart.events = xEventGroupCreateStatic(&bspUart.eventsStorage);
        bspUart.ioFailed = false;
        const uart_config_t uartConfig = {
            .baud_rate = (int)baudrate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_APB,
        };
        if ((ESP_OK == retVal) && (NULL != bspUart.events))
        {
            retVal = uart_driver_install(BSP_UART_DEVICE, BSP_UART_BUFFER_SIZE,
                    BSP_UART_BUFFER_SIZE, 20, &bspUart.evtQueue, 0);
            bspUart.driverInstalled = (ESP_OK == retVal);
        }
        else
        {
            retVal = ESP_FAIL;
        }
        if (ESP_OK == retVal)
        {
            retVal = uart_param_config(BSP_UART_DEVICE, &uartConfig);
        }
        if (ESP_OK == retVal)
        {
            retVal = uart_set_pin(BSP_UART_DEVICE, BSP_UART_PIN_TX, BSP_UART_PIN_RX,
                    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
            bspUart.txConfigured = (ESP_OK == retVal);
        }
        if (ESP_OK == retVal)
        {
            bspUart.rxLargeQueueHnd = xQueueCreateStatic(BSP_UART_QUEUE_LARGE_SIZE, sizeof(struct bsp_uart_large_element_s), bspUart.rxLargeBuff, &bspUart.rxLargeQueue);
            bspUart.txLargeQueueHnd = xQueueCreateStatic(BSP_UART_QUEUE_LARGE_SIZE, BSP_UART_QUEUE_LARGE_ELEMENT_SIZE, bspUart.txLargeBuff, &bspUart.txLargeQueue);
            bspUart.rxSmallQueueHnd = xQueueCreateStatic(BSP_UART_QUEUE_SMALL_SIZE, sizeof(struct bsp_uart_small_element_s), bspUart.rxSmallBuff, &bspUart.rxSmallQueue);
            bspUart.txSmallQueueHnd = xQueueCreateStatic(BSP_UART_QUEUE_SMALL_SIZE, BSP_UART_QUEUE_SMALL_ELEMENT_SIZE, bspUart.txSmallBuff, &bspUart.txSmallQueue);
            bspUart.txHexQueueHnd = xQueueCreateStatic(BSP_UART_QUEUE_SMALL_SIZE, sizeof(struct bsp_uart_hex_element_s), bspUart.txHexBuff, &bspUart.txHexQueue);
            if ((NULL != bspUart.rxLargeQueueHnd) && (NULL != bspUart.txLargeQueueHnd) &&
                (NULL != bspUart.rxSmallQueueHnd) && (NULL != bspUart.txSmallQueueHnd) &&
                (NULL != bspUart.txHexQueueHnd))
            {
                bspUart.init = true;
                bspUart.workerCreated = (pdPASS == xTaskCreate(Bsp_Uart_Task,
                        "Bsp_Uart_Task", 4096, NULL, 12, NULL));
                if (true == bspUart.workerCreated)
                {
                    res = RES_OK;
                }
                else
                {
                    bspUart.init = false;
                }
            }
        }
        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "UART initialization incomplete; resources retained for shutdown");
        }
    }
    return res;
}

res_t Bsp_Uart_DeInit(void)
{
    res_t res = RES_OK;
    bspUart.init = false;
    if (NULL != bspUart.events)
    {
        xEventGroupSetBits(bspUart.events, BSP_UART_STOP_REQUEST);
    }
    if (true == bspUart.workerCreated)
    {
        EventBits_t bits = xEventGroupWaitBits(bspUart.events, BSP_UART_STOPPED,
                pdFALSE, pdTRUE, pdMS_TO_TICKS(BSP_UART_STOP_TIMEOUT_MS));
        if (0U == (bits & BSP_UART_STOPPED))
        {
            ESP_LOGE(TAG, "UART worker stop timed out; driver retained");
            res = RES_ERROR_TIMEOUT;
        }
        else
        {
            bspUart.workerCreated = false;
        }
    }
    if ((false == bspUart.workerCreated) && (true == bspUart.driverInstalled))
    {
        /* Software queues have no writer now. Discard unsent work on a failed shutdown. */
        if (((NULL != bspUart.txSmallQueueHnd) && (0U != uxQueueMessagesWaiting(bspUart.txSmallQueueHnd))) ||
            ((NULL != bspUart.txLargeQueueHnd) && (0U != uxQueueMessagesWaiting(bspUart.txLargeQueueHnd))) ||
            ((NULL != bspUart.txHexQueueHnd) && (0U != uxQueueMessagesWaiting(bspUart.txHexQueueHnd))))
        {
            ESP_LOGW(TAG, "UART shutdown discarding queued TX data");
            res = RES_ERROR;
        }
        esp_err_t txRes = uart_wait_tx_done(BSP_UART_DEVICE, pdMS_TO_TICKS(BSP_UART_TX_DONE_TIMEOUT_MS));
        if (ESP_OK != txRes)
        {
            ESP_LOGE(TAG, "UART TX completion failed (%d)", txRes);
            if (RES_OK == res)
            {
                res = (ESP_ERR_TIMEOUT == txRes) ? RES_ERROR_TIMEOUT : RES_ERROR;
            }
        }
        /* Latch the UART idle-high pad BEFORE changing routing or deleting the driver.
         * See E5 AT specification 4.30: any RX edge can wake the sleeping modem.
         * On failed TX completion, force high instead; sleep is already unconfirmed. */
        if ((ESP_OK == txRes) && (true == bspUart.txConfigured))
        {
            if (ESP_OK != gpio_hold_en(BSP_UART_PIN_TX))
            {
                ESP_LOGE(TAG, "UART TX hold failed");
                if (RES_OK == res)
                {
                    res = RES_ERROR;
                }
            }
        }
        const gpio_config_t txConfig = {
            .pin_bit_mask = (1ULL << BSP_UART_PIN_TX),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t levelRes = gpio_set_level(BSP_UART_PIN_TX, 1U);
        esp_err_t configRes = gpio_config(&txConfig);
        esp_err_t holdRes = gpio_hold_en(BSP_UART_PIN_TX);
        gpio_deep_sleep_hold_en();
        if ((ESP_OK != levelRes) || (ESP_OK != configRes) || (ESP_OK != holdRes))
        {
            ESP_LOGE(TAG, "UART TX sleep configuration failed (%d/%d/%d)", levelRes, configRes, holdRes);
            if (RES_OK == res)
            {
                res = RES_ERROR;
            }
        }
        if (ESP_OK == uart_driver_delete(BSP_UART_DEVICE))
        {
            bspUart.driverInstalled = false;
            bspUart.txConfigured = false;
            bspUart.evtQueue = NULL;
            bspUart.txSmallQueueHnd = NULL;
            bspUart.txLargeQueueHnd = NULL;
            bspUart.txHexQueueHnd = NULL;
            bspUart.rxSmallQueueHnd = NULL;
            bspUart.rxLargeQueueHnd = NULL;
        }
        else
        {
            ESP_LOGE(TAG, "UART driver deletion failed");
            if (RES_OK == res)
            {
                res = RES_ERROR;
            }
        }
    }
    if ((true == bspUart.ioFailed) && (RES_OK == res))
    {
        res = RES_ERROR;
    }
    return res;
}

res_t Bsp_Uart_Send(const uint8_t *buffer, size_t size)
{
    res_t res = (true == bspUart.ioFailed) ? RES_ERROR : RES_ERROR_UNINITIALIZED;
    if ((true == bspUart.init) && (false == bspUart.ioFailed))
    {
        if(size <= BSP_UART_QUEUE_SMALL_ELEMENT_SIZE)
        {
            uint8_t tmpBuff[BSP_UART_QUEUE_SMALL_ELEMENT_SIZE] = {0};
            memcpy(tmpBuff, buffer, size);
            if(pdTRUE == xQueueSend(bspUart.txSmallQueueHnd, tmpBuff, 0U))
            {
                res = RES_OK;
                ESP_LOGD(TAG, "message small enqueued");
            }
            else
            {
                res = RES_BUFFER_FULL;
                ESP_LOGE(TAG, "send error, small full");
            }
        }
        if(RES_OK != res)
        {
            if(size < BSP_UART_QUEUE_LARGE_ELEMENT_SIZE)
            {
                uint8_t tmpBuff[BSP_UART_QUEUE_LARGE_ELEMENT_SIZE] = {0};
                memcpy(tmpBuff, buffer, size);
                if(pdTRUE == xQueueSend(bspUart.txLargeQueueHnd, tmpBuff, 0U))
                {
                    res = RES_OK;
                    ESP_LOGD(TAG, "message large enqueued");
                }
                else
                {
                    res = RES_BUFFER_FULL;
                    ESP_LOGE(TAG, "send error, lagre full");
                }
            }
            else
            {
                res = RES_ERROR_INVALID_PARAMETER;
                ESP_LOGE(TAG, "send error, buffer too large");
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "send error, not init");
    }
    return res;
}

res_t Bsp_Uart_Send_Hex(const uint8_t *buffer, size_t size)
{
    res_t res = (true == bspUart.ioFailed) ? RES_ERROR : RES_ERROR_UNINITIALIZED;
    if ((true == bspUart.init) && (false == bspUart.ioFailed))
    {
        if(size <= BSP_UART_QUEUE_SMALL_ELEMENT_SIZE)
        {
            struct bsp_uart_hex_element_s el;
            el.byteNum = (uint8_t) size;
            memset(el.buffer, 0U, sizeof(el.buffer));
            memcpy(el.buffer, buffer, size);
            if(pdTRUE == xQueueSend(bspUart.txHexQueueHnd, &el, 0U))
            {
                res = RES_OK;
                ESP_LOGD(TAG, "message hex enqueued");
            }
            else
            {
                res = RES_BUFFER_FULL;
                ESP_LOGE(TAG, "send error, hex full");
            }
        }
        if(RES_OK != res)
        {
            // if(size < BSP_UART_QUEUE_LARGE_ELEMENT_SIZE)
            // {
            //     uint8_t tmpBuff[BSP_UART_QUEUE_LARGE_ELEMENT_SIZE] = {0};
            //     memcpy(tmpBuff, buffer, size);
            //     if(pdTRUE == xQueueSend(bspUart.txLargeQueueHnd, tmpBuff, 0U))
            //     {
            //         res = RES_OK;
            //         ESP_LOGD(TAG, "message large enqueued");
            //     }
            //     else
            //     {
            //         res = RES_BUFFER_FULL;
            //         ESP_LOGE(TAG, "send error, lagre full");
            //     }
            // }
            // else
            // {
            //     res = RES_ERROR_INVALID_PARAMETER;
            //     ESP_LOGE(TAG, "send error, buffer too large");
            // }
            ESP_LOGE(TAG, "send error, need to implement a large queue");
        }
    }
    else
    {
        ESP_LOGE(TAG, "send error, not init");
    }
    return res;
}

res_t Bsp_Uart_Recv(uint8_t *buffer, size_t size, size_t *byteRead)
{
    res_t res = (true == bspUart.ioFailed) ? RES_ERROR : RES_ERROR_UNINITIALIZED;
    if ((true == bspUart.init) && (false == bspUart.ioFailed))
    {
        if (size >= BSP_UART_QUEUE_LARGE_ELEMENT_SIZE)
        {
            struct bsp_uart_large_element_s element;
            if (pdTRUE == xQueueReceive(bspUart.rxLargeQueueHnd, (void*) &element, 0UL))
            {
                memcpy(buffer, element.buffer, element.byteNum);
                *byteRead = element.byteNum;
                res = RES_OK;
            }
            else
            {
                res = RES_BUFFER_EMPTY;
            }
        }
        if (RES_OK != res)
        {
            if (size >= BSP_UART_QUEUE_SMALL_ELEMENT_SIZE)
            {
                struct bsp_uart_small_element_s element;
                if (pdTRUE == xQueueReceive(bspUart.rxSmallQueueHnd, (void*) &element, 0UL))
                {
                    memcpy(buffer, element.buffer, element.byteNum);
                    *byteRead = element.byteNum;
                    res = RES_OK;
                }
                else
                {
                    res = RES_BUFFER_EMPTY;
                }
            }
        }
    }
    return res;
}
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/
static void Bsp_Uart_Task(void *args)
{
    TickType_t lastWakeTime;
    const TickType_t taskTime = pdMS_TO_TICKS(10);
    lastWakeTime = xTaskGetTickCount(); // Initialise the lastWakeTime variable with the current time.
    uart_event_t event;
    struct bsp_uart_hex_element_s el;
    while (0U == (xEventGroupGetBits(bspUart.events) & BSP_UART_STOP_REQUEST))
    {
        xTaskDelayUntil(&lastWakeTime, taskTime);
        if (0U != (xEventGroupGetBits(bspUart.events) & BSP_UART_STOP_REQUEST))
        {
            break;
        }

        if(xQueueReceive(bspUart.txSmallQueueHnd, (void * )bspUart.sendtmpSmallBuff, 0UL))
        {
            ESP_LOGD(TAG, "dequeue small message");
            size_t length = strnlen((char *)bspUart.sendtmpSmallBuff, sizeof(bspUart.sendtmpSmallBuff));
            if ((int)length != uart_write_bytes(BSP_UART_DEVICE, bspUart.sendtmpSmallBuff, length))
            {
                bspUart.ioFailed = true;
            }
            memset(bspUart.sendtmpSmallBuff, 0x00, sizeof(bspUart.sendtmpSmallBuff));
        }
        if(xQueueReceive(bspUart.txLargeQueueHnd, (void * )bspUart.sendtmpLargeBuff, 0UL))
        {
            ESP_LOGD(TAG, "dequeue large message");
            size_t length = strnlen((char *)bspUart.sendtmpLargeBuff, sizeof(bspUart.sendtmpLargeBuff));
            if ((int)length != uart_write_bytes(BSP_UART_DEVICE, bspUart.sendtmpLargeBuff, length))
            {
                bspUart.ioFailed = true;
            }
            memset(bspUart.sendtmpLargeBuff, 0x00, sizeof(bspUart.sendtmpLargeBuff));
        }
        if(xQueueReceive(bspUart.txHexQueueHnd, (void*) &el, 0UL))
        {
            ESP_LOGD(TAG, "dequeue hex message");
            
            memcpy(bspUart.sendtmpHexBuff, el.buffer, el.byteNum);
            uint8_t len = el.byteNum;
            if ((int)len != uart_write_bytes(BSP_UART_DEVICE, bspUart.sendtmpHexBuff, (size_t)len))
            {
                bspUart.ioFailed = true;
            }
            memset(bspUart.sendtmpHexBuff, 0x00, (size_t)len);
        }
        if(xQueueReceive(bspUart.evtQueue, (void * )&event, 0UL))
        {
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                {
                    bool_t enqueued = false;
                    if (event.size <= BSP_UART_QUEUE_SMALL_ELEMENT_SIZE)
                    {
                        struct bsp_uart_small_element_s element = {0};
                        element.byteNum = event.size;
                        if ((int)event.size == uart_read_bytes(BSP_UART_DEVICE, element.buffer, event.size, 0U))
                        {
                            enqueued = (pdTRUE == xQueueSend(bspUart.rxSmallQueueHnd, &element, 0U));
                        }
                    }
                    else if (event.size <= BSP_UART_QUEUE_LARGE_ELEMENT_SIZE)
                    {
                        struct bsp_uart_large_element_s element = {0};
                        element.byteNum = event.size;
                        if ((int)event.size == uart_read_bytes(BSP_UART_DEVICE, element.buffer, event.size, 0U))
                        {
                            enqueued = (pdTRUE == xQueueSend(bspUart.rxLargeQueueHnd, &element, 0U));
                        }
                    }
                    if (false == enqueued)
                    {
                        /* Missing bytes invalidate command replies; never reread consumed data. */
                        bspUart.ioFailed = true;
                        ESP_LOGE(TAG, "UART receive/read queue failure");
                    }
                    break;
                }
                    //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    bspUart.ioFailed = true;
                    ESP_LOGE(TAG, "hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(BSP_UART_DEVICE);
                    break;
                    //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    bspUart.ioFailed = true;
                    ESP_LOGE(TAG, "ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(BSP_UART_DEVICE);
                    break;
                    //Event of UART RX break detected
                case UART_BREAK:
                    bspUart.ioFailed = true;
                    ESP_LOGE(TAG, "uart rx break");
                    break;
                    //Event of UART parity check error
                case UART_PARITY_ERR:
                    bspUart.ioFailed = true;
                    ESP_LOGE(TAG, "uart parity error");
                    break;
                    //Event of UART frame error
                case UART_FRAME_ERR:
                    bspUart.ioFailed = true;
                    ESP_LOGE(TAG, "uart frame error");
                    break;
                    //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    ESP_LOGI(TAG, "uart pattern detected");
                    break;
                    //Others
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    /* No driver or queue accesses after this acknowledgment. */
    xEventGroupSetBits(bspUart.events, BSP_UART_STOPPED);
    vTaskDelete(NULL);
}
/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/
