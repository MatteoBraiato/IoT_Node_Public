/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file e5lora.c
 * History:
 *  - 21/02/2025  1.0 MatteoBraiato - First release
 *  - 18/10/2025  1.1 MatteoBraiato - Send Hex message implementation
 * file description 
 */

/**************************************************************************************************
 *  1. PRIVATE INCLUDE FILES                                                                      *
 **************************************************************************************************/
#include "e5lora.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <stdatomic.h>
#include "esp_mac.h"
#include "string.h"

#include "bsp/bsp_uart.h"
/**************************************************************************************************
 *  2. PRIVATE DEFINES                                                                            *
 **************************************************************************************************/
#define E5LORA_BAUDRATE                 (9600UL)
#define E5LORA_RECVBUFF_DIM             (1024UL)
#define E5_LORA_CMD_MAX_LENGTH_BYTE     (128U)

#define E5_LORA_ERROR_STR_MAX_LEN       (64U)
#define E5_LORA_ERROR_STR_MSG_ERR       ("Error sending message")

#define E5_LORA_BUFFER_DIM              (128U)

#define E5_LORA_MAX_OP_NUM              (10U)

#define E5_LORA_INIT_TIMEOUT_MS         (60000UL)

#define E5_LORA_JOIN_TIMEOUT_MS         (30000UL)

#define E5_LORA_WAKE_PROBE_TIMEOUT_MS   (1500UL)

#define E5_LORA_SHUTDOWN_TIMEOUT_MS     (5000U)
#define E5_LORA_SLEEP_RESPONSE_MS       (1000U)
#define E5_LORA_SLEEP_COMPLETION_MS     (1500U)
#define E5_LORA_WORKER_STOP_MS          (100U)
#define E5_SHUTDOWN_REQUEST             BIT0
#define E5_MAIN_STOPPED                 BIT1
#define E5_UART_STOP_REQUEST            BIT2
#define E5_UART_STOPPED                 BIT3
#define E5_UART_DRAINED                 BIT4
#define E5_SLEEP_DONE                   BIT5

#define E5_LORA_TASK_TIME_MS            (10U)

#define SEND_AT_CHECK_RESPONSE_NO_LEN   (0xFFU)

#define E5LORA_ACK_FLAG_NONE            (0xFFU)  /* ackFlagIndex value when operation does not correspond to an init step */

#define E5LORA_DEFAULT_APPKEY           (/* INSERT DEFAULT APPKEY HERE */)  /* hex string without quotes */
/**************************************************************************************************
 *  3. PRIVATE MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4. PRIVATE ENUMERATIONS                                                                       *
 **************************************************************************************************/
typedef enum 
{
    E5LORA_UART_STATE_IDLE,
    E5LORA_UART_STATE_SEND,
    E5LORA_UART_STATE_WAIT,
    E5LORA_UART_STATE_DONE,
    E5LORA_UART_STATE_NORESPONSE,
    E5LORA_UART_STATE_ERROR
} e5lora_uart_state_t;

enum e5lora_ack_e
{
    E5LORA_INIT_STEP_ID,
    E5LORA_INIT_STEP_MODE,
    E5LORA_INIT_STEP_DR,
    E5LORA_INIT_STEP_ADR,
    E5LORA_INIT_STEP_CH,
    E5LORA_INIT_STEP_APPKEY,
    E5LORA_JOIN_ACK,
    E5LORA_ACK_NUM
};

typedef enum
{
    E5LORA_JOIN_RESULT_NONE = 0,
    E5LORA_JOIN_RESULT_FRESH,
    E5LORA_JOIN_RESULT_RETAINED,
    E5LORA_JOIN_RESULT_FAILED
} e5lora_join_result_t;
struct e5lora_uart_operation_s
{
    char cmd[E5_LORA_BUFFER_DIM];
    uint16_t timeout_ms;
    size_t sizeTx;
    char expectedAck[E5_LORA_BUFFER_DIM];
    bool expectedAckFlag;
    opResultCb_t cb;
    bool_t isHex;
    uint8_t ackFlagIndex;  /* Index into e5lora.ack[] to set on completion, or 0xFF if none */
}__attribute__ ((packed));
/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/
typedef struct e5lora_uart_operation_s e5lora_uart_operation_t;

#define E5_LORA_OP_BUFF_SIZE            (E5_LORA_MAX_OP_NUM * sizeof(e5lora_uart_operation_t))

/**************************************************************************************************
 *  5. PRIVATE STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/
typedef struct
{
    _Atomic(e5lora_uart_state_t) uartState;
    
    // send
    char cmd[E5_LORA_BUFFER_DIM];
    size_t sizeTx;
    char expected_ack[E5_LORA_BUFFER_DIM];
    bool_t expectedAckFlag;
    // receive
    char recvBuff[E5LORA_RECVBUFF_DIM];
    char recvChunk[E5LORA_RECVBUFF_DIM];
    size_t sizeRx;
    TickType_t startWaitTick;
    uint16_t timeout_ms;

    QueueHandle_t opQueueHandle;
    StaticQueue_t opQueue;
    uint8_t queueBuffer[E5_LORA_OP_BUFF_SIZE];
} e5_uart_t;

struct e5lora_s
{
    atomic_bool run;
    atomic_bool initRequest;
    atomic_bool init;
    atomic_bool joinRequest;
    atomic_bool joined;
    _Atomic(e5lora_state_t) state;
    
    e5_uart_t uart;

    atomic_bool error;
    uint8_t errorStr[E5_LORA_ERROR_STR_MAX_LEN];

    atomic_bool ack[E5LORA_ACK_NUM];
    e5lora_join_result_t joinResult;
    atomic_bool wakeProbeDone;
    res_t wakeProbeRes;
    bool_t initAttempted;
    bool_t uartReady;
    bool_t mainTaskCreated;
    bool_t uartTaskCreated;
    bool_t cleanupComplete;
    res_t shutdownRes;
    res_t sleepRes;
    EventGroupHandle_t events;
    StaticEventGroup_t eventsStorage;
    SemaphoreHandle_t commandLock;
    StaticSemaphore_t commandLockStorage;
};

/**************************************************************************************************
 *  6. PRIVATE TYPES                                                                              *
 **************************************************************************************************/
typedef struct e5lora_s e5lora_t;
/**************************************************************************************************
 *  7. PRIVATE (STATIC) FUNCTION PROTOTYPES                                                       *
 **************************************************************************************************/
