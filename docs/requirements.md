# Requirements Traceability

This document maps the coursework requirements to the agreed firmware design and current repository status.

## Compliance Risk

The assignment requires at least one of these communication interfaces: Bluetooth, Wi-Fi, Ethernet, USB ACM excluding the debug UART, or USB NCM. If none are used, the assignment allows substitution with a manually implemented I2C or SPI sensor driver.

The agreed MVP uses LTE/4G through the nRF9160 modem and does not include the manual I2C/SPI sensor substitute. LTE/4G is the main product communication interface, but it is not explicitly listed in the assignment text.

Therefore, instructor approval is required for LTE/4G to count as the communication-interface requirement. Without that approval, the project may not satisfy the stated minimum hardware requirement as written.

## Hardware Requirements

| Requirement             | Target implementation                                         | Verification                                                                 | Current status                                                                                                                                |
| ----------------------- | ------------------------------------------------------------- | ---------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Zephyr RTOS application | Standalone Zephyr/NCS C application under `app/`.             | Build with `west build`.                                                     | Basic app exists. Final NCS target still pending.                                                                                             |
| Debug UART              | Zephyr console, logging, and shell over the board debug UART. | Connect serial terminal and verify boot logs/shell.                          | Shell/logging config exists for current app.                                                                                                  |
| LED                     | Onboard LED through devicetree alias `led0`.                  | State changes produce documented LED patterns.                               | `led_status` observes bike state and drives/logically tracks patterns. Hardware validation still pending.                                     |
| Button                  | Onboard button through devicetree alias `sw0`.                | Button starts trip from `RESERVED` and ends trip from `IN_USE`.              | `button_input` publishes debounced zbus button events from `sw0`; hardware validation still pending.                                          |
| Communication interface | LTE/4G modem on nRF9160 DK with MQTT.                         | Device connects over LTE and exchanges MQTT messages with Mosquitto.         | LTE attach diagnostics and MQTT command/state/event paths are implemented. Hardware broker validation remains pending.                        |
| Optional GNSS           | Best-effort GNSS telemetry on nRF9160.                        | Telemetry includes location when a fix is valid and no-fix status otherwise. | Implemented as a background `gnss` cache plus `telemetry_sample_chan` fields; real fix acquisition still needs nRF9160 DK/outdoor validation. |

## Zephyr Subsystem Requirements

| Requirement | Target implementation                                                                                                                             | Verification                                                                                                                      | Current status                                                                                                                                 |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Logging     | Zephyr Logging for boot, config, LTE/MQTT lifecycle, backend commands, state transitions, trips, telemetry publish results, warnings, and errors. | UART logs during demo and tests.                                                                                                  | Basic logging and LTE lifecycle logging exist. Policy not fully implemented.                                                                   |
| Shell       | Shell commands for setup, diagnostics, and simulated backend rental commands.                                                                     | Run `bike set ...`, `bike get`, `bike state`, `bike lte status`, `bike mqtt status`, `bike sim authorize`, and `bike sim cancel`. | Config, state, LTE/MQTT diagnostic, and simulation commands implemented.                                                                       |
| Settings    | Zephyr Settings with NVS backend on hardware.                                                                                                     | Configure settings, reboot, verify values persist.                                                                                | Runtime Settings exists for development only. NVS pending.                                                                                     |
| zbus        | Channels: `button_event`, `backend_command`, `bike_state`, and `telemetry_sample`.                                                                | Unit tests and runtime logs show decoupled module communication.                                                                  | Channel scaffolding exists; backend/button/state channels are used, `button_input` publishes button events, and `led_status` observes state.   |
| ZTEST       | Unit tests for state, backend commands, LED mapping, button event flow, button debounce, config validation, and telemetry formatting.             | Run Twister on `native_sim/native/64`.                                                                                            | Initial config, state, LED mapping, LED cached-init, button event, and debounce tests added. Telemetry tests pending.                          |
| Twister     | Automated test execution for non-hardware logic.                                                                                                  | `west twister ...` passes.                                                                                                        | Initial test application exists under `tests/`; `native_sim/native/64` avoids hosts missing default native simulator 32-bit runtime libraries. |

## Functional Requirements

