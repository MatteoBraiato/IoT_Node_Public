#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TAG uartTag
#include "../../system/bsp/bsp_uart.c"
#undef TAG
#define TAG i2cTag
#include "../../system/bdi/bdi_i2c.c"
#undef TAG
#include "../../system/bsp/bsp_i2c.c"

static bool driverAlive;
static bool busAlive;
static unsigned uartDeletes, i2cDeletes;
static bool holdHigh;
static bool gpioFailure;
static bool drainFailure;
static bool parameterFailure;
static bool writeFailure;
static atomic_bool writeActive;
static bool blockWrite;
static bool readFailure;
static bool busTimeout;
static atomic_bool busTransaction;
static StaticQueue_t eventsQueue;
static uint8_t eventsBuffer[20 * sizeof(uart_event_t)];
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) { assert(pin == BSP_UART_PIN_TX && level == 1); return ESP_OK; }
esp_err_t gpio_config(const gpio_config_t *cfg) { (void)cfg; return ESP_OK; }
esp_err_t gpio_hold_en(gpio_num_t pin)
{
    assert(pin == BSP_UART_PIN_TX);
    esp_err_t result = ESP_FAIL;
    if (!gpioFailure)
    {
        holdHigh = true;
        result = ESP_OK;
    }
    return result;
}
esp_err_t gpio_hold_dis(gpio_num_t pin) { (void)pin; holdHigh = false; return ESP_OK; }
void gpio_deep_sleep_hold_en(void) {}
void gpio_deep_sleep_hold_dis(void) {}
esp_err_t uart_driver_install(int port, int rx, int tx, int count, QueueHandle_t *queue, int flags)
{
    (void)port; (void)rx; (void)tx; (void)flags;
    driverAlive = true;
    *queue = xQueueCreateStatic((size_t)count, sizeof(uart_event_t), eventsBuffer, &eventsQueue);
    return ESP_OK;
}
esp_err_t uart_param_config(int port, const uart_config_t *config) { (void)port; (void)config; return parameterFailure ? ESP_FAIL : ESP_OK; }
esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts) { (void)port; (void)tx; (void)rx; (void)rts; (void)cts; return ESP_OK; }
esp_err_t uart_wait_tx_done(int port, TickType_t wait)
{
    (void)port; assert(driverAlive); assert(wait == pdMS_TO_TICKS(500U));
    assert(!bspUart.workerCreated);
    return drainFailure ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t uart_driver_delete(int port)
{
    (void)port; assert(driverAlive && !bspUart.workerCreated);
    assert(holdHigh || gpioFailure);
    driverAlive = false; ++uartDeletes; return ESP_OK;
}
int uart_write_bytes(int port, const void *data, size_t size)
{
    (void)port; (void)data; assert(driverAlive);
    writeActive = true;
    if (blockWrite) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
    assert(driverAlive);
    writeActive = false;
    return writeFailure ? -1 : (int)size;
}
int uart_read_bytes(int port, void *data, size_t size, TickType_t wait) { (void)port; (void)data; (void)wait; assert(driverAlive); return readFailure ? -1 : (int)size; }
esp_err_t uart_flush_input(int port) { (void)port; assert(driverAlive); return ESP_OK; }
esp_err_t i2c_param_config(int port, const i2c_config_t *config) { (void)port; (void)config; return ESP_OK; }
esp_err_t i2c_driver_install(int port, int mode, int rx, int tx, int flags) { (void)port; (void)mode; (void)rx; (void)tx; (void)flags; busAlive = true; return ESP_OK; }
esp_err_t i2c_driver_delete(int port)
{
    (void)port; assert(busAlive && !bdiI2c.workerCreated && !busTransaction);
    busAlive = false; ++i2cDeletes; return ESP_OK;
}
esp_err_t i2c_master_write_read_device(int port, uint8_t addr, const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxLen, TickType_t wait)
{
    (void)port; (void)addr; (void)tx; (void)txLen; (void)rx; (void)rxLen; (void)wait;
    assert(busAlive); return ESP_OK;
}
esp_err_t i2c_master_write_to_device(int port, uint8_t addr, const uint8_t *tx, size_t len, TickType_t wait)
{
    (void)port; (void)addr; (void)tx; (void)len;
    assert(busAlive); busTransaction = true;
    vTaskDelay(busTimeout ? pdMS_TO_TICKS(2000U) : wait);
    assert(busAlive); busTransaction = false; return ESP_ERR_TIMEOUT;
}
int main(int argc, char **argv)
{
    assert(argc == 2); const char *test = argv[1];
    if (strncmp(test, "uart_", 5) == 0)
    {
        if (strcmp(test, "uart_partial") == 0) { parameterFailure = true; }
        if (strcmp(test, "uart_task_fail") == 0) { hostFailTask = "Bsp_Uart_Task"; }
        if (strcmp(test, "uart_queue_fail") == 0) { hostFailQueue = true; }
        if (strcmp(test, "uart_uninitialized") != 0)
        {
            res_t r = Bsp_Uart_Init(9600U);
            assert(r == ((parameterFailure || hostFailTask || hostFailQueue) ? RES_ERROR : RES_OK));
        }
        gpioFailure = strcmp(test, "uart_gpio_fail") == 0;
        drainFailure = strcmp(test, "uart_drain_fail") == 0;
        writeFailure = strcmp(test, "uart_write_fail") == 0;
        blockWrite = strcmp(test, "uart_stop_timeout") == 0;
        readFailure = strcmp(test, "uart_read_fail") == 0;
        if (writeFailure || blockWrite)
        {
            assert(Bsp_Uart_Send((const uint8_t *)"AT\r\n", 4) == RES_OK);
            if (blockWrite)
            {
                while (!writeActive) { vTaskDelay(1U); }
            }
            else
            {
                vTaskDelay(10U);
                uint8_t data[1024]; size_t count;
                assert(Bsp_Uart_Recv(data, sizeof(data), &count) == RES_ERROR);
            }
        }
        if (readFailure)
        {
            uart_event_t event = { .type = UART_DATA, .size = 8U };
            assert(xQueueSend(bspUart.evtQueue, &event, 0U) == pdTRUE);
            vTaskDelay(10U);
        }
        res_t expected = (drainFailure || blockWrite) ? RES_ERROR_TIMEOUT : ((gpioFailure || writeFailure || readFailure) ? RES_ERROR : RES_OK);
        assert(Bsp_Uart_DeInit() == expected);
        unsigned deleted = strcmp(test, "uart_uninitialized") == 0 ? 0U : 1U;
        if (blockWrite) { assert(driverAlive && uartDeletes == 0U); }
        else { assert(uartDeletes == deleted); }
        hostJoinTasks();
        (void)Bsp_Uart_DeInit(); assert(uartDeletes == deleted);
    }
    else
    {
        if (strcmp(test, "i2c_task_fail") == 0) { hostFailTask = "BDI_I2C_Task"; }
        if (strcmp(test, "i2c_queue_fail") == 0) { hostFailQueue = true; }
        if (strcmp(test, "i2c_uninitialized") != 0)
        {
            assert(Bdi_I2c_Init() == ((hostFailTask || hostFailQueue) ? RES_ERROR : RES_OK));
        }
        if ((strcmp(test, "i2c_inflight") == 0) || (strcmp(test, "i2c_stop_timeout") == 0))
        {
            static uint8_t data = 0;
            busTimeout = strcmp(test, "i2c_stop_timeout") == 0;
            assert(Bdi_I2c_WriteData(0x76U, 0xf4U, 1U, &data, 1U, NULL) == RES_OK);
            while (!busTransaction) { vTaskDelay(1U); }
        }
        res_t expected = busTimeout ? RES_ERROR_TIMEOUT : RES_OK;
        assert(Bsp_I2c_Shutdown() == expected);
        if (busTimeout) { assert(busAlive && i2cDeletes == 0); }
        hostJoinTasks();
        assert(Bsp_I2c_Shutdown() == RES_OK);
        assert(i2cDeletes == (strcmp(test, "i2c_uninitialized") == 0 ? 0U : 1U));
    }
    printf("PASS %s\n", test); return 0;
}
