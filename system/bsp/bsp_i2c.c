#include "bsp_i2c.h"
#include "bdi/bdi_i2c.h"

res_t Bsp_I2c_Shutdown(void)
{
    /* Deinit stops and acknowledges the worker before deleting the driver. */
    res_t res = Bdi_I2c_Deinit();
    if (RES_ERROR_UNINITIALIZED == res)
    {
        res = RES_OK;
    }
    return res;
}
