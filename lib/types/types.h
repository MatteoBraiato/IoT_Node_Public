/* \file types.h
 *
 * \defgroup types TYPES
 *
 * History:
 * - 25/07/24: First Release
 * @author Massimo Dian
 *
 * Lista delle Define dei compilatori. Puo' essere trovata qui
 * https://sourceforge.net/p/predef/wiki/Compilers/
 * __MWERKS__ : Freescale Codewarrior 5.1
 * __GNUC__ : gcc and version
 * __CC_ARM : ARM Compiler
 * __ARMCC_VERSION: Compiler version
 * __ICCARM__ : IAR Compiler
 * _MSC_VER : Microsoft Visual C++
 *
 * Define in base allo standard. Puo' essere trovata qui:
 * https://sourceforge.net/p/predef/wiki/Standards/
 * __STDC_VERSION__ >= 199901L : C99
 * __STDC_VERSION__ >= 201112L : C11
 * __cplusplus : Compilatore c++
 *
 * Define in base al sistema operativo. Puo' essere trovata qui:
 * https://sourceforge.net/p/predef/wiki/OperatingSystems/
 * __linux__ -> Linux, almeno c'e' un kernel Linux
 * __WINDOWS__ -> Windows
 *
 *
 */
#ifndef TYPES_H_
#define TYPES_H_