static void E5Lora_Task(void *arg);
static res_t Perform_Init(void);
static res_t Perform_Join(void);
static res_t Send_At_Check_Response(const char *expected_ack, int timeout_ms, const char *command, uint8_t command_length, uint8_t ackFlagIndex, opResultCb_t cb);
static res_t sendMsg(uint8_t *cmd, uint8_t length, opResultCb_t cb);
static void DisplayError(void);
static void Uart_Send_Receive_Task(void *arg);
static res_t E5Lora_Wake_Module(void);
static res_t E5Lora_Enter_LowPower(void);
static void E5Lora_WakeProbe_Cb(res_t res, void *userData);
static void E5Lora_Sleep_Cb(res_t res, void *userData);
static bool_t E5Lora_ShutdownRequested(void);
res_t E5_Lora_Enqueue_At_Command(const e5lora_uart_operation_t *op);
res_t Check_Init_Steps(void);
static res_t BuildMsghex(char *msghex, size_t *msghexLen, const uint8_t *payload, size_t payloadLen);
/**************************************************************************************************
 *  8. PRIVATE (STATIC) VARIABLES                                                                 *
 **************************************************************************************************/
static e5lora_t e5lora;
static const char* TAG = "E5_LORA";
/**************************************************************************************************
 *  9. PUBLIC VARIABLES DEFINITIONS                                                               *
 **************************************************************************************************/

/**************************************************************************************************
 *  10. PRIVATE (STATIC) INLINE FUNCTION DEFINITIONS                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  11. PUBLIC FUNCTION DEFINITIONS                                                               *
 **************************************************************************************************/
res_t E5Lora_Init(void)
{
    res_t res = RES_ERROR;
    BaseType_t taskRes = pdFAIL;
    
    if ((true == e5lora.initAttempted) || (true == E5Lora_ShutdownRequested()))
    {
        res = RES_ALREADY_INITIALIZED;
    }
    else
    {
        e5lora.initAttempted = true;
        e5lora.events = xEventGroupCreateStatic(&e5lora.eventsStorage);
        e5lora.commandLock = xSemaphoreCreateMutexStatic(&e5lora.commandLockStorage);
        if ((NULL != e5lora.events) && (NULL != e5lora.commandLock))
        {
            xEventGroupSetBits(e5lora.events, E5_MAIN_STOPPED | E5_UART_STOPPED);
            res = Bsp_Uart_Init(E5LORA_BAUDRATE);
            e5lora.uartReady = (RES_OK == res);
        }

        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "Uart initialization error");
        }
        
        /* Create queue and uart task to handle RX and TX uart messages */
        if (RES_OK == res)
        {
            e5lora.uart.opQueueHandle = xQueueCreateStatic(E5_LORA_MAX_OP_NUM, sizeof(e5lora_uart_operation_t), e5lora.uart.queueBuffer, &e5lora.uart.opQueue);
            if(NULL == e5lora.uart.opQueueHandle)
            {
                ESP_LOGE(TAG, "Error while creating uart queue for communicating with E5");
                res = RES_ERROR;
            }
        }
        
        /* Create UART task */
        if (RES_OK == res)
        {
            e5lora.uart.uartState = E5LORA_UART_STATE_IDLE;
            xEventGroupClearBits(e5lora.events, E5_UART_STOPPED);
            taskRes = xTaskCreatePinnedToCore(Uart_Send_Receive_Task, "E5_LORA_Uart_Task", 4096UL, NULL, 10, NULL, tskNO_AFFINITY);
            e5lora.uartTaskCreated = (pdPASS == taskRes);
            if(pdPASS != taskRes)
            {
                xEventGroupSetBits(e5lora.events, E5_UART_STOPPED);
                ESP_LOGE(TAG, "Failed to create UART task");
                res = RES_ERROR;
            }
        }
        
        /* Create main E5 task */
        if (RES_OK == res)
        {
            e5lora.state = E5LORA_STATE_OFF;
            e5lora.initRequest = true;
            e5lora.run = true;
            xEventGroupClearBits(e5lora.events, E5_MAIN_STOPPED);
            taskRes = xTaskCreatePinnedToCore(E5Lora_Task, "E5_LORA_Task", 4096UL, NULL, 9, NULL, tskNO_AFFINITY);
            e5lora.mainTaskCreated = (pdPASS == taskRes);
            if(pdPASS != taskRes)
            {
                xEventGroupSetBits(e5lora.events, E5_MAIN_STOPPED);
                ESP_LOGE(TAG, "Failed to create E5 main task");
                res = RES_ERROR;
                e5lora.run = false;
            }
        }
        
        /* Wait for initialization and network join */
        if (RES_OK == res)
        {
            while ((E5LORA_STATE_READY != e5lora.state) && (E5LORA_STATE_ERROR != e5lora.state))
            {
                /* it's not an infinite loop, check E5_Lora_Task for the timeout --> E5_LORA_INIT_TIMEOUT_MS + E5_LORA_JOIN_TIMEOUT_MS*/
                vTaskDelay(pdMS_TO_TICKS(50U));
            }

            if (E5LORA_STATE_READY == e5lora.state)
            {
                e5lora.initRequest = false;
                res = RES_OK;
            }
            else if (E5LORA_STATE_ERROR == e5lora.state)
            {
                res = RES_ERROR;
            }
            else
            {
                /* condition not handled */
                res = RES_ERROR;
            }
        }
    }
    
    return res;
}

