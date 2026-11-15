#ifndef HOST_I2C_H
#define HOST_I2C_H
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#define I2C_MODE_MASTER 1
typedef struct { int mode, sda_io_num, scl_io_num, sda_pullup_en, scl_pullup_en; struct { uint32_t clk_speed; } master; } i2c_config_t;
esp_err_t i2c_param_config(int port, const i2c_config_t *config);
esp_err_t i2c_driver_install(int port, int mode, int rx, int tx, int flags);
esp_err_t i2c_driver_delete(int port);
esp_err_t i2c_master_write_read_device(int port, uint8_t addr, const uint8_t *tx, size_t txLen, uint8_t *rx, size_t rxLen, TickType_t wait);
esp_err_t i2c_master_write_to_device(int port, uint8_t addr, const uint8_t *tx, size_t len, TickType_t wait);
#endif
