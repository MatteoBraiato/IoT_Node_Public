# Shutdown regression checks

Run from the repository root:

```sh
bash tests/host/run.sh
```

Requires a C11 host compiler, pthreads, Bash, and `timeout`. Binaries are built in a
fresh temporary directory and removed on exit. No board connection is used.

The harness compiles the actual application, E5, UART, I2C, and BME280 shutdown
code against test doubles. Its RTOS shim uses a 100 Hz simulated tick, accelerated
10 times relative to wall time. Each scenario runs in its own process.

The 43 scenarios cover:

- Full E5 initialization, correct/fragmented sleep replies, unexpected `OK`, no
  reply, send errors, queue rejection, busy deadline, and preserved first errors.
- Operation after 65 seconds and across `TickType_t` rollover.
- Missing resources and failed queue/task creation, including a usable modem
  transport after the main E5 task could not be created.
- Repeated cleanup without retransmitting the sleep command.
- UART TX/GPIO/read/write failures; worker-stop timeout retaining its driver.
- I2C shutdown during a transaction; worker-stop timeout retaining its driver.
- Normal and failed application initialization using the same shutdown order;
  reaching ESP32 sleep even when cleanup fails.
- BME280 sleep after partial initialization, skipping an unavailable interface,
  and preserving the shared bus for the later shutdown step.

The shim is not an ESP-IDF scheduler or an electrical simulator. Existing warnings
in unrelated BME280 code can appear when compiling the entire source for tests.

## Firmware build

```sh
. ~/esp/v5.4/esp-idf/export.sh
idf.py build
```

## Required bench validation

1. Capture ESP32 TX/E5 RX at normal shutdown. Confirm exactly one complete
   `AT+LOWPOWER` command, the `+LOWPOWER: SLEEP` reply, and a continuously high TX
   level through UART teardown and ESP32 deep sleep. Capture multiple cycles.
2. Confirm the next timer wake can communicate, join/reuse the session, and send
   the existing 10-byte payload.
3. Repeat after a sensor initialization failure (for example, disconnect the
   BME280 while powered off). Confirm cleanup still attempts E5 sleep and the MCU
   reaches deep sleep. Restore the sensor before the next normal run.
4. Suppress or corrupt the sleep reply with a UART test fixture. Confirm bounded
   waiting, an explicit “sleep unconfirmed” log, no retry/extra uplink, and MCU
   deep sleep. Do not infer the modem's physical state solely from a lost reply.
5. With USB disconnected, compare normal and initialization-failure sleep current
   using the same 4 V supply at the charger's battery input. Keep LEDs and other
   hardware identical between comparisons. No numerical savings are asserted by
   these host tests.

Initialization/deinitialization is serialized by the application. E5 shutdown is
terminal for the current wake cycle: repeated calls return its recorded result;
the next MCU wake starts a fresh lifecycle. If a worker cannot stop within its
budget, its driver is retained until MCU deep sleep rather than freed underneath
it. Local logs are the only fault reporting; no retained flag or uplink was added.
