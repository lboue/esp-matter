# Thermostat Controller

This example creates a [Thermostat Controller](https://github.com/espressif/esp-matter/blob/main/SUPPORTED_DEVICE_TYPES.md)
device (device type id `0x030A`) using the data model.

It creates a Thermostat **client** cluster on the endpoint. A real Thermostat device
(for example a smart radiator valve or wall thermostat) can then be *bound* to this
device. Once bound, the example subscribes to three attributes of the bound thermostat:

-   `ThermostatRunningState`: primary signal, heat demand is active when its `Heat` bit
    (`RelayStateBitmap`) is set.
-   `LocalTemperature` and `OccupiedHeatingSetpoint`: fallback signal, used to *infer*
    heat demand (with hysteresis) from `LocalTemperature <= OccupiedHeatingSetpoint -
    hysteresis`. This covers thermostats that do not implement `ThermostatRunningState`,
    or that report it without its `Heat` bit ever changing. The two signals are combined
    with a logical OR: either one asking for heat is enough to activate the demand.

This is meant as a starting point for a "boiler relay" project: an ESP32 that watches
one or more Matter thermostats and, when any of them ask for heat, drives a relay
wired to a boiler's thermostat/demand input. In this example, no relay is wired yet:
heat-demand transitions are only logged over UART and reflected on the on-board status
LED, so you can validate the binding/subscription logic before touching mains- or
boiler-wiring. See [Going further](#4-going-further-driving-a-real-boiler-relay) to
turn this into an actual boiler-control device.

See the [docs](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html) for more information about building and flashing the firmware.

## 1. Hardware

This example is configured for an **ESP32-C6-DevKitM-1 / DevKitC-1**:

-   BOOT button (GPIO9) -> factory reset (hold for 5s, per `CONFIG_BUTTON_LONG_PRESS_TIME_MS`).
-   On-board WS2812 RGB LED (GPIO8) -> heat-demand indicator (on when the bound
    thermostat is calling for heat, off otherwise).

To use a different board, adjust `CONFIG_BSP_BUTTON_1_GPIO` / `CONFIG_BSP_LED_RGB_GPIO`
(or add a `sdkconfig.defaults.<target>` for your chip) via `idf.py menuconfig`.

## 2. Building and Flashing

```
export ESP_MATTER_PATH=/path/to/esp-matter
cd examples/thermostat_controller
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> erase-flash flash monitor
```

## 3. Post Commissioning Setup: bind a real thermostat

Using chip-tool, commission both devices: this example (the controller) and your
Matter thermostat (or a simulated one, e.g. the
[connectedhomeip thermostat example](https://github.com/project-chip/connectedhomeip/tree/master/examples/thermostat)).
If you have trouble commissioning both with the default passcode/discriminator, power
them on and commission them one at a time.

For the commands below:

-   Node Id of the controller (this example) used during commissioning is `0x7283` (29315 in decimal)
-   Node Id of the thermostat used during commissioning is `0x5164` (20836 in decimal)
-   Cluster Id for the Thermostat cluster is `0x0201` (513 in decimal)
-   Binding cluster is present on endpoint 1 of the controller (the Thermostat
    Controller endpoint, `thermostat_controller_endpoint_id` printed at boot)
-   Endpoint 1 of the thermostat is assumed to carry its Thermostat server cluster;
    adjust to match your device

Update the thermostat's ACL to allow the controller to read/subscribe to it:
```
accesscontrol write acl '[{"privilege": 5, "authMode": 2, "subjects": [ 112233, 29315 ], "targets": null}]' 0x5164 0x0
```

Update the controller's binding table to point at the thermostat:
```
binding write binding '[{"node":20836, "endpoint":1, "cluster":513}]' 0x7283 0x1
```

Once the binding is written, the controller's log (`idf.py monitor`) should show:
```
I (....) app_main: Binding entry changed
I (....) app_main: Bound to thermostat nodeId=0x...5164 endpoint=1, subscribing to ThermostatRunningState/LocalTemperature/OccupiedHeatingSetpoint
I (....) app_driver: ThermostatRunningState=0x0001
I (....) app_driver: LocalTemperature=19.50 degC
I (....) app_driver: OccupiedHeatingSetpoint=21.00 degC
I (....) app_driver: RunningState heat=1, LocalTemperature/OccupiedHeatingSetpoint-based heat=1 -> demand=1
I (....) app_driver: Le thermostat demande un cycle de chauffe -> chaudiere ON
```
and the status LED turns on. When the thermostat later reports `ThermostatRunningState`
with the `Heat` bit cleared (or `LocalTemperature` reaches `OccupiedHeatingSetpoint`),
the LED turns back off and `Plus de demande de chauffe -> chaudiere OFF` is logged.

Two things can be tuned under `idf.py menuconfig` -> `Example Configuration`:

-   `THERMOSTAT_CONTROLLER_SUBSCRIBE_MIN_INTERVAL` / `..._MAX_INTERVAL`: the
    subscription's min/max report intervals; the max interval bounds how long it can
    take to notice a demand change if no attribute report arrives (keep-alive).
-   `THERMOSTAT_CONTROLLER_HEATING_HYSTERESIS_CENTIDEGREES`: the hysteresis (in 0.01°C)
    applied to the `LocalTemperature`/`OccupiedHeatingSetpoint` fallback signal, to avoid
    it flapping around the setpoint on thermostats that don't report
    `ThermostatRunningState` usefully.

## 4. Going further: driving a real boiler relay

`app_driver_set_heat_demand()` in [main/app_driver.cpp](main/app_driver.cpp) is the
single place called on every heat-demand transition. To actually switch a boiler,
replace (or complement) its LED/log body with a GPIO drive to a relay or
opto-isolator module, e.g.:

```c
gpio_set_level(BOILER_RELAY_GPIO, active ? 1 : 0);
```

**Safety note**: most domestic boilers expose a low-voltage (typically 24V AC or
volt-free dry contact) thermostat demand input, but some older or non-domestic
installations switch mains voltage directly. Never wire an ESP32 GPIO directly to a
boiler input — always use a relay or solid-state relay module rated for the boiler's
circuit, with proper isolation. If you are not confident about your boiler's wiring,
consult its installation manual or a qualified installer before connecting anything.

## 5. Device console

Read the current binding table or force-inspect the running state manually with the
standard esp-matter console, e.g.:
```
matter esp diag
```

## A2 Appendix FAQs

### A2.1 Binding Failed

The thermostat is not getting bound to the controller:

-   Make sure the thermostat's ACL is updated. You can read it again to make sure it
    is correct: `accesscontrol read acl 0x5164 0x0`.
-   Make sure the cluster id used in the `binding write binding` command (`513`) and
    the remote endpoint match the thermostat's actual Thermostat server endpoint.
-   If you are still facing issues, reproduce the issue on the default example for the
    device and then raise an [issue](https://github.com/espressif/esp-matter/issues).
    Make sure to share these:
    -   The complete device logs for both devices taken over UART.
    -   The complete chip-tool logs.
    -   The esp-matter and esp-idf branch you are using.

### A2.2 Heat demand never turns on, even though the thermostat is heating

-   Check which of the three subscribed attributes actually change on your device
    (`ThermostatRunningState`, `LocalTemperature`, `OccupiedHeatingSetpoint`): some
    thermostats never update `ThermostatRunningState`'s `Heat` bit, in which case the
    example falls back to the `LocalTemperature`/`OccupiedHeatingSetpoint` comparison —
    make sure both of those attributes are actually reported (check the thermostat's
    supported attribute list with `* read-by-id 0x0201 0xfffb 0x5164 0x1` via chip-tool).
-   Some thermostats instead only implement `PIHeatingDemand` (a 0-100% attribute). If
    neither `ThermostatRunningState` nor `LocalTemperature`/`OccupiedHeatingSetpoint` are
    usable on your device, add `PIHeatingDemand` (attribute id `0x0008`) as a fourth
    subscribed attribute in `main/app_driver.cpp`.
-   Make sure the binding was written on the **controller's** endpoint 1, not the
    thermostat's.