res_t E5Lora_DeInit(void)
{
    res_t res = RES_OK;
    res_t cleanupRes;
    bool_t workersStopped = true;
    const TickType_t startTick = xTaskGetTickCount();
    const TickType_t drainTicks = pdMS_TO_TICKS(E5_LORA_SHUTDOWN_TIMEOUT_MS);

    if (false == e5lora.initAttempted)
    {
        res = RES_ERROR_UNINITIALIZED;
    }
    else if (true == e5lora.cleanupComplete)
    {
        /* Idempotent: never send another command to an already sleeping modem. */
        res = e5lora.shutdownRes;
    }
    else
    {
        if (NULL != e5lora.events)
        {
            /* Serialize closing admission against an in-progress queue submission. */
            if ((NULL != e5lora.commandLock) &&
                (pdTRUE == xSemaphoreTake(e5lora.commandLock, drainTicks)))
            {
                xEventGroupSetBits(e5lora.events, E5_SHUTDOWN_REQUEST);
                xSemaphoreGive(e5lora.commandLock);
            }
            else
            {
                xEventGroupSetBits(e5lora.events, E5_SHUTDOWN_REQUEST);
                res = RES_ERROR_TIMEOUT;
                ESP_LOGE(TAG, "Timed out closing E5 command admission");
            }
        }
        e5lora.run = false;
        e5lora.initRequest = false;
        e5lora.joinRequest = false;

        if ((RES_OK == res) && (true == e5lora.uartReady) && (true == e5lora.uartTaskCreated))
        {
            TickType_t elapsed = xTaskGetTickCount() - startTick;
            TickType_t remaining = (elapsed < drainTicks) ? (drainTicks - elapsed) : 0U;
            const EventBits_t required = E5_MAIN_STOPPED | E5_UART_DRAINED;
            EventBits_t bits = xEventGroupWaitBits(e5lora.events, required, pdFALSE, pdTRUE, remaining);
            if (required == (bits & required))
            {
                res = E5Lora_Enter_LowPower();
            }
            else
            {
                res = RES_ERROR_TIMEOUT;
                ESP_LOGW(TAG, "E5 busy at shutdown deadline; sleep command not sent");
            }
        }
        else if (RES_OK == res)
        {
            res = RES_ERROR_NOT_READY;
            ESP_LOGW(TAG, "E5 transport unavailable; sleep command not sent");
        }

        if (NULL != e5lora.events)
        {
            xEventGroupSetBits(e5lora.events, E5_UART_STOP_REQUEST);
            const EventBits_t required = E5_MAIN_STOPPED | E5_UART_STOPPED;
            EventBits_t bits = xEventGroupWaitBits(e5lora.events, required, pdFALSE, pdTRUE,
                    pdMS_TO_TICKS(E5_LORA_WORKER_STOP_MS));
            workersStopped = (required == (bits & required));
        }
        if (true == workersStopped)
        {
            e5lora.mainTaskCreated = false;
            e5lora.uartTaskCreated = false;
            cleanupRes = Bsp_Uart_DeInit();
            if (RES_OK != cleanupRes)
            {
                ESP_LOGW(TAG, "UART shutdown failed (%d)", cleanupRes);
                if (RES_OK == res)
                {
                    res = cleanupRes;
                }
            }
            e5lora.uartReady = false;
            e5lora.init = false;
            e5lora.joined = false;
            e5lora.joinResult = E5LORA_JOIN_RESULT_NONE;
            e5lora.state = E5LORA_STATE_OFF;
        }
        else
        {
            ESP_LOGE(TAG, "E5 workers did not stop; UART retained until ESP32 deep sleep");
            if (RES_OK == res)
            {
                res = RES_ERROR_TIMEOUT;
            }
        }
        if (RES_OK != res)
        {
            ESP_LOGW(TAG, "E5 shutdown incomplete; sleep unconfirmed (%d)", res);
        }
        e5lora.shutdownRes = res;
        e5lora.cleanupComplete = true;
    }
    return res;
}

res_t E5Lora_Join(void)
{
    res_t res = RES_ERROR_NOT_READY;
    if (false == E5Lora_ShutdownRequested())
    {
        e5lora.joinRequest = true;
        res = RES_OK;
    }
    return res;
}

res_t E5Lora_EnsureConnected(bool_t *joined_now)
{
    res_t res = RES_OK;
    const TickType_t startTick = xTaskGetTickCount();
    const TickType_t timeoutTicks = pdMS_TO_TICKS(E5_LORA_JOIN_TIMEOUT_MS + 2000U);
    if (NULL == joined_now)
    {
        res = RES_ERROR_INVALID_PARAMETER;
    }
    else
    {
        *joined_now = false;
        if (true == E5Lora_ShutdownRequested())
        {
            res = RES_ERROR_NOT_READY;
        }
        else if (false == e5lora.init)
        {
            res = RES_ERROR_UNINITIALIZED;
        }
        else if (E5LORA_STATE_CONNECTED != e5lora.state)
        {
            if ((E5LORA_STATE_READY != e5lora.state) &&
                (E5LORA_STATE_JOINING != e5lora.state) &&
                (E5LORA_STATE_WAIT_FOR_JOIN != e5lora.state))
            {
                res = RES_ERROR_NOT_READY;
            }
            else
            {
                e5lora.joined = false;
                e5lora.joinResult = E5LORA_JOIN_RESULT_NONE;
                e5lora.ack[E5LORA_JOIN_ACK] = false;
                e5lora.joinRequest = true;
                while ((E5LORA_STATE_CONNECTED != e5lora.state) &&
                       (E5LORA_STATE_ERROR != e5lora.state) && (RES_OK == res))
                {
                    if (true == E5Lora_ShutdownRequested())
                    {
                        res = RES_ERROR_NOT_READY;
                    }
                    else if ((xTaskGetTickCount() - startTick) >= timeoutTicks)
                    {
                        res = RES_ERROR_TIMEOUT;
                    }
                    else
                    {
                        vTaskDelay(pdMS_TO_TICKS(20U));
                    }
                }
                if ((RES_OK == res) && (E5LORA_STATE_CONNECTED != e5lora.state))
                {
                    res = RES_ERROR;
                }
                while ((RES_OK == res) && (E5LORA_UART_STATE_IDLE != e5lora.uart.uartState))
                {
                    if (true == E5Lora_ShutdownRequested())
                    {
                        res = RES_ERROR_NOT_READY;
                    }
                    else if ((xTaskGetTickCount() - startTick) >= timeoutTicks)
                    {
                        res = RES_ERROR_TIMEOUT;
                    }
                    else
                    {
                        vTaskDelay(pdMS_TO_TICKS(10U));
                    }
                }
                if (RES_OK == res)
                {
                    *joined_now = (E5LORA_JOIN_RESULT_FRESH == e5lora.joinResult);
                }
            }
        }
    }
    return res;
}