#ifdef __cplusplus
extern "C" 
{
#endif /* __cplusplus */

/**************************************************************************************************
 *  1.  INCLUDE FILES                                                                             *
 **************************************************************************************************/
#if defined (__MWERKS__)
#include <hiware.h>
#endif
#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 199409L
#endif
/* Microsoft Visual Studio supporta solo C89 ufficialmente. In realta' i file .h del C99 sono supportati */
#if (__STDC_VERSION__ >= 199901L) || defined (_MSC_VER) || defined(__cplusplus)
#include <stdbool.h>
#include <stdint.h>
#endif
#include <stdlib.h>
#include <stddef.h>
/**************************************************************************************************
 *  2.  PUBLIC DEFINES                                                                            *
 **************************************************************************************************/
#ifndef TRUE
#define TRUE    1U
#endif
#ifndef FALSE
#define FALSE   0U
#endif
#define TYPES_VERSION_H               1
#define TYPES_VERSION_M               0
#define TYPES_VERSION_L               0

/**************************************************************************************************
 *  3.  PUBLIC MACROS                                                                             *
 **************************************************************************************************/
#ifndef EUNUSED
#define EUNUSED(x) ((void)x)
#endif /* EUNUSED */
#ifndef TOSTRING
#define _TOSTRING(x) #x
#define TOSTRING(x) _TOSTRING(x)
#endif
#ifdef USE_EASSERT
#define EASSERT(x) {if (!(x)) {while(1);}}
#else
#define EASSERT(x)
#endif /* USE_EASSERT */

#if defined(__GNUC__) && (__GNUC__ > 7)
#define EFALLTHROUGH __attribute__((fallthrough))
#else
#define EFALLTHROUGH
#endif
/**************************************************************************************************
 *  4.  PUBLIC ENUMERATIONS                                                                       *
 **************************************************************************************************/

/**************************************************************************************************
 *  5.  PUBLIC STRUCTURES/UNIONS                                                                  *
 **************************************************************************************************/

/**************************************************************************************************
 *  6.  PUBLIC TYPES                                                                              *
 **************************************************************************************************/
#if (__STDC_VERSION__ < 199901L) && (!defined (_MSC_VER)) && (!defined(__cplusplus))
#if defined (__MWERKS__)
typedef uint8   uint8_t;
typedef int8    int8_t;
typedef uint16  uint16_t;
typedef int16   int16_t;
typedef uint32  uint32_t;
typedef int32   int32_t;
typedef uint16  uint_fast8_t;
typedef uint16  uint_fast16_t;
typedef uint32  uint_fast32_t;
typedef int16   int_fast8_t;
typedef int16   int_fast16_t;
typedef int32   int_fast32_t;
#else
typedef unsigned char   uint8_t;
typedef signed char     int8_t;
typedef unsigned short  uint16_t;
typedef signed short    int16_t;
typedef unsigned long   uint32_t;
typedef signed long     int32_t;
typedef uint32_t        uint_fast8_t;
typedef uint32_t        uint_fast16_t;
typedef uint32_t        uint_fast32_t;
typedef int32_t         int_fast8_t;
typedef int32_t         int_fast16_t;
typedef int32_t         int_fast32_t;
#define UINT32_MAX      0xFFFFFFFFUL

#endif /* defined (__MWERKS__) */

typedef enum
{
    false = 0,
    true  = 1
} bool;
#endif /* (__STDC_VERSION__ < 199901L) && (!defined (_MSC_VER)) && (!defined(__cplusplus)) */

#ifndef __BOOLEAN_T_TYPE__
#define __BOOLEAN_T_TYPE__
typedef uint8_t boolean_t;
#endif
#ifndef __BOOL_T_TYPE__
#define __BOOL_T_TYPE__
typedef bool    bool_t;
#endif

#ifndef __FLOAT32_T_TYPE__
#define __FLOAT32_T_TYPE__
/** single precision floating point number (4 byte) */
typedef float        float32_t;
#endif

#ifndef __FLOAT64_T_TYPE__
#define __FLOAT64_T_TYPE__
/** double precision floating point number (8 byte) */
typedef double       float64_t;
#endif

#ifndef __CHAR_T_TYPE__
#define __CHAR_T_TYPE__
/** ASCII character for string generation (8 bit) */
typedef char         char_t;
#endif

#ifndef __FUNC_PTR_T_TYPE__
/** function pointer type to void/void function */
typedef void         (*func_ptr_t)(void);
#define  __FUNC_PTR_T_TYPE__
#endif
/*! \defgroup types_enums types_enums
 * \ingroup types
 * \brief Enumeratives of types module
 * @{
 */

/*!
  * \brief  Function results
 */
typedef enum
{
    RES_OK                             =  0,  /*!< No error                                                                                       */
    RES_OPERATION_IN_PROGRESS          =  1,  /*!< Indicator for operation in progress (e.g. ADC conversion not finished, DMA channel used, etc.) */
    RES_BUFFER_FULL                    =  2,  /*!< Circular buffer can not be written because the buffer is full                                  */
    RES_BUFFER_EMPTY                   =  3,  /*!< Circular buffer can not be read because the buffer is empty                                    */
    RES_BUFFER_NOTFULL                 =  4,  /*!< Circular buffer not full, some operation could be not correct                                  */
    RES_ALREADY_INITIALIZED            =  5,  /*!< Module (or part of it) was already initialized                                                 */
    RES_ERROR                          = -1,  /*!< Non-specific error code                                                                        */
    RES_ERROR_ADDRESS_ALIGNMENT        = -2,  /*!< Address alignment does not match                                                               */
    RES_ERROR_ACCESS_RIGHTS            = -3,  /*!< Wrong mode (e.g. user/system) mode is set                                                      */
    RES_ERROR_INVALID_PARAMETER        = -4,  /*!< Provided parameter is not valid                                                                */
    RES_ERROR_INVALID_MODE             = -5,  /*!< Operation not allowed in current mode                                                          */
    RES_ERROR_UNINITIALIZED            = -6,  /*!< Module (or part of it) was not initialized properly                                            */
    RES_ERROR_TIMEOUT                  = -7,  /*!< Time Out error occurred (e.g. I2C arbitration lost, Flash time-out, etc.)                      */
    RES_ERROR_NOT_READY                = -8,  /*!< A requested final state is not reached                                                         */
    RES_ERROR_NOT_IMPLEMENTED          = -9,  /*!< L'operazione richiesta non e' ancora stata implementata, controllare la configurazione         */
    RES_ERROR_MEMORYCORRUPTION         = -10, /*!< A memory corruption has been identified                                                        */
    RES_ERROR_OUTOFMEMORY              = -11, /*!< Operation cannot be performed because a memory allocation failed                               */
    RES_ERROR_DEMOEXPIRED              = -12, /*!< Demo functionality expired                                                                     */
} res_t;

typedef enum
{
    DATATYPE_INT8        ,
    DATATYPE_UINT8       ,
    DATATYPE_INT16       ,
    DATATYPE_UINT16      ,
    DATATYPE_INT32       ,
    DATATYPE_UINT32      ,
    DATATYPE_ENUM        ,
    DATATYPE_BOOL        ,
    DATATYPE_NUM
} datatype_t;


/*! @} */ /* types_enums */
/* For retro-compatibility -> To be deprecated */
#ifndef __FUNC_PTR_RESULT_CB_T_TYPE__
/** function pointer type to void/void function*/
typedef void        (*result_cb_t)(void* data, res_t res);
#define __FUNC_PTR_RESULT_CB_T_TYPE__
#endif // __FUNC_PTR_RESULT_CB_T_TYPE__

#ifndef __FUNC_PTR_RES_CB_T_TYPE__
/** function pointer type to void/void function*/
typedef void        (*res_cb_t)(void* data, res_t res);
#define __FUNC_PTR_RES_CB_T_TYPE__
#endif // __FUNC_PTR_RES_CB_T_TYPE__


#ifndef __FUNC_PTR_RES_CONSTCB_T_TYPE__
/** function pointer type to const void/void function*/
typedef void        (*res_constcb_t)(const void* data, res_t res);
#define __FUNC_PTR_RES_CONSTCB_T_TYPE__
#endif // __FUNC_PTR_RES_CONSTCB_T_TYPE__

#ifndef __ASYNC_CB_TYPE_
/** \brief Structure used for passing callbacks on asynchronous operations.
 *  \details
 *   Some functions which executes time cosnsuming operation (such writing on flash, calculating
 *   checksum or CRC, writing or reading from EEPROM), can be implemented as asynchronous functions.
 *   The function just verify that the operation is potentially executable and triggers the start of
 *   operation. The user (caller) can provide this structure to the function to be informed
 *   of the result of the operation.
 *   The structure provide two fields:
 *   - an (optional) opaque pointer passed by the caller to get the context
 *   - a callback, which shall be called by function module when the operation is finished. It passes
 *     back the opaque pointer to the user.
 */
typedef struct
{
    void *userData;
    res_cb_t callback;
} async_cb_t;
#define __ASYNC_CB_TYPE_
#endif // __ASYNC_CB_TYPE_


/**************************************************************************************************
 *  7.  PUBLIC (EXTERN) VARIABLES DECLARATIONS                                                    *
 **************************************************************************************************/

/**************************************************************************************************
 *  8.  PUBLIC FUNCTION DECLARATIONS                                                              *
 **************************************************************************************************/

/**************************************************************************************************
 *  9.  PUBLIC INLINE FUNCTION DEFINITIONS                                                        *
 **************************************************************************************************/

 #ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TYPES_H_ */



/**************************************************************************************************
 *  EOF                                                                                           *
 **************************************************************************************************/