# Testing and Demo Plan

This document defines the planned automated tests and manual hardware validation for the Bikeshare Firmware MVP.

## Strategy

Use two validation paths:

- Automated ZTEST/Twister tests on `native_sim` for deterministic firmware logic.
- Manual hardware validation on `nRF9160 DK` for UART, LED, button, LTE, MQTT, Settings/NVS, and best-effort GNSS.

The LTE modem, cellular network, MQTT broker reachability, and GNSS fix behavior are not reliable enough to be the only automated pass/fail criteria. They should be validated with a repeatable demo checklist.

## Automated Test Target

Primary automated platform:

```text
native_sim/native/64
```

The test metadata also allows `native_sim`, but `native_sim/native/64` is useful on hosts that do not have the 32-bit runtime libraries needed by the default native simulator runner.

Example Twister command shape:

```bash
west twister -p native_sim -T bikeshare-firmware/tests
```

Current recommended command for this workspace:

```bash
west twister -p native_sim/native/64 -T bikeshare-firmware/tests
```

The repository now has an initial `tests/` application for config validation, core state transitions, LED state-to-pattern mapping, LED cached-init behavior, button event publishing,button debounce filtering, MQTT topic construction, MQTT command parsing, MQTT command status counters, and compact MQTT event formatting, telemetry sample formatting, and GNSS valid/no-fix cache transitions. Additional suites are still planned for timeout timing, transport diagnostics, and more backend edge cases.

## Planned ZTEST Suites

| Suite             | Purpose                                                  | Example checks                                                                                                                                                     |
| ----------------- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --- |
| `bike_state`      | Validate all state-machine transitions.                  | Boot rules, `AVAILABLE -> RESERVED`, `RESERVED -> IN_USE`, `IN_USE -> AVAILABLE`, error handling. Initial coverage exists.                                         |
| `backend_command` | Validate backend command handling.                       | Accept `RENT_AUTHORIZE` only in `AVAILABLE`, accept matching `RENT_CANCEL` only in `RESERVED`, reject duplicates/mismatches. Initial direct state coverage exists. |
| `led_status`      | Validate state-to-pattern mapping.                       | `UNREGISTERED=off`, `AVAILABLE=slow blink`, `RESERVED=fast blink`, `IN_USE=solid on`, `ERROR=SOS/error`. Initial coverage exists.                                  |
| `button_input`    | Validate button event publishing into the state machine. | Published button events move `RESERVED -> IN_USE` and `IN_USE -> AVAILABLE`; duplicate presses inside the debounce window are ignored. Initial coverage exists.    |
| `bike_config`     | Validate configuration handling.                         | Required fields, non-empty strings, valid `mqtt_port` in `1..65535`, invalid config keeps bike `UNREGISTERED`. Initial coverage exists.                            |
| `mqtt_client`     | Validate MQTT helper logic.                              | Topic construction, JSON command parsing, command status counters, and compact state/button event payload formatting. Initial coverage exists.                     |     |
| `telemetry`       | Validate telemetry formatting logic.                     | Includes bike ID, state, `uptime_ms`, rental ID when active, trip duration, LTE status placeholder, GNSS fix/no-fix status.                                        |

## State-Machine Test Cases

Minimum state tests:

- Missing settings boot into `UNREGISTERED`.
- Valid settings plus successful initialization boot into `AVAILABLE`.
- Initialization failure boots into `ERROR`.
- Button press in `UNREGISTERED` is ignored.
- Button press in `AVAILABLE` is ignored.
- `RENT_AUTHORIZE` in `AVAILABLE` enters `RESERVED` and stores `rental_id`.
- Button press in `RESERVED` enters `IN_USE` and starts trip timing.
- Button press in `IN_USE` enters `AVAILABLE` and clears `rental_id`.
- Matching `RENT_CANCEL` in `RESERVED` enters `AVAILABLE`.
- Mismatched `RENT_CANCEL` in `RESERVED` is rejected.
- Duplicate `RENT_AUTHORIZE` in `RESERVED` is rejected.
- `RENT_CANCEL` in `IN_USE` is rejected.
- Reservation timeout after 60 seconds returns to `AVAILABLE`.
- Button press in `ERROR` is ignored.

## Configuration Tests

Required settings:

- `bike/id`
- `bike/device_token`
- `bike/mqtt_host`
- `bike/mqtt_port`
- `bike/apn`