res_t E5Lora_SendMsg(uint8_t *msg, size_t length, opResultCb_t cb)
{
    res_t res = RES_ERROR;
    char cmd[E5_LORA_CMD_MAX_LENGTH_BYTE];
    // uint8_t cmdLen = 0U;

    if ((NULL != msg) && (length <= E5LORA_MAX_PAYLOAD_LENGTH))
    {
        // pack the values    
        const char prefix[] = "AT+MSG=";
        const char suffix[] = "\r\n";

        size_t pos = 0;
        memcpy(cmd + pos, prefix, sizeof(prefix)-1);
        pos += (sizeof(prefix)-1);
        memcpy(cmd + pos, msg, length);
        pos += length;
        memcpy(cmd + pos, suffix, sizeof(suffix)-1);
        pos += (sizeof(suffix)-1);

        /* Exceed payload max size */
        if (pos > E5_LORA_CMD_MAX_LENGTH_BYTE)
        {
            // errore nella creazione della stringa
            ESP_LOGE(TAG, "Command error, length > %d byte", E5_LORA_CMD_MAX_LENGTH_BYTE);
            res = RES_ERROR;
        }
        /* negative payload size --> error */
        else if (pos <= 0U)
        {
            // errore nella creazione della stringa
            ESP_LOGE(TAG, "Command error, length <= 0 byte");
            res = RES_ERROR;
        }
        else
        {
            // send message
            res = sendMsg((uint8_t*)cmd, pos, cb);
        }
    }
    return res;
}

res_t E5Lora_SendMsgHex(uint8_t *msg, size_t length, opResultCb_t cb)
{
    res_t res = RES_ERROR;
    char cmd[E5_LORA_CMD_MAX_LENGTH_BYTE];

    if ((NULL != msg) && (length <= E5LORA_MAX_PAYLOAD_LENGTH))
    {
        /* pack the values */
        size_t cmdLength = 0;
        res = BuildMsghex(cmd, &cmdLength, msg, length);
        ESP_LOGD(TAG, "Hex message packed = %s", cmd);

        if (cmdLength > E5_LORA_CMD_MAX_LENGTH_BYTE)
        {
            // errore nella creazione della stringa
            ESP_LOGE(TAG, "Command error, length > %d byte", E5_LORA_CMD_MAX_LENGTH_BYTE);
            res = RES_ERROR;
        }
        else if (cmdLength <= 0U)
        {
            // errore nella creazione della stringa
            ESP_LOGE(TAG, "Command error, length <= 0 byte");
            res = RES_ERROR;
        }
        else
        {
            // send message
            res = sendMsg((uint8_t*)cmd, cmdLength, cb);
        }
    }
    return res;
}
/**************************************************************************************************
 *  12. PRIVATE (STATIC) FUNCTION DEFINITIONS                                                     *
 **************************************************************************************************/
static res_t E5Lora_Wake_Module(void)
{
    res_t res = RES_ERROR_TIMEOUT;
    TickType_t startTick = 0U;
    const TickType_t timeoutTicks = pdMS_TO_TICKS(E5_LORA_WAKE_PROBE_TIMEOUT_MS);
    uint8_t dummy_char = 'A';

    e5lora.wakeProbeDone = false;
    e5lora.wakeProbeRes = RES_ERROR_TIMEOUT;

    /* Send dummy character to wake the module */
    res = Bsp_Uart_Send(&dummy_char, 1U);
    if (RES_OK == res)
    {
        /* Wait at least 5ms for module to wake up as per specification */
        vTaskDelay(pdMS_TO_TICKS(5U));

        /* Verify module is awake by sending AT and waiting for the callback result */
        res = Send_At_Check_Response("+AT: OK\r\n", 1000, "AT\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_ACK_FLAG_NONE, E5Lora_WakeProbe_Cb);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to send wake-up character");
    }

    if (RES_OK == res)
    {
        startTick = xTaskGetTickCount();
        while ((false == e5lora.wakeProbeDone) && ((xTaskGetTickCount() - startTick) <= timeoutTicks))
        {
            vTaskDelay(pdMS_TO_TICKS(10U));
        }

        if (true == e5lora.wakeProbeDone)
        {
            res = e5lora.wakeProbeRes;
        }
        else
        {
            res = RES_ERROR_TIMEOUT;
        }
    }

    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "Module wake-up verification failed");
    }
    else
    {
        ESP_LOGD(TAG, "Module wake-up verification successful");
    }

    return res;
}

static res_t E5Lora_Enter_LowPower(void)
{
    res_t res;
    e5lora.sleepRes = RES_ERROR_TIMEOUT;
    xEventGroupClearBits(e5lora.events, E5_SLEEP_DONE);
    res = Send_At_Check_Response("+LOWPOWER: SLEEP\r\n", E5_LORA_SLEEP_RESPONSE_MS,
            "AT+LOWPOWER\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN,
            E5LORA_ACK_FLAG_NONE, E5Lora_Sleep_Cb);
    if (RES_OK == res)
    {
        EventBits_t bits = xEventGroupWaitBits(e5lora.events, E5_SLEEP_DONE,
                pdFALSE, pdTRUE, pdMS_TO_TICKS(E5_LORA_SLEEP_COMPLETION_MS));
        res = (0U != (bits & E5_SLEEP_DONE)) ? e5lora.sleepRes : RES_ERROR_TIMEOUT;
    }
    else
    {
        ESP_LOGW(TAG, "E5 sleep command enqueue failed (%d)", res);
    }
    if (RES_OK != res)
    {
        /* A lost reply is not proof the modem stayed awake. Do not send again. */
        ESP_LOGW(TAG, "E5 sleep reply unconfirmed (%d)", res);
    }
    else
    {
        ESP_LOGI(TAG, "E5 sleep acknowledged");
    }
    return res;
}

static void E5Lora_Sleep_Cb(res_t res, void *userData)
{
    EUNUSED(userData);
    e5lora.sleepRes = res;
    xEventGroupSetBits(e5lora.events, E5_SLEEP_DONE);
}

static bool_t E5Lora_ShutdownRequested(void)
{
    bool_t requested = false;
    if (NULL != e5lora.events)
    {
        requested = (0U != (xEventGroupGetBits(e5lora.events) & E5_SHUTDOWN_REQUEST));
    }
    return requested;
}

static void E5Lora_WakeProbe_Cb(res_t res, void *userData)
{
    EUNUSED(userData);

    e5lora.wakeProbeRes = res;
    e5lora.wakeProbeDone = true;
}

