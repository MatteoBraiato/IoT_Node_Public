#ifndef BSP_I2C_H_
#define BSP_I2C_H_

#include "types/types.h"

/* Call after all sensor operations have completed. Safe after partial init. */
res_t Bsp_I2c_Shutdown(void);

#endif