Validation rules:

- Required strings must be non-empty.
- Strings must respect fixed maximum lengths.
- `mqtt_port` must be numeric and in `1..65535`.
- Invalid or missing settings keep the bike in `UNREGISTERED`.

## Manual Hardware Validation

Hardware target:

```text
nrf9160dk/nrf9160/ns
```

If the SDK uses the older board naming scheme:

```text
nrf9160dk_nrf9160_ns
```

Build:

```bash
west build -b nrf9160dk/nrf9160/ns bikeshare-firmware/app -d build/nrf9160dk -p always
```

Flash:

```bash
west flash -d build/nrf9160dk
```

Manual validation checklist:

- Open the debug UART and confirm boot logs are printed.
- Confirm the shell prompt is available over the debug UART.
- Configure the bike:

```text
bike set id BIKE_001
bike set token TOKEN_FOR_DEMO
bike set mqtt_host <reachable-mosquitto-host>
bike set mqtt_port 1883
bike set apn <sim-apn>
bike get
```

For production-like TLS validation, use port `8883` and provision the broker CA
certificate into the nRF modem credential store at
`CONFIG_BIKE_MQTT_TLS_SEC_TAG` before connecting.

- Reboot and confirm values persist through NVS.
- Confirm the bike leaves `UNREGISTERED` and enters `AVAILABLE` after valid configuration and initialization.
- Confirm LTE network registration is logged.
- Confirm MQTT connects to the Mosquitto broker.
- Subscribe on the broker host to observe messages:

```bash
mosquitto_sub -h <broker-host> -t 'bikes/BIKE_001/#' -v
```

- Simulate backend authorization locally if the backend publisher is not ready:

```text
bike sim authorize RENTAL_001
```

- Confirm state changes to `RESERVED` and LED fast-blinks.
- Press the board button and confirm state changes to `IN_USE` and LED becomes solid on.
- Press the board button again and confirm state returns to `AVAILABLE` and LED slow-blinks.
- Confirm MQTT event messages are published for reservation, trip start, and trip end.
- Confirm state messages are published on `bikes/BIKE_001/state`.
- Confirm button events are published on `bikes/BIKE_001/events`.
- Confirm telemetry messages are published periodically once the telemetry module is enabled.
- Confirm GNSS reports either a valid fix or an explicit no-fix status.
- Run `bike gnss` and confirm it reports supported/running/fix/last-error status without blocking the shell.
- Run `bike sim authorize RENTAL_002` and do not press the button; confirm timeout returns the bike to `AVAILABLE` after 60 seconds.
- Disconnect MQTT broker or network temporarily; confirm local state continues and reconnect attempts are logged.

## Mosquitto Reachability

The nRF9160 reaches the broker through LTE. The broker cannot be reachable only as `localhost` or a private LAN address unless the device has a route to that network.

Acceptable demo options:

- Run Mosquitto on a host with a public IP address.
- Configure router port forwarding to a local Mosquitto host.
- Use a VPN that the LTE device can reach.
- Use a tunnel such as ngrok or Cloudflare Tunnel.

## Demo Acceptance Criteria

The demo is considered successful when:

- Debug UART shows boot, shell, and log output.
- Required configuration is set and persists after reboot.
- LTE attaches and MQTT connects.
- MQTT topics under `bikes/{bike_id}/#` show telemetry and trip/state events.
- `RENT_AUTHORIZE` moves the bike to `RESERVED`.
- Button press starts a trip from `RESERVED`.
- Button press ends a trip from `IN_USE`.
- LED pattern matches the current state.
- GNSS telemetry is best-effort and does not block the demo.
- ZTEST/Twister tests pass for non-hardware logic.

## Known Test Limitations

- GNSS fixes may be unavailable indoors or during short demos.
- Native-simulation tests do not exercise real `nrf_modem_gnss`, satellite acquisition, antenna behavior, or modem functional-mode interactions; they only validate cache/telemetry no-fix and valid-fix logic.
- LTE registration depends on SIM, antenna, network coverage, and APN configuration.
- Local Mosquitto must be reachable from the cellular network.
- Current repository code implements native tests for config/state/LED/button and telemetry/GNSS formatting logic plus LTE/MQTT diagnostics. Hardware validation, real GNSS fix acquisition, MQTT telemetry publication, and several planned suites are still pending.
