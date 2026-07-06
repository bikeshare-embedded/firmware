```
    ██████  ██ ██   ██ ███████ ███████ ██   ██  █████  ██████  ███████
    ██   ██ ██ ██  ██  ██      ██      ██   ██ ██   ██ ██   ██ ██
    ██████  ██ █████   █████   ███████ ███████ ███████ ██████  █████
    ██   ██ ██ ██  ██  ██           ██ ██   ██ ██   ██ ██   ██ ██
    ██████  ██ ██   ██ ███████ ███████ ██   ██ ██   ██ ██   ██ ███████
```
Embedded firmware for a shared bicycle management system built with Zephyr RTOS.

The final coursework target is the Nordic Semiconductor `nRF9160 DK` using the nRF Connect SDK as the practical Zephyr-based baseline for LTE-M/NB-IoT, MQTT, GNSS, modem libraries, and board support. The host target `native_sim` is kept for local development and automated logic tests.

## Team

- [Ruan Tenorio](https://github.com/ruantmelo)
- [Vinícius Neitzke](https://github.com/Neiwone)

## Documentation

- [Architecture](docs/architecture.md)
- [Requirements Traceability](docs/requirements.md)
- [Testing and Demo Plan](docs/testing.md)
- [Setup](#setup), [Build](#build), [Test](#test), and [Flash](#flash) below

## Features

The firmware is a Zephyr/NCS application that runs on one bicycle controller and provides:

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

The backend, administrator dashboard, and user portal are integration context for the firmware. They are not part of this repository.

## Target Hardware

- Board: Nordic Semiconductor `nRF9160 DK`.
- Zephyr/NCS board target: `nrf9160dk/nrf9160/ns`.
- If the installed SDK uses the older board naming scheme, use `nrf9160dk_nrf9160_ns` instead.
- Required peripherals: debug UART, onboard LED, onboard button, LTE modem, GNSS.

## Setup

This repo (`bikeshare-firmware/`) is the west manifest/app repo inside a larger
nRF Connect SDK (NCS) west workspace. The parent workspace directory (one
level above this repo) holds the west-managed `nrf/`, `zephyr/`, `nrfxlib/`,
`modules/`, `bootloader/`, and `.west/` directories and a
Python virtual environment (`.venv/`).

Prerequisites:

- An NCS managed toolchain installed under `~/ncs/toolchains/` (for example,
  via nRF Connect for VS Code or the `nrfutil toolchain-manager`). This
  provides the compiler, CMake, and other build tools required for hardware
  targets.
- A Python virtual environment at the workspace root with `west` installed,
  used for simple/native commands that do not need the full managed
  toolchain:

  ```bash
  python3 -m venv .venv
  source .venv/bin/activate
  pip install west
  ```

- `nrfjprog`/J-Link tools available on `PATH` for flashing the `nRF9160 DK`
  over its on-board debugger.

From a fresh clone of the parent workspace fetch the west-managed dependencies once:

```bash
west update
west zephyr-export
west packages pip --install
```

Install Zephyr SDK

```bash
west sdk install
```

## Build

Use the nRF Connect SDK for the final LTE/GNSS firmware target. The app manifest now imports `sdk-nrf` `v2.7.0`, matching the local NCS workspace used for nRF9160 LTE modem libraries.

Typical final hardware build command:

```bash
west build -b nrf9160dk/nrf9160/ns bikeshare-firmware/app -d build/nrf9160dk -p always
```

Development build for `native_sim`:

```bash
west build -b native_sim bikeshare-firmware/app -d build/native_sim -p always
```

Run `native_sim`:

```bash
west build -d build/native_sim -t run
```

## Test

Run the automated ZTEST/Twister suites (config validation, state transitions,
LED mapping, button debounce, MQTT topic/command/telemetry formatting, and
GNSS cache logic) on the host simulator. See
[Testing and Demo Plan](docs/testing.md) for the full suite list and manual
hardware validation checklist.

```bash
west twister -p native_sim/native/64 -T bikeshare-firmware/tests
```

## Flash

Flash a build already produced by `west build` (see [Build](#build)) onto a
connected `nRF9160 DK`:

```bash
west flash -d build/nrf9160dk
```

## Runtime Configuration

The bike settings are:

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

## MQTT Broker

The firmware assumes a MQTT broker. Because the bike reaches the broker over LTE, the broker must be reachable from the cellular network through a public IP address, router port forwarding, VPN, or a tunnel such as ngrok or Cloudflare Tunnel. A broker bound only to `localhost` or a private LAN address will not be reachable from the device.

MQTT topics:

```text
bikes/{bike_id}/telemetry
bikes/{bike_id}/state
bikes/{bike_id}/events
bikes/{bike_id}/commands
```

### Security (TLS)

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


## Reference

This project is inspired by the Penn State Hackster project "Bike Share System with Cellular-Based IoT and oneM2M". This repository is a separate Zephyr/NCS firmware project for coursework.