static void E5Lora_Task(void *arg)
{
    res_t res;
    TickType_t lastWakeTime;
    TickType_t initStartTime = 0U;
    TickType_t joinStartTime = 0U;
    const TickType_t taskTime = pdMS_TO_TICKS(E5_LORA_TASK_TIME_MS);
    lastWakeTime = xTaskGetTickCount(); // Initialise the lastWakeTime variable with the current time.
    while(e5lora.run)
    {
        xTaskDelayUntil(&lastWakeTime, taskTime);
        if (true == E5Lora_ShutdownRequested())
        {
            break;
        }
        switch(e5lora.state)
        {
            case E5LORA_STATE_OFF:
            {
                if (true == e5lora.initRequest)
                {
                    e5lora.state = E5LORA_STATE_START_INIT;
                }
                else 
                {
                    // do nothing
                }
                break;
            }
            case E5LORA_STATE_START_INIT:
            {
                // initialization process
                res = Perform_Init();
                initStartTime = xTaskGetTickCount();

                if (RES_OK == res)
                {
                    e5lora.state = E5LORA_STATE_WAIT_FOR_INIT;
                }
                else
                {
                    e5lora.state = E5LORA_STATE_ERROR;
                }
                break;
            }
            case E5LORA_STATE_WAIT_FOR_INIT:
            {
                res = Check_Init_Steps();
                TickType_t currentTick = xTaskGetTickCount();
                uint32_t elapsedMs = (currentTick - initStartTime) * portTICK_PERIOD_MS;

                if (elapsedMs < E5_LORA_INIT_TIMEOUT_MS)
                {
                    if (RES_OK == res)
                    {
                        e5lora.state = E5LORA_STATE_READY;
                        e5lora.init = true;
                        ESP_LOGI(TAG, "Init - Complete!\n");
                    }
                    else
                    {
                        // operation in progress
                        // ESP_LOGD(TAG, "Operation in progress...\n");
                    }
                }
                else
                {
                    // timeout
                    ESP_LOGE(TAG, "Init - Timeout");
                    e5lora.state = E5LORA_STATE_ERROR;
                }
                
                break;
            }
            case E5LORA_STATE_READY:
            {
                if (true == e5lora.joinRequest)
                {
                    e5lora.state = E5LORA_STATE_JOINING;
                    ESP_LOGI(TAG, "Join request initialized, joining network...\n");
                }
                else
                {
                    // do nothing
                }
                break;
            }
            case E5LORA_STATE_JOINING:
            {
                res = Perform_Join();
                joinStartTime = xTaskGetTickCount();

                if (RES_OK == res)
                {
                    e5lora.state = E5LORA_STATE_WAIT_FOR_JOIN;
                }
                else
                {
                    e5lora.state = E5LORA_STATE_ERROR;
                }
                break;
            }
            case E5LORA_STATE_WAIT_FOR_JOIN:
            {
                TickType_t currentTick = xTaskGetTickCount();
                uint32_t elapsedMs = (currentTick - joinStartTime) * portTICK_PERIOD_MS;

                if (elapsedMs < E5_LORA_JOIN_TIMEOUT_MS)
                {
                    if (true == e5lora.ack[E5LORA_JOIN_ACK])
                    {
                        e5lora.state = E5LORA_STATE_CONNECTED;
                        e5lora.joined = true;
                        e5lora.joinRequest = false;
                        if (E5LORA_JOIN_RESULT_RETAINED == e5lora.joinResult)
                        {
                            ESP_LOGI(TAG, "Retained LoRa session verified\n");
                        }
                        else
                        {
                            ESP_LOGI(TAG, "Join Complete!\n");
                        }
                    }
                    else
                    {
                        // operation in progress
                        // ESP_LOGD(TAG, "Operation in progress...\n");
                    }
                }
                else
                {
                    // timeout
                    ESP_LOGE(TAG, "Join - Timeout");
                    e5lora.state = E5LORA_STATE_ERROR;
                }
                break;
            }
            case E5LORA_STATE_CONNECTED:
            {
                // handle event during application run
                if (true == e5lora.error)
                {
                    e5lora.state = E5LORA_STATE_ERROR;
                }
                break;
            }
            case E5LORA_STATE_ERROR:
            {
                DisplayError();
                // implement retry --> deinit --> init x3. If not working --> off
                break;
            }
        }
    }
    xEventGroupSetBits(e5lora.events, E5_MAIN_STOPPED);
    vTaskDelete(NULL);
}

