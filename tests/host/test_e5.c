/* Compile the real E5 implementation against a controllable UART and RTOS shim. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/e5lora.c"

static pthread_mutex_t rxLock = PTHREAD_MUTEX_INITIALIZER;
static char reply[256];
static size_t replyOffset;
static unsigned sleepCommands;
static bool fragment;
static const char *sleepReply = "+LOWPOWER: SLEEP\r\n";
static res_t initResult = RES_OK;
static res_t sendResult = RES_OK;
static res_t cleanupResult = RES_OK;
static unsigned cleanupCalls;
static bool failWake;

res_t Bsp_Uart_Init(uint32_t baud) { assert(baud == 9600); return initResult; }
res_t Bsp_Uart_DeInit(void)
{
    assert((xEventGroupGetBits(e5lora.events) & E5_UART_STOPPED) != 0);
    ++cleanupCalls;
    return cleanupResult;
}
res_t Bsp_Uart_Send(const uint8_t *buffer, size_t length)
{
    res_t result = RES_OK;
    if (length != 1)
    {
    pthread_mutex_lock(&rxLock);
    replyOffset = 0;
    reply[0] = '\0';
    if (strcmp((const char *)buffer, "AT+LOWPOWER\r\n") == 0)
    {
        ++sleepCommands;
        snprintf(reply, sizeof(reply), "%s", sleepReply);
    }
    else if ((strcmp((const char *)buffer, "AT\r\n") == 0) && !failWake)
    {
        snprintf(reply, sizeof(reply), "+AT: OK\r\n");
    }
    else if ((strncmp((const char *)buffer, "AT+", 3) == 0) &&
             (strcmp((const char *)buffer, "AT+BUSY\r\n") != 0))
    {
        /* Mirror expected configuration replies without logging credentials. */
        snprintf(reply, sizeof(reply), "%s", e5lora.uart.expected_ack);
    }
    pthread_mutex_unlock(&rxLock);
    result = sendResult;
    }
    return result;
}
res_t Bsp_Uart_Send_Hex(const uint8_t *buffer, size_t size) { return Bsp_Uart_Send(buffer, size); }
res_t Bsp_Uart_Recv(uint8_t *buffer, size_t size, size_t *length)
{
    pthread_mutex_lock(&rxLock);
    size_t n = strlen(reply + replyOffset);
    if (fragment && n > 3) { n = 3; }
    assert(n <= size);
    memcpy(buffer, reply + replyOffset, n); replyOffset += n; *length = n;
    pthread_mutex_unlock(&rxLock);
    return n ? RES_OK : RES_BUFFER_EMPTY;
}
static void transport(void)
{
    e5lora.initAttempted = true;
    e5lora.uartReady = true;
    e5lora.events = xEventGroupCreateStatic(&e5lora.eventsStorage);
    e5lora.commandLock = xSemaphoreCreateMutexStatic(&e5lora.commandLockStorage);
    xEventGroupSetBits(e5lora.events, E5_MAIN_STOPPED);
    e5lora.uart.opQueueHandle = xQueueCreateStatic(E5_LORA_MAX_OP_NUM,
            sizeof(e5lora_uart_operation_t), e5lora.uart.queueBuffer, &e5lora.uart.opQueue);
    e5lora.uart.uartState = E5LORA_UART_STATE_IDLE;
    e5lora.uartTaskCreated = true;
    assert(pdPASS == xTaskCreate(Uart_Send_Receive_Task, "test transport", 0, NULL, 0, NULL));
}
static void testFullQueue(void)
{
            e5lora.initAttempted = true; e5lora.uartReady = true;
            e5lora.events = xEventGroupCreateStatic(&e5lora.eventsStorage);
            e5lora.commandLock = xSemaphoreCreateMutexStatic(&e5lora.commandLockStorage);
            e5lora.uart.opQueueHandle = xQueueCreateStatic(E5_LORA_MAX_OP_NUM,
                    sizeof(e5lora_uart_operation_t), e5lora.uart.queueBuffer, &e5lora.uart.opQueue);
            e5lora_uart_operation_t op = {0};
            for (unsigned i = 0; i < E5_LORA_MAX_OP_NUM; ++i) { assert(xQueueSend(e5lora.uart.opQueueHandle, &op, 0) == pdTRUE); }
            assert(E5Lora_Enter_LowPower() == RES_BUFFER_FULL);
            assert(sleepCommands == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *test = argv[1];
    res_t expected = RES_OK;
    unsigned commands = 1;
    if (strcmp(test, "uninitialized") == 0)
    {
        assert(E5Lora_DeInit() == RES_ERROR_UNINITIALIZED);
        assert(cleanupCalls == 0);
    }
    else if (strcmp(test, "queue_full") == 0)
    {
        testFullQueue();
    }
    else
    {
        if (strcmp(test, "fragmented") == 0) { fragment = true; }
        if (strcmp(test, "unexpected") == 0) { sleepReply = "OK\r\n"; expected = RES_ERROR_TIMEOUT; }
        if (strcmp(test, "missing") == 0) { sleepReply = ""; expected = RES_ERROR_TIMEOUT; }
        if (strcmp(test, "send_error") == 0) { sendResult = RES_ERROR; expected = RES_ERROR; }
        if (strcmp(test, "cleanup_error") == 0) { cleanupResult = RES_ERROR; expected = RES_ERROR; }
        if (strcmp(test, "first_error") == 0)
        { sleepReply = ""; cleanupResult = RES_ERROR; expected = RES_ERROR_TIMEOUT; }
        if (strcmp(test, "rollover") == 0) { hostTickOffset = UINT32_MAX - xTaskGetTickCount() - 5U; fragment = true; }
        if (strcmp(test, "long_uptime") == 0) { hostTickOffset = 7000U - xTaskGetTickCount(); }
        if (strcmp(test, "init_uart_fail") == 0) { initResult = RES_ERROR; commands = 0; expected = RES_ERROR_NOT_READY; }
        if (strcmp(test, "init_queue_fail") == 0) { hostFailQueue = true; commands = 0; expected = RES_ERROR_NOT_READY; }
        if (strcmp(test, "init_worker_fail") == 0) { hostFailTask = "E5_LORA_Uart_Task"; commands = 0; expected = RES_ERROR_NOT_READY; }
        if (strcmp(test, "init_main_fail") == 0) { hostFailTask = "E5_LORA_Task"; }
        if (strcmp(test, "init_config_fail") == 0) { failWake = true; }
        if (strncmp(test, "init_", 5) == 0) { assert(E5Lora_Init() != RES_OK); }
        else if (strcmp(test, "normal_init") == 0) { assert(E5Lora_Init() == RES_OK); }
        else { transport(); }
        if (strcmp(test, "busy") == 0)
        {
            assert(Send_At_Check_Response("never", 30000, "AT+BUSY\r\n", SEND_AT_CHECK_RESPONSE_NO_LEN, E5LORA_ACK_FLAG_NONE, NULL) == RES_OK);
            expected = RES_ERROR_TIMEOUT; commands = 0;
        }
        TickType_t start = xTaskGetTickCount();
        res_t actual = E5Lora_DeInit();
        assert(actual == expected);
        TickType_t elapsed = xTaskGetTickCount() - start;
        assert(elapsed < pdMS_TO_TICKS(7000U));
        assert(sleepCommands == commands);
        assert(cleanupCalls == 1);
        assert(E5Lora_DeInit() == expected);
        assert(sleepCommands == commands && cleanupCalls == 1);
        assert(E5Lora_Join() == RES_ERROR_NOT_READY);
        bool_t joined = false;
        assert(E5Lora_EnsureConnected(&joined) == RES_ERROR_NOT_READY);
        hostJoinTasks();
    }
    printf("PASS %s\n", test);
    return 0;
}
