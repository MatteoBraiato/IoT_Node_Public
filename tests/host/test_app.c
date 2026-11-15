#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../../src/app.c"
#include "../../main/main.c"

static jmp_buf sleeping;
static char order[5];
static unsigned step;
static res_t bmeInit = RES_OK;
static res_t e5Init = RES_OK;
static res_t shutdownResult[4];
static res_t recorded(unsigned index, char name)
{
    assert(step == index && step < 4U); order[step++] = name;
    return shutdownResult[index];
}
res_t Bsp_Max1704_Deinit(void) { return recorded(0, 'M'); }
res_t Bsp_Bme280_DeInit(void) { return recorded(1, 'S'); }
res_t Bsp_I2c_Shutdown(void) { return recorded(2, 'I'); }
res_t E5Lora_DeInit(void) { return recorded(3, 'L'); }
res_t Bsp_Di_InitPin(bsp_di_pin_t pin, bsp_di_pin_pull_type_t pull) { (void)pin; (void)pull; return RES_OK; }
res_t Bsp_Di_GetState(bsp_di_pin_t pin, bsp_di_pin_status_t *state) { (void)pin; *state = BSP_DI_HIGH; return RES_OK; }
res_t Bsp_Bme280_Init(void) { return bmeInit; }
res_t Bsp_Max1704_Init(void) { return RES_OK; }
res_t E5Lora_Init(void) { return e5Init; }
res_t Bsp_Bme280_Read_Raw(int32_t *t, uint32_t *h, uint32_t *p) { *t = 210; *h = 500; *p = 100000; return RES_OK; }
res_t Bsp_Max1704_Read_Soc(uint8_t *soc) { *soc = 70; return RES_OK; }
res_t Bsp_Max1704_Read_Voltage(uint16_t *v) { *v = 400; return RES_OK; }
res_t E5Lora_EnsureConnected(bool_t *joined) { *joined = false; return RES_ERROR_NOT_READY; }
res_t E5Lora_SendMsgHex(uint8_t *data, size_t size, opResultCb_t cb) { (void)data; (void)size; (void)cb; assert(false); return RES_ERROR; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return ESP_SLEEP_WAKEUP_TIMER; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t duration)
{
    assert(step == 4U && strcmp(order, "MSIL") == 0);
    assert(duration == (uint64_t)APP_SLEEP_INTERVAL_S * 1000000ULL);
    return ESP_OK;
}
void esp_deep_sleep_start(void) { longjmp(sleeping, 1); }
TaskHandle_t xTaskCreateStaticPinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
        void *arg, unsigned priority, StackType_t *memory, StaticTask_t *task, int core)
{
    (void)name; (void)stack; (void)priority; (void)memory; (void)task; (void)core;
    fn(arg); return NULL;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 1024; }
int main(int argc, char **argv)
{
    assert(argc == 2); const char *test = argv[1];
    if (strcmp(test, "bme_init_fail") == 0) { bmeInit = RES_ERROR; }
    if (strcmp(test, "e5_init_fail") == 0) { e5Init = RES_ERROR; }
    if (strcmp(test, "cleanup_fail") == 0) { shutdownResult[1] = RES_ERROR; shutdownResult[3] = RES_ERROR_TIMEOUT; }
    if (strcmp(test, "first_cleanup_error") == 0)
    {
        shutdownResult[0] = RES_ERROR_TIMEOUT; shutdownResult[1] = RES_ERROR;
        assert(App_Shutdown_Peripherals() == RES_ERROR_TIMEOUT);
        assert(step == 4U && strcmp(order, "MSIL") == 0);
    }
    else
    {
        if (setjmp(sleeping) == 0) { app_main(); assert(false); }
        assert(step == 4U && strcmp(order, "MSIL") == 0);
    }
    printf("PASS app_%s\n", test); return 0;
}
