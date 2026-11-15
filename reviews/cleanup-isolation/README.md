# Diagnostic cleanup isolation

## Exact comparison findings

`e0521e2` has exactly one parent, `9cd29e5`. This is the diagnostic cleanup itself;
there is no intervening merge to explain this pair's behavior.

The Git blobs for `src/e5lora.c`, `system/bsp/bsp_uart.c`,
`system/bsp/bsp_bme280.c`, and `system/bdi/bdi_i2c.c` are identical between the two
commits. The retained dependency versions/hashes and hardware pin assignments
are unchanged. The LED dependency is removed.

On a successful measurement cycle, these explicit waits disappear:

| Location in working revision | Removed nominal wait | Position in cycle |
| --- | --- | --- |
| `app_main`, heartbeat | 1 second total | Before `App_Init`, hence before modem initialization |
| `Bsp_Max1704_Init`, checkpoint 1 | 2 seconds | After modem/BME280 init; before first MAX1704 register read |
| `Bsp_Max1704_Init`, checkpoint 2 | 3 seconds | After successful MAX1704 init; before wake-cycle task creation |
| `Bsp_Max1704_Read_Soc`, checkpoint 4 | 5 seconds | After SOC read; before voltage read |
| `Bsp_Max1704_Read_Voltage`, checkpoint 3 | 4 seconds | After voltage read; before `E5Lora_EnsureConnected` and uplink |

Each checkpoint blink contributes 500 ms on + 500 ms off, followed by a
one-second pause. Total removed explicit delay: 15 seconds, plus LED API overhead.
The later waits are conditional on reaching their checkpoints; 15 seconds is
not a claim for failed initialization paths.

The removed I2C scan was called after the transmission attempt, in the development
awake branch. With committed `HW_CFG_DEEPSLEEP_EN=true` and
`HW_CFG_DEV_DEBUG_EN=false`, that call is not reached. It did not initialize the
production I2C bus before transmission.

The removed LED helper controls GPIO48 through RMT; it does not initialize the
sensor bus or modem pins. Its allocation/interrupt effects and refresh timings
can still alter execution, so restoring delays alone is an isolation experiment,
not an exhaustive restoration of its side effects.

No sensor read, modem command, application task creation, or production I2C
initialization was deleted in this cleanup. Timing/scheduling is therefore the
leading hypothesis from the source diff. Hardware logs or the experiments below
are needed to identify the actual failing stage. The fatal sensor-init policy
and modem enqueue race previously discussed exist in both revisions and are
not, by themselves, an explanation of this comparison.

## Test patches

These are **alternative bench experiments**, not confirmed production fixes.
Both patches are based on exactly `e0521e2101b09290f77eddae902120ac4192d59c`.
They do not include the provisional changes made on master in the earlier investigation.
Do not apply both patches together.

- `01-startup-delay-only.patch`: restores just the nominal one-second wait before
  `App_Init()`, without the LED/RMT code.
- `02-all-removed-delays.patch`: restores all five explicit wait durations at
  their former locations (1 + 2 + 3 + 5 + 4 = 15 seconds on the successful path),
  without the LED/RMT code. Callback scheduling and LED refresh overhead are not
  reproduced exactly.

Apply one patch to a clean checkout of e0521e2 using `git apply <patch-path>`.
For build/flash/monitor, use the project's `python tools/dev.py` interface from a
configured checkout; this historical revision predates that interface, so do not
silently mix in other master's firmware changes when preparing the test build.

Compare each experiment against unmodified e0521e2 under the same power/reset
conditions, with the same sdkconfig. Record boot logs and received payloads.

| Observation | What it establishes |
| --- | --- |
| Patch 01 restores correct operation | The initial startup timing is sufficient to explain this failure under the tested conditions. Logs are still needed to identify which component needs readiness handling. |
| Only patch 02 restores operation | One or more later waits, or their combined scheduling effect, matters. Isolate those wait locations individually next. |
| Neither restores operation | The explicit delays alone are insufficient. Isolate LED initialization/refresh effects and build configuration next. |

A successful experiment is not evidence that an arbitrary 1- or 15-second delay
is the correct production solution. A final change should address the identified
readiness, scheduling, or driver requirement and preserve fault reporting.

Power impact: patch 01 adds roughly one second to each wake, patch 02 roughly
15 seconds on the successful path. These are temporary bench tests, not a
recommendation for unattended deployment. Patches have been checked for clean
application to e0521e2; no hardware outcome is claimed.
