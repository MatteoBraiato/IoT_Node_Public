#ifndef HOST_UART_H
#define HOST_UART_H
#include "esp_err.h"
#include "freertos/queue.h"
#define UART_NUM_1 1
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_APB 1
#define UART_PIN_NO_CHANGE (-1)
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
typedef struct { int type; size_t size; } uart_event_t;
enum { UART_DATA, UART_FIFO_OVF, UART_BUFFER_FULL, UART_BREAK, UART_PARITY_ERR, UART_FRAME_ERR, UART_PATTERN_DET };
esp_err_t uart_driver_install(int port, int rx, int tx, int count, QueueHandle_t *queue, int flags);
esp_err_t uart_param_config(int port, const uart_config_t *config);
esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts);
esp_err_t uart_wait_tx_done(int port, TickType_t wait);
esp_err_t uart_driver_delete(int port);
int uart_write_bytes(int port, const void *data, size_t size);
int uart_read_bytes(int port, void *data, size_t size, TickType_t wait);
esp_err_t uart_flush_input(int port);
#endif
