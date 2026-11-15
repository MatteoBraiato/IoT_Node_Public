/**************************************************************************************************
 * Copyright (c) 2025 - Matteo Braiato - All rights reserved.                          *
 **************************************************************************************************/
/* @file bdi_i2c.h
 * Brief description
 */

#ifndef bdi_i2c_H_
#define bdi_i2c_H_
#ifdef __cplusplus
extern "C" 
{
#endif /* __cplusplus */

/**************************************************************************************************
 *  1.  INCLUDE FILES                                                                             *
 **************************************************************************************************/
#include <types/types.h>
/**************************************************************************************************
 *  2.  PUBLIC DEFINES                                                                            *
 **************************************************************************************************/

/**************************************************************************************************
 *  3.  PUBLIC MACROS                                                                             *
 **************************************************************************************************/

/**************************************************************************************************
 *  4.  PUBLIC ENUMERATIONS                                                                       *
 **************************************************************************************************/
enum bdi_i2c_operation_type_e
{
	BDI_I2C_OP_READ,
	BDI_I2C_OP_WRITE,
	BDI_I2C_OP_READWRITE
};

enum bdi_i2c_op_result_e
{
	BDI_I2C_RES_DONE,
	BDI_I2C_RES_FAIL
};
typedef enum bdi_i2c_op_result_e bdi_i2c_op_result_t;
typedef void (*opResultCb_t)(bdi_i2c_op_result_t res, void *userData);
/**************************************************************************************************
 *  5.  PUBLIC STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/
struct bdi_i2c_operation_s
{
	enum bdi_i2c_operation_type_e type;
	uint8_t devAddr;
	uint16_t regAddr;
	uint8_t regAddrSize;
    uint8_t *writeBuffer;
    uint16_t writeBufferSize;
    uint8_t *readBuffer;
    uint16_t readBufferSize;
    opResultCb_t cb;
    void *userData;
}__attribute__ ((packed));
/**************************************************************************************************
 *  6.  PUBLIC TYPES                                                                              *
 **************************************************************************************************/
typedef struct bdi_i2c_operation_s bdi_i2c_operation_t;
/**************************************************************************************************
 *  7.  PUBLIC (EXTERN) VARIABLES DECLARATIONS                                                    *
 **************************************************************************************************/

/**************************************************************************************************
 *  8.  PUBLIC FUNCTION DECLARATIONS                                                              *
 **************************************************************************************************/
res_t Bdi_I2c_Init(void);
res_t Bdi_I2c_Deinit(void);
res_t Bdi_I2c_Start(void);
res_t Bdi_I2c_Stop(void);
res_t Bdi_I2c_WriteData(uint8_t devAddr, uint8_t regAddr, uint8_t regAddrSize, uint8_t *data, size_t len, opResultCb_t cb);
res_t Bdi_I2c_ReadData(uint8_t devAddr, uint8_t regAddr, uint8_t regAddrSize, uint8_t *data, size_t len, opResultCb_t cb);
res_t Bdi_I2c_WriteReadData(uint8_t devAddr, uint8_t regAddr, uint8_t regAddrSize, uint8_t *writeBuff, size_t writeBuffLen, uint8_t *readBuff, size_t readBuffLen, opResultCb_t cb);
/**************************************************************************************************
 *  9.  PUBLIC INLINE FUNCTION DEFINITIONS                                                        *
 **************************************************************************************************/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* bdi_i2c_H_ */

/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/