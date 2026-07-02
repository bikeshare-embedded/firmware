```
    ██████  ██ ██   ██ ███████ ███████ ██   ██  █████  ██████  ███████
    ██   ██ ██ ██  ██  ██      ██      ██   ██ ██   ██ ██   ██ ██
    ██████  ██ █████   █████   ███████ ███████ ███████ ██████  █████
    ██   ██ ██ ██  ██  ██           ██ ██   ██ ██   ██ ██   ██ ██
    ██████  ██ ██   ██ ███████ ███████ ██   ██ ██   ██ ██   ██ ███████
```
Embedded firmware for a shared bicycle management system built with Zephyr RTOS.

The final coursework target is the Nordic Semiconductor `nRF9160 DK` using the nRF Connect SDK as the practical Zephyr-based baseline for LTE-M/NB-IoT, MQTT, GNSS, modem libraries, and board support. The host target `native_sim` is kept for local development and automated logic tests.

## Documentation

- [Architecture](docs/architecture.md)
- [Requirements Traceability](docs/requirements.md)
- [Testing and Demo Plan](docs/testing.md)

## MVP Scope

The firmware MVP is a Zephyr/NCS application that runs on one bicycle controller and provides:

- Debug UART for logs and shell access.
- One onboard LED for state indication.
- One onboard button for trip start/end actions.
- LTE/4G attach and diagnostics through the nRF9160 modem.
- MQTT communication with a Mosquitto broker reachable from the LTE network.
- Shell commands for setup, diagnostics, and local backend-command simulation.
- Persistent configuration through Zephyr Settings with an NVS backend on hardware.
- Internal module communication through zbus.
- Logging through Zephyr Logging.
- Automated logic validation with ZTEST and Twister.
- Best-effort GNSS telemetry when a valid fix is available.

The backend, administrator dashboard, and user portal are integration context for the firmware. They are not part of this repository's firmware deliverable.

## Requirement Compliance Note

The assignment lists Bluetooth, Wi-Fi, Ethernet, USB ACM/NCM, or a manually implemented I2C/SPI sensor as acceptable communication/sensor requirement paths. This project uses LTE/4G as the MVP communication feature and does not implement the manual I2C/SPI sensor substitute.

Because LTE/4G is not explicitly listed in the assignment text, instructor approval is required for LTE/4G to count as the communication-interface requirement. See [Requirements Traceability](docs/requirements.md).

## Current Implementation Status

The current repository is an early application skeleton, not the complete MVP.

Implemented now:

- Basic Zephyr application under `app/`.
- Logging startup messages.
- `bike_config` module with runtime settings for `id`, `device_token`, `mqtt_host`, `mqtt_port`, and `apn`.
- Dedicated shell module with setup commands, `bike get`, `bike state`, and local simulation commands.
- Initial zbus channels for button events, backend commands, state changes, and telemetry samples.
- Initial bike state machine for `UNREGISTERED`, `AVAILABLE`, `RESERVED`, and `IN_USE` flows.
- `led_status` module that observes bike state changes and maps them to LED patterns, using `led0` when available and a logical fallback on host simulation.
- `button_input` module that reads the physical `sw0` button, schedules work from the GPIO interrupt, debounces hardware presses, and publishes button events to zbus.
- Zephyr Settings using `CONFIG_SETTINGS_RUNTIME` for development-time storage.
- Board-specific app configuration: `native_sim` and `native_sim/native/64` own TAP/static-IP networking, while `nrf9160dk_nrf9160_ns` owns GPIO/UART/NVS persistence scaffolding.
- Initial ZTEST/Twister application for config validation, core state transitions, LED state-to-pattern mapping, LED cached-init behavior, button event publishing, and button debounce filtering.
- NCS manifest pinned to `v2.7.0` for nRF9160 modem support.
- Initial LTE modem module with attach/disconnect/status shell diagnostics on nRF9160.
- MQTT client module with connect/disconnect/status shell diagnostics,
  command-topic subscription, JSON backend command parsing, state/event
  publishing, reconnect/backoff, token credentials, and TLS support.
- Initial MPU6050 motion sensor module, disabled unless a matching devicetree node and `CONFIG_SENSOR` are available.

Main implementation gaps before the agreed MVP:

- Validate LTE attach on nRF9160 DK hardware with a real SIM/APN.
- Validate NVS-backed Settings on hardware.
- Validate LED GPIO behavior on nRF9160 DK hardware.
- Validate physical button behavior on nRF9160 DK hardware.
- Complete the `ERROR` fault path in the state machine.
- Add best-effort GNSS telemetry.
- Expand ZTEST/Twister suites for telemetry, timeout timing, and edge cases.

## Target Hardware

