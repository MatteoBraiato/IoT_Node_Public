#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build_dir=$(mktemp -d /tmp/iot-shutdown-tests.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
cc -std=c11 -D_POSIX_C_SOURCE=200809L -g -pthread -Wall -Wextra -Wno-format -Wno-unused-parameter \
    -Itests/host/include -Isrc -Isystem -Ilib -Icfg \
    tests/host/rtos.c tests/host/test_e5.c -o "$build_dir/test_e5"
for scenario in uninitialized normal_init success fragmented unexpected missing send_error cleanup_error first_error \
    long_uptime rollover queue_full busy init_uart_fail init_queue_fail init_worker_fail init_main_fail init_config_fail; do
    timeout 10 "$build_dir/test_e5" "$scenario"
done
cc -std=c11 -D_POSIX_C_SOURCE=200809L -g -pthread -Wall -Wextra -Wno-format -Wno-unused-parameter \
    -Itests/host/include -Isrc -Isystem -Ilib -Icfg \
    tests/host/rtos.c tests/host/test_peripherals.c -o "$build_dir/test_peripherals"
for scenario in uart_uninitialized uart_success uart_partial uart_task_fail uart_queue_fail uart_gpio_fail uart_drain_fail uart_write_fail uart_read_fail uart_stop_timeout \
    i2c_uninitialized i2c_success i2c_task_fail i2c_queue_fail i2c_inflight i2c_stop_timeout; do
    timeout 10 "$build_dir/test_peripherals" "$scenario"
done
for module in app bme; do
    cc -std=c11 -D_POSIX_C_SOURCE=200809L -g -pthread -Wall -Wextra -Wno-format -Wno-unused-parameter \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -Itests/host/include -Isrc -Isystem -Ilib -Icfg \
        tests/host/rtos.c "tests/host/test_$module.c" -o "$build_dir/test_$module"
done
for scenario in normal bme_init_fail e5_init_fail cleanup_fail first_cleanup_error; do
    timeout 10 "$build_dir/test_app" "$scenario"
done
for scenario in uninitialized bus_fail partial sleep_fail; do
    timeout 10 "$build_dir/test_bme" "$scenario"
done
