# IoT_Node

ESP32-S3 solar-powered sensor node built with ESP-IDF.

The current firmware wake cycle is:

1. Boot or wake from deep sleep
2. Initialize GPIO, ADC, BME280, MAX1704, and LoRa-E5
3. Read sensors and battery state
4. Verify or restore LoRa connectivity
5. Send one uplink payload
6. Shut peripherals down and return to deep sleep

## Shell Workflow

This project can be built outside VS Code with the local ESP-IDF install:

```bash
. ~/esp/v5.4/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

Combined flash and monitor flow:

```bash
. ~/esp/v5.4/esp-idf/export.sh
idf.py -p /dev/ttyUSB0 flash monitor
```

## Development Debug Mode

Development mode is controlled from `system/bsp/hw_cfg.h`.

```c
#define HW_CFG_DEV_DEBUG_EN                 (true)
#define HW_CFG_DEV_DEBUG_AWAKE_MS           (300000UL)
```

When enabled, the node stays awake after the wake cycle instead of entering deep sleep immediately. This is useful for flashing, monitoring logs, and validating the runtime behavior from a terminal.

## Notes

- LoRa-E5 low-power behavior follows the AT command documentation in `doc/Lora e5 module/`.
- The app currently keeps LoRa credentials in source.
- The payload format is unchanged in this pass.
