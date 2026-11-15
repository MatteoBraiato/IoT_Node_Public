#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../system/bsp/bsp_bme280.c"
static unsigned sleepWrites;
static res_t busInit = RES_OK;
static int8_t sleepResult = BME280_OK;
res_t Bdi_I2c_Init(void) { return busInit; }
res_t Bdi_I2c_Start(void) { return RES_OK; }
res_t Bdi_I2c_Stop(void) { assert(false); return RES_OK; }
res_t Bdi_I2c_Deinit(void) { assert(false); return RES_OK; }
res_t Bdi_I2c_ReadData(uint8_t a, uint8_t r, uint8_t s, uint8_t *d, size_t n, opResultCb_t cb)
{ (void)a; (void)r; (void)s; (void)d; (void)n; (void)cb; return RES_ERROR_UNINITIALIZED; }
res_t Bdi_I2c_WriteData(uint8_t a, uint8_t r, uint8_t s, uint8_t *d, size_t n, opResultCb_t cb)
{ (void)a; (void)r; (void)s; (void)d; (void)n; (void)cb; return RES_ERROR_UNINITIALIZED; }
int8_t bme280_set_sensor_mode(uint8_t mode, struct bme280_dev *dev)
{
    assert(mode == BME280_POWERMODE_SLEEP);
    assert(dev->read && dev->write && dev->intf_ptr);
    assert(*(uint8_t *)dev->intf_ptr == BME280_I2C_ADDR_PRIM);
    ++sleepWrites; return sleepResult;
}
int main(int argc, char **argv)
{
    assert(argc == 2); const char *test = argv[1];
    if (strcmp(test, "bus_fail") == 0) { busInit = RES_ERROR; }
    if (strcmp(test, "sleep_fail") == 0) { sleepResult = BME280_E_COMM_FAIL; }
    if (strcmp(test, "uninitialized") != 0)
    {
        (void)bme280_interface_selection(&bspBme280.dev, BME280_I2C_INTF);
        /* Simulate a later sensor-init failure after interface setup succeeded. */
        assert(!bspBme280.init);
    }
    bool available = strcmp(test, "uninitialized") != 0 && busInit == RES_OK;
    assert(Bsp_Bme280_DeInit() == (available ? (sleepResult == BME280_OK ? RES_OK : RES_ERROR) : RES_ERROR_UNINITIALIZED));
    assert(sleepWrites == (available ? 1U : 0U));
    assert(Bsp_Bme280_DeInit() == RES_ERROR_UNINITIALIZED);
    assert(sleepWrites == (available ? 1U : 0U));
    printf("PASS bme_%s\n", test); return 0;
}