| Requirement          | Target implementation                                                                                                                                   | Verification                                                   | Current status                                                                                                                            |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| Bike identification  | Persist `bike/id`.                                                                                                                                      | `bike set id <id>`, reboot, `bike get`.                        | Existing `id` command exists. Persistence is runtime-only.                                                                                |
| Device token         | Persist `bike/device_token`.                                                                                                                            | `bike set token <token>`, MQTT auth uses token.                | Runtime setting, shell command, and MQTT password use are implemented.                                                                    |
| MQTT broker host     | Persist `bike/mqtt_host`.                                                                                                                               | `bike set mqtt_host <host>`, MQTT connects.                    | Runtime setting, shell command, validation, and MQTT connection use implemented. Hardware validation pending.                             |
| MQTT broker port     | Persist `bike/mqtt_port`.                                                                                                                               | `bike set mqtt_port <port>`, validation rejects invalid ports. | Runtime setting, shell command, validation, and MQTT connection use implemented. Hardware validation pending.                             |
| LTE APN              | Persist `bike/apn`.                                                                                                                                     | `bike set apn <apn>`, modem configuration uses APN.            | Runtime setting, shell command, and LTE PDN configuration implemented.                                                                    |
| Initial state        | Missing/invalid config enters `UNREGISTERED`; valid config and successful initialization enters `AVAILABLE`; unrecoverable init failure enters `ERROR`. | State-machine tests and boot demo.                             | `UNREGISTERED`/`AVAILABLE` boot rule implemented. `ERROR` fault path pending.                                                             |
| Rental authorization | `RENT_AUTHORIZE` from MQTT or `bike sim authorize <rental_id>` moves `AVAILABLE -> RESERVED`.                                                           | MQTT/demo shell flow.                                          | Shell simulation and MQTT JSON command parsing paths publish through `backend_command_chan`.                                              |
| Rental cancellation  | `RENT_CANCEL` from MQTT or `bike sim cancel` moves matching `RESERVED` or `IN_USE` rentals to `AVAILABLE`.                                               | MQTT/demo shell flow.                                          | Shell simulation and MQTT JSON command parsing paths publish through `backend_command_chan`.                                              |
| Reservation timeout  | `RESERVED` returns to `AVAILABLE` after 60 seconds without button press.                                                                                | ZTEST with controlled time or manual demo.                     | Implemented with delayed work; controlled-time test pending.                                                                              |
| Trip start           | Button press in `RESERVED` moves to `IN_USE`.                                                                                                           | Button demo and state test.                                    | State transition and debounced `button_input` event publishing implemented; hardware validation pending. Shell can simulate button event. |
| Trip end             | Button press in `IN_USE` moves to `AVAILABLE`.                                                                                                          | Button demo and state test.                                    | State transition and debounced `button_input` event publishing implemented; hardware validation pending. Shell can simulate button event. |
| Invalid commands     | Invalid, duplicate, or state-incompatible backend commands are warned and rejected without state change.                                                | Backend command tests.                                         | Initial rejection behavior implemented.                                                                                                   |
| LED indication       | One LED shows state pattern.                                                                                                                            | Manual demo and LED mapping test.                              | State-to-pattern mapping and `led0` driver path implemented; hardware demo validation pending.                                            |
| Telemetry            | Publish periodic telemetry over MQTT.                                                                                                                   | Broker receives messages.                                      | Not implemented yet.                                                                                                                      |
| Offline behavior     | Local state continues during LTE/MQTT disconnect; MQTT reconnects in background.                                                                        | Manual disconnect/reconnect validation.                        | Not implemented yet.                                                                                                                      |

## Logging Policy

Use Zephyr Logging levels consistently:

| Level     | Use                                                                                                           |
| --------- | ------------------------------------------------------------------------------------------------------------- |
| `LOG_INF` | Boot, configuration loaded, LTE/MQTT connected, state transitions, trip start/end, telemetry publish success. |
| `LOG_WRN` | Ignored button presses, invalid backend commands, missing optional GNSS fix, reconnect attempts.              |
| `LOG_ERR` | Settings load failure, modem initialization failure, MQTT setup failure, unrecoverable module errors.         |

## Current Implementation Gaps

The repository currently implements the core configuration, state, LED, button, LTE/MQTT diagnostics, GNSS cache, and zbus telemetry scaffolding. The following work remains before the documentation and firmware match the full demo target:

- Validate LTE attach on nRF9160 DK hardware with a real SIM/APN.
- Validate board-specific nRF9160 LTE, MQTT, GNSS, and NVS configuration on hardware.
- Validate hardware persistence with NVS instead of runtime-only Settings.
- Validate LED GPIO behavior on nRF9160 DK hardware.
- Validate button GPIO behavior on nRF9160 DK hardware.
- Complete MQTT command parsing and telemetry/event publishing over zbus.
- Add best-effort GNSS telemetry.
- Expand ZTEST/Twister tests for telemetry, timeout timing, and more backend edge cases.
- Add the final demo flow.

## Reference

The project was inspired by the Penn State Hackster project "Bike Share System with Cellular-Based IoT and oneM2M". This firmware design is separate and focused on the Zephyr/NCS coursework deliverable.