- Board: Nordic Semiconductor `nRF9160 DK`.
- Zephyr/NCS board target: `nrf9160dk/nrf9160/ns`.
- If the installed SDK uses the older board naming scheme, use `nrf9160dk_nrf9160_ns` instead.
- Required peripherals: debug UART, onboard LED, onboard button, LTE modem, GNSS.

## Setup Baseline

Use the nRF Connect SDK for the final LTE/GNSS firmware target. The app manifest now imports `sdk-nrf` `v2.7.0`, matching the local NCS workspace used for nRF9160 LTE modem libraries.

Typical final hardware build command:

```bash
source ../ncs-env.sh
west build -b nrf9160dk/nrf9160/ns bikeshare-firmware/app -d build/nrf9160dk -p always
```

Flash command:

```bash
west flash -d build/nrf9160dk
```

Development build for `native_sim`:

```bash
west build -b native_sim bikeshare-firmware/app -d build/native_sim -p always
```

If the default `native_sim` runner fails to link because the host lacks compatible 32-bit runtime libraries, use the 64-bit native simulator variant:

```bash
west build -b native_sim/native/64 bikeshare-firmware/app -d build/native_sim64 -p always
```

Run `native_sim`:

```bash
west build -d build/native_sim -t run
```

Run the 64-bit native simulator build:

```bash
west build -d build/native_sim64 -t run
```

## Runtime Configuration

The agreed MVP settings are:

- `bike/id`
- `bike/device_token`
- `bike/mqtt_host`
- `bike/mqtt_port`
- `bike/apn`

Implemented shell commands:

```text
bike set id <bike_id>
bike set token <device_token>
bike set mqtt_host <host>
bike set mqtt_port <port>
bike set apn <apn>
bike get
bike state
bike lte status
bike lte connect
bike lte disconnect
bike mqtt status
bike mqtt connect
bike mqtt disconnect
```

For local demos, shell can inject simulated backend commands:

```text
bike sim authorize <rental_id>
bike sim cancel [rental_id]
bike sim button
```

The demo command `bike get` may print the token for debugging. Production firmware should mask secrets.

## MQTT Broker

The MVP assumes a Mosquitto broker. Because the bike reaches the broker over LTE, the broker must be reachable from the cellular network through a public IP address, router port forwarding, VPN, or a tunnel such as ngrok or Cloudflare Tunnel. A broker bound only to `localhost` or a private LAN address will not be reachable from the device.

MQTT topics:

```text
bikes/{bike_id}/telemetry
bikes/{bike_id}/state
bikes/{bike_id}/events
bikes/{bike_id}/commands
```

Production MQTT should use TLS on port `8883`. The firmware uses the bike ID
as the MQTT username and `bike/device_token` as the password. On nRF9160, the
broker CA certificate must be provisioned into the modem credential store at
`CONFIG_BIKE_MQTT_TLS_SEC_TAG`, which defaults to security tag `42`. For public
broker bring-up, port `1883` remains an insecure TCP path.

Provision the CA certificate with Nordic's LTE Link Monitor, modem shell, or
another tool that can write nRF modem credentials. The modem credential write
uses credential type `0` for a CA certificate:

```text
AT%CMNG=0,42,0,"<broker CA certificate PEM>"
```

## Final Hardware Demo Checklist

- Firmware boots and prints logs on debug UART.
- Shell is available over the debug UART.
- Required settings are configured with `bike set ...` commands.
- Settings persist after reboot through NVS on hardware.
- LTE attaches to the cellular network.
- `bike lte status` reports registration state and diagnostic errors.
- MQTT connects to the Mosquitto broker.
- Bike enters `AVAILABLE` after valid configuration and initialization.
- Backend command `RENT_AUTHORIZE` or `bike sim authorize <rental_id>` moves the bike to `RESERVED`.
- Button press in `RESERVED` starts the trip and moves the bike to `IN_USE`.
- Button press in `IN_USE` ends the trip and returns the bike to `AVAILABLE`.
- LED pattern changes with each state.
- Telemetry and trip/state events are published over MQTT.
- GNSS fields are included when a valid fix exists; otherwise telemetry reports that no fix is available.
- ZTEST/Twister suites pass for non-hardware logic.

## Current Native Simulation Demo

The current implementation can validate the configuration, state-machine flow, logical LED state mapping, button event flow, and transport diagnostics without GNSS or hardware NVS:

```text
bike set id BIKE_001
bike set token TOKEN_FOR_DEMO
bike set mqtt_host broker.example.com
bike set mqtt_port 1883
bike set apn internet
bike get
bike state
bike sim authorize RENTAL_001
bike state
bike sim button
bike state
bike sim button
bike state
```

Automated checks:

```bash
west twister -p native_sim/native/64 -T bikeshare-firmware/tests
```

## Reference

This project is inspired by the Penn State Hackster project "Bike Share System with Cellular-Based IoT and oneM2M". This repository is a separate Zephyr/NCS firmware project for coursework.