static res_t Perform_Init(void)
{
    res_t res = RES_ERROR;

    /* Wake up the module from low power mode (if it was sleeping) */
    res = E5Lora_Wake_Module();
    if (RES_OK != res)
    {
        ESP_LOGE(TAG, "Failed to wake up module during initialization");
        return res;
    }

    /* initialize init steps res */
    for (uint8_t idx=E5LORA_INIT_STEP_ID; idx<E5LORA_INIT_STEP_APPKEY+1; idx++)
    {
        e5lora.ack[idx] = false;
    }

    // TODO use callback functions in Send_At_Check_Response

    // check response: AT --> +AT: OK
    res = Send_At_Check_Response("+AT: OK\r\n", 3000, "AT\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_INIT_STEP_ID, (void*)NULL);

    if (RES_OK == res)
    {
        ESP_LOGD(TAG, "Device connected\n");
        // set mode: AT+MODE=LWOTAA --> +MODE: LWOTAA
        res = Send_At_Check_Response("+MODE: LWOTAA\r\n", 3000, "AT+MODE=LWOTAA\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_INIT_STEP_MODE, (void*)NULL);
        if (RES_OK == res)
        {
            ESP_LOGD(TAG, "+MODE: LWOTAA\n");
            // set data rate: AT+DR=EU868 --> +DR: EU868
            res = Send_At_Check_Response("+DR: EU868\r\n", 3000, "AT+DR=EU868\r\n",SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_INIT_STEP_DR, (void*)NULL);
        }

        if (RES_OK == res)
        {
            ESP_LOGD(TAG, "+DR: EU868\n");
            // set ADR: AT+ADR=ON --> +ADR: ON
            res = Send_At_Check_Response("+ADR: ON\r\n", 3000, "AT+ADR=ON\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_INIT_STEP_ADR, (void*)NULL);
        }

        if (RES_OK == res)
        {
            ESP_LOGI(TAG, "ADR enabled (+ADR: ON)");
            // set channel: AT+CH=NUM,0-2 --> +CH: NUM
            res = Send_At_Check_Response("+CH: NUM, 0-2\r\n", 3000, "AT+CH=NUM,0-2\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_INIT_STEP_CH, (void*)NULL);
        }

        if (RES_OK == res)
        {
            ESP_LOGD(TAG, "AT+CH=NUM,0-2\n");
            // set appkey: AT+KEY=APPKEY,"<appkey>" --> +KEY: APPKEY
            // res = Send_At_Check_Response("+KEY: APPKEY <INSERT_APPKEY_HERE>\r\n", 3000,
            //      "AT+KEY=APPKEY,\"<INSERT_APPKEY_HERE>\"\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_INIT_STEP_APPKEY, (void*)NULL);    // TODO here is hardcoded the appkey, make it configurable
        }
        ESP_LOGD(TAG, "+KEY: APPKEY Set\n");
    }
    else
    {
        ESP_LOGE(TAG, "No E5 module found. Please check the pyhsical connection or the baudrate setting\n");
    }
    
    return res;
}

static res_t Perform_Join(void)
{
    res_t res = RES_ERROR;
    e5lora.joinResult = E5LORA_JOIN_RESULT_NONE;

    res = Send_At_Check_Response("+JOIN: Done\r\n", 30000, "AT+JOIN\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_JOIN_ACK, (void*)NULL);
    //ESP_LOGI(TAG, "JOIN: network joined");

    if (RES_OK != res)
    {
        // TODO a cosa serve?
        Send_At_Check_Response("+ID: AppEui\r\n", 30000, "AT+ID\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_ACK_FLAG_NONE, (void*)NULL);
        ESP_LOGE(TAG, "JOIN failed!\n");
    }
    else
    {
        // do nothing
    }

    return res;
}

static res_t Send_At_Check_Response(const char *expected_ack, int timeout_ms, const char *command, uint8_t command_length, uint8_t ackFlagIndex, opResultCb_t cb)
{
    res_t res = RES_ERROR;
    e5lora_uart_operation_t op;

    if (E5LORA_UART_STATE_IDLE == e5lora.uart.uartState)
    {
        // set up command
        if (NULL != command)
        {
            /* Set command buffer */
            if (SEND_AT_CHECK_RESPONSE_NO_LEN == command_length)    // length is determined by string length (string must have \0 terminator)
            {
                op.sizeTx = snprintf(op.cmd, E5_LORA_BUFFER_DIM, "%s", command);
                op.isHex = false;
            }
            else    // length is determined by command_length value
            {
                op.sizeTx = command_length;
                memcpy(op.cmd, command, (size_t) command_length);
                op.isHex = true;
            }

            /* Set acknowledge buffer */
            if (NULL != expected_ack)
            {
                snprintf(op.expectedAck, E5_LORA_BUFFER_DIM, "%s", expected_ack);
                op.expectedAckFlag = true;
            }
            else
            {
                op.expectedAckFlag = false;
            }

            op.cb = cb;
            op.timeout_ms = (uint16_t) timeout_ms;
            op.ackFlagIndex = ackFlagIndex;  /* Index of init step to flag, or 0xFF if none */
            res = E5_Lora_Enqueue_At_Command(&op);
        }
        else
        {
            /* No command */
            res = RES_ERROR;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Another communication is still on going");
        res = RES_ERROR_NOT_READY;
    }

    return res;
}

static res_t sendMsg(uint8_t *cmd, uint8_t length, opResultCb_t cb)
{
    res_t res = RES_ERROR;
    uint8_t msg[E5_LORA_CMD_MAX_LENGTH_BYTE];

    /* If connected to LORA network then can send message */
    if (E5LORA_STATE_CONNECTED == e5lora.state)
    {
        // copy message
        memset(msg, 0, E5_LORA_CMD_MAX_LENGTH_BYTE);
        if (length < E5_LORA_CMD_MAX_LENGTH_BYTE)
        {
            memcpy(msg, cmd, length);
        }
        // send mesage
        res = Send_At_Check_Response("+MSGHEX: Done", 30000, (char*)msg, length, E5LORA_ACK_FLAG_NONE, cb); // TODO here is hardcoded to send hex messages, make it configurable
        if (RES_OK != res)
        {
            ESP_LOGE(TAG, "Send failed!");
            e5lora.error = true;
            memset(e5lora.errorStr,0,E5_LORA_ERROR_STR_MAX_LEN);
            memcpy(e5lora.errorStr, E5_LORA_ERROR_STR_MSG_ERR, sizeof(E5_LORA_ERROR_STR_MSG_ERR));
        }
        else
        {
            // message sent correctly
        }
    }
    else if (E5LORA_STATE_ERROR == e5lora.state)
    {
        // signal error
        ESP_LOGE(TAG, "Device is in error state, can't send message!");
        res = RES_ERROR;
    }
    else if (E5LORA_STATE_OFF == e5lora.state)
    {
        ESP_LOGE(TAG, "Device is off, can't send message. Please initialize the device, join the network and then send a message.\n");
        res = RES_ERROR;
    }
    else
    {
        // do not send message, signal status ok but not joined the network yet
        ESP_LOGW(TAG, "Device is on, but has not yet joined the network. Please join the network and then send a message.\n");
        res = RES_ERROR_NOT_READY;
    }

    return res;
}

static void DisplayError(void)
{
    if (true == e5lora.error)
    {
        ESP_LOGE(TAG, "Device error: %s", (char*)e5lora.errorStr);
        /* Don't clear error flag here – let caller decide when to retry */
    }
}

static void Uart_Send_Receive_Task(void *arg)
{
    res_t res = RES_ERROR;
    e5lora_uart_operation_t op = {0};
    TickType_t lastWakeTime;

    memset(op.cmd, 0U, E5_LORA_BUFFER_DIM);
    memset(op.expectedAck, 0U, E5_LORA_BUFFER_DIM);
    
    const TickType_t taskTime = pdMS_TO_TICKS(E5_LORA_TASK_TIME_MS);
    lastWakeTime = xTaskGetTickCount(); // Initialise the lastWakeTime variable with the current time
    while(true)
    {
        xTaskDelayUntil(&lastWakeTime, taskTime);
        if (0U != (xEventGroupGetBits(e5lora.events) & E5_UART_STOP_REQUEST))
        {
            break;
        }
        switch (e5lora.uart.uartState)
        {
            case (E5LORA_UART_STATE_IDLE):
            {
                /* wait for uart operations */
                if(pdTRUE == xQueueReceive(e5lora.uart.opQueueHandle, (void*)&op, 0UL))
                {
                    /* save operation data from queue to local var */
                    ESP_LOGD(TAG, "Command of size %d dequeued = %s", op.sizeTx, op.cmd);
                    memcpy(e5lora.uart.cmd, op.cmd, op.sizeTx);
                    if (false == op.isHex)
                    {
                        e5lora.uart.cmd[op.sizeTx] = '\0';
                        e5lora.uart.sizeTx = op.sizeTx + 1U;
                    }
                    else
                    {
                        e5lora.uart.sizeTx = op.sizeTx;
                    }
                    snprintf(e5lora.uart.expected_ack, E5_LORA_BUFFER_DIM, "%s", op.expectedAck);
                    e5lora.uart.timeout_ms = op.timeout_ms;
                    e5lora.uart.expectedAckFlag = op.expectedAckFlag;
                    memset(e5lora.uart.recvBuff, 0U, E5LORA_RECVBUFF_DIM);
                    e5lora.uart.sizeRx = 0U;

                    e5lora.uart.uartState = E5LORA_UART_STATE_SEND;
                }
                else
                {
                    if (true == E5Lora_ShutdownRequested())
                    {
                        /* Only this worker can acknowledge an empty queue AND idle transaction. */
                        xEventGroupSetBits(e5lora.events, E5_UART_DRAINED);
                    }
                }
                break;
            }
            case (E5LORA_UART_STATE_SEND):
            {
                /* send uart tx message */

                /* if is ASCII */
                if (false == op.isHex)
                {
                    res = Bsp_Uart_Send((uint8_t*)e5lora.uart.cmd, e5lora.uart.sizeTx);
                    ESP_LOGD(TAG, "AT command: %s", e5lora.uart.cmd);
                }
                /* if is hex*/
                else
                {
                    res = Bsp_Uart_Send_Hex((uint8_t*)e5lora.uart.cmd, e5lora.uart.sizeTx);
                }

                if (RES_OK == res)
                {
                    if (true == e5lora.uart.expectedAckFlag)
                    {
                        e5lora.uart.startWaitTick = xTaskGetTickCount();
                        e5lora.uart.uartState = E5LORA_UART_STATE_WAIT;
                    }
                    else
                    {
                        /* Operation sent successfully, no ACK expected – transition to done */
                        e5lora.uart.uartState = E5LORA_UART_STATE_DONE;
                    }   
                }
                else
                {
                    e5lora.uart.uartState = E5LORA_UART_STATE_ERROR;
                }
                break;
            }
            case (E5LORA_UART_STATE_WAIT):
            {
                /* wait for response message
                * if no expected pattern stay here unitl timeout, then E5LORA_UART_STATE_NORESPONSE
                * if expected pattern then E5LORA_UART_STATE_DONE 
                * */
                TickType_t currentTick = xTaskGetTickCount();
                TickType_t elapsedTicks = currentTick - e5lora.uart.startWaitTick;
                
                if (elapsedTicks >= pdMS_TO_TICKS(e5lora.uart.timeout_ms))
                {
                    // timeout
                    e5lora.uart.uartState = E5LORA_UART_STATE_NORESPONSE;
                }
                else
                {
                    // read from UART
                    size_t chunkSize = 0U;
                    res = Bsp_Uart_Recv((uint8_t*)e5lora.uart.recvChunk, E5LORA_RECVBUFF_DIM, &chunkSize);
                    if (RES_OK == res)
                    {
                        /* Validate received size */
                        if (chunkSize > E5LORA_RECVBUFF_DIM)
                        {
                            ESP_LOGE(TAG, "Received size (%d) exceeds buffer! Possible UART driver bug.", chunkSize);
                            chunkSize = E5LORA_RECVBUFF_DIM;  /* Clamp to prevent overflow */
                        }

                        {
                            size_t usedLen = strnlen(e5lora.uart.recvBuff, E5LORA_RECVBUFF_DIM);
                            size_t remainingLen = (E5LORA_RECVBUFF_DIM - 1U) - usedLen;
                            size_t copyLen = chunkSize;

                            if (copyLen > remainingLen)
                            {
                                copyLen = remainingLen;
                            }

                            if (copyLen > 0U)
                            {
                                memcpy(&e5lora.uart.recvBuff[usedLen], e5lora.uart.recvChunk, copyLen);
                                e5lora.uart.recvBuff[usedLen + copyLen] = '\0';
                            }

                            e5lora.uart.sizeRx = strnlen(e5lora.uart.recvBuff, E5LORA_RECVBUFF_DIM);
                        }

                        // expected ack
                        ESP_LOGD(TAG, "Wait for ACK of size %d: %s", sizeof(e5lora.uart.expected_ack), e5lora.uart.expected_ack);
                        // received message
                        ESP_LOGD(TAG, "Received message of size %d: %s", e5lora.uart.sizeRx, e5lora.uart.recvBuff);

                        if (E5LORA_JOIN_ACK == op.ackFlagIndex)
                        {
                            if (NULL != strstr(e5lora.uart.recvBuff, "+JOIN: Joined already"))
                            {
                                e5lora.joinResult = E5LORA_JOIN_RESULT_RETAINED;
                                e5lora.ack[op.ackFlagIndex] = true;
                                e5lora.uart.uartState = E5LORA_UART_STATE_DONE;
                                ESP_LOGD(TAG, "Retained join session detected");
                                break;
                            }
                            else if (NULL != strstr(e5lora.uart.recvBuff, "+JOIN: Join failed"))
                            {
                                e5lora.joinResult = E5LORA_JOIN_RESULT_FAILED;
                                e5lora.error = true;
                                memset(e5lora.errorStr, 0, E5_LORA_ERROR_STR_MAX_LEN);
                                snprintf((char *)e5lora.errorStr, E5_LORA_ERROR_STR_MAX_LEN, "%s", "Join failed");
                                e5lora.uart.uartState = E5LORA_UART_STATE_ERROR;
                                break;
                            }
                        }

                        if (NULL != strstr(e5lora.uart.recvBuff, "LoRaWAN modem is busy"))
                        {
                            e5lora.error = true;
                            memset(e5lora.errorStr, 0, E5_LORA_ERROR_STR_MAX_LEN);
                            snprintf((char *)e5lora.errorStr, E5_LORA_ERROR_STR_MAX_LEN, "%s", "Modem busy");
                            e5lora.uart.uartState = E5LORA_UART_STATE_ERROR;
                            break;
                        }
                        else if (NULL != strstr(e5lora.uart.recvBuff, "Please join network first"))
                        {
                            e5lora.error = true;
                            memset(e5lora.errorStr, 0, E5_LORA_ERROR_STR_MAX_LEN);
                            snprintf((char *)e5lora.errorStr, E5_LORA_ERROR_STR_MAX_LEN, "%s", "Join required");
                            e5lora.uart.uartState = E5LORA_UART_STATE_ERROR;
                            break;
                        }

                        // check for substring <expected_ack> inside recvBuff
                        if (NULL != strstr(e5lora.uart.recvBuff, e5lora.uart.expected_ack))
                        {
                            /* substring found */
                            e5lora.uart.uartState = E5LORA_UART_STATE_DONE;
                            ESP_LOGD(TAG, "Ack received: %s.", e5lora.uart.recvBuff);

                            /* Set ack flag if this operation corresponds to an init step */
                            if (e5lora.state < E5LORA_STATE_CONNECTED && op.ackFlagIndex != E5LORA_ACK_FLAG_NONE)
                            {
                                ESP_LOGD(TAG, "Init step %d completed", op.ackFlagIndex);
                                e5lora.ack[op.ackFlagIndex] = true;
                                if (E5LORA_JOIN_ACK == op.ackFlagIndex)
                                {
                                    e5lora.joinResult = E5LORA_JOIN_RESULT_FRESH;
                                }
                            }
                        }
                        else
                        {
                            // wait for matching ack
                        }
                        /* TODO da capire se la stringa attesa è sempre l'ultima o meno
                        *  Se non è l'ultima non devo continuare a confrontare
                        */
                    }
                    else if (RES_BUFFER_EMPTY != res)
                    {
                        e5lora.uart.uartState = E5LORA_UART_STATE_ERROR;
                    }
                    else
                    {
                        /* No bytes yet. */
                    }
                }
                break;
            }
            case (E5LORA_UART_STATE_DONE):
            {
                // communicate the successful communication, save data and then go to idle
                if (NULL != op.cb)
                {
                    // TODO use callback to set init step as completed or to signal correct ack
                    op.cb(RES_OK, (void*)NULL);
                }
                // reset buffer to get ready for a new message
                memset(op.cmd, 0U, E5_LORA_BUFFER_DIM);
                memset(op.expectedAck, 0U, E5_LORA_BUFFER_DIM);
                memset(e5lora.uart.recvBuff, 0U, E5LORA_RECVBUFF_DIM);
                e5lora.uart.uartState = E5LORA_UART_STATE_IDLE;
                break;
            }
            case (E5LORA_UART_STATE_NORESPONSE):
            {
                // communicate the unsuccessfull communication and then go to idle
                ESP_LOGW(TAG, "Write done, but no expected response from E5. Please check the device or the connections.\n");
                if (NULL != op.cb)
                {
                    op.cb(RES_ERROR_TIMEOUT, (void*)NULL);
                }
                e5lora.uart.uartState = E5LORA_UART_STATE_IDLE;
                break;
            }
            case (E5LORA_UART_STATE_ERROR):
            {
                ESP_LOGE(TAG, "Error during uart communication. Please check the device or the connections.\n");
                if (NULL != op.cb)
                {
                    op.cb(RES_ERROR, (void*)NULL);
                }
                e5lora.uart.uartState = E5LORA_UART_STATE_IDLE;
                break;
            }
            default:
            {
                // do nothing
                break;
            }
        }
    }
    /* No BSP accesses or callbacks after this acknowledgment. */
    xEventGroupSetBits(e5lora.events, E5_UART_STOPPED);
    vTaskDelete(NULL);
}

res_t E5_Lora_Enqueue_At_Command(const e5lora_uart_operation_t *op)
{
    res_t res = RES_ERROR_INVALID_PARAMETER;
    if (NULL != op)
    {
        res = RES_ERROR_NOT_READY;
        if ((NULL != e5lora.commandLock) && (NULL != e5lora.uart.opQueueHandle) &&
            (true == e5lora.uartReady) && (pdTRUE == xSemaphoreTake(e5lora.commandLock, 0U)))
        {
            /* The private sleep callback identifies the sole shutdown command. */
            if ((false == E5Lora_ShutdownRequested()) || (E5Lora_Sleep_Cb == op->cb))
            {
                res = RES_BUFFER_FULL;
                if (pdTRUE == xQueueSend(e5lora.uart.opQueueHandle, op, 0U))
                {
                    res = RES_OK;
                }
            }
            xSemaphoreGive(e5lora.commandLock);
        }
    }
    return res;
}

res_t Check_Init_Steps(void)
{
    res_t res = RES_ERROR;
    bool_t tmp = true;
    
    for(uint8_t idx=E5LORA_INIT_STEP_ID; idx<E5LORA_INIT_STEP_APPKEY+1; idx++)
    {
        tmp &= e5lora.ack[idx];
    }

    if (true == tmp)
    {
        res = RES_OK;
    }
    else
    {
        res = RES_OPERATION_IN_PROGRESS;
    }

    return res;
}

static res_t BuildMsghex(char *msghex, size_t *msghexLen, const uint8_t *payload, size_t payloadLen)
{
    static const char hexdigits[] = "0123456789ABCDEF";
    const char prefix[] = "AT+MSGHEX=\"";
    size_t prefix_len = strlen(prefix);
    size_t requiredLen;

    if ((NULL == msghex) || (NULL == msghexLen) || (NULL == payload))
    {
        return RES_ERROR_INVALID_PARAMETER;
    }

    if (payloadLen > E5LORA_MAX_PAYLOAD_LENGTH)
    {
        return RES_ERROR_INVALID_PARAMETER;
    }

    requiredLen = prefix_len + (2U * payloadLen) + 3U;
    if (requiredLen >= E5_LORA_CMD_MAX_LENGTH_BYTE)
    {
        return RES_ERROR_INVALID_PARAMETER;
    }

    memcpy(msghex, prefix, prefix_len);

    for (size_t i = 0; i < payloadLen; ++i)
    {
        uint8_t b = payload[i];
        msghex[prefix_len + i * 2U + 0U] = hexdigits[(b >> 4) & 0x0FU];
        msghex[prefix_len + i * 2U + 1U] = hexdigits[b & 0x0FU];
    }

    msghex[requiredLen - 3U] = '"';
    msghex[requiredLen - 2U] = '\r';
    msghex[requiredLen - 1U] = '\n';
    msghex[requiredLen] = '\0';

    *msghexLen = requiredLen;
    return RES_OK;
}

/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/
