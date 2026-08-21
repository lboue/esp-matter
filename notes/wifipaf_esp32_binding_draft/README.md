# ESP32 WiFiPAF platform binding -- DRAFT scaffold

Phase 1 of the esp-matter issue #1825 plan: the ESP32 platform binding for
Wi-Fi NAN-USD / WiFiPAF commissioning in `connectedhomeip`. **Not build-validated.**

This lives here, not as a commit inside the `connectedhomeip/connectedhomeip`
submodule, because that submodule tracks `espressif/connectedhomeip` directly and
this session has no fork of it to push to (unlike esp-matter itself, see
`notes/issue-1825-wifi-only-commissioning.md`).

## Contents

- `wifipaf-esp32-binding-draft.patch` -- unified diff against the pinned
  `connectedhomeip/connectedhomeip` commit (`efefc94f`), touching:
  - `src/platform/ESP32/ConnectivityManagerImpl.h` -- adds the WiFiPAF method/member
    declarations, mirroring Linux's shape.
  - `src/platform/ESP32/ConnectivityManagerImpl_WiFiPafESP32.cpp` -- new file, the
    actual binding (also included standalone below for easier reading).
  - `src/platform/ESP32/BUILD.gn` -- wires the new file in behind
    `chip_device_config_enable_wifipaf`.
- `ConnectivityManagerImpl_WiFiPafESP32.cpp` -- the new file's content, for reading
  without applying the patch.

## To apply

```
cd connectedhomeip/connectedhomeip
git apply ../../notes/wifipaf_esp32_binding_draft/wifipaf-esp32-binding-draft.patch
```

## What it does

Mirrors `src/platform/Linux/ConnectivityManagerImpl_WiFiPafWpaSupplicant.cpp`
method-for-method, but drives ESP-IDF's `esp_wifi_nan_*()` NAN-USD API directly
instead of wpa_supplicant over D-Bus:

| Linux (wpa_supplicant/D-Bus)         | ESP32 (this draft)                          |
| ------------------------------------- | -------------------------------------------- |
| `nanpublish` D-Bus call               | `esp_wifi_nan_publish_service()`             |
| `nansubscribe` D-Bus call             | `esp_wifi_nan_subscribe_service()`           |
| `nancancel_publish`/`_subscribe`      | `esp_wifi_nan_cancel_service()`              |
| `nantransmit` (follow-up send)        | `esp_wifi_nan_send_message()`                |
| `"nanreplied"` signal                 | `WIFI_EVENT_NAN_REPLIED`                     |
| `"nandiscovery-result"` signal        | `WIFI_EVENT_NAN_SVC_MATCH`                   |
| `"nanreceive"` signal                 | `WIFI_EVENT_NAN_RECEIVE`                     |
| `"nan{publish,subscribe}-terminated"` | **no ESP-IDF equivalent** -- see open items  |

## Known open items (not yet resolved -- do these before trusting this code)

1. **`chip_device_config_enable_wifipaf` is Linux-only by default.**
   `src/platform/device.gni:144` hardcodes
   `chip_device_config_enable_wifipaf = chip_enable_wifi && chip_device_platform == "linux"`.
   Nothing in this patch compiles in until that's overridden for `esp32` --
   either edit `device.gni` itself, or override the arg from wherever esp-matter
   generates its `gn args` for the ESP32 chip build (likely under
   `connectedhomeip/connectedhomeip/config/esp32/`, not yet located in this pass).
2. **No publish/subscribe-terminated event on ESP-IDF.** Session cleanup here is
   purely caller-driven (`Cancel*`/`Shutdown`); a TTL expiry on the radio side has
   no corresponding callback to react to. Needs a decision: poll, accept the gap,
   or request the event from the ESP-IDF NAN team.
3. **Frequency/channel unit mismatch.** `_WiFiPafSetApFreq()` carries a frequency
   in MHz (matching Linux's D-Bus API), but ESP-IDF's `usd_default_channel` wants
   a channel number. `FreqMhzToChannel()` in the new `.cpp` is a 2.4 GHz-only
   placeholder, not a real mapping.
4. **`WIFI_EVENT_NAN_REPLIED`'s exact semantics** were confirmed from ESP-IDF's
   header/struct definition but not from example code actually using it (the
   stock `usd_publisher`/`usd_subscriber` examples don't distinguish match vs.
   reply) -- worth double-checking against real traffic once building.
5. **Not build-tested** -- no ESP-IDF/GN toolchain was available in the session
   that wrote this. First real step for whoever picks this up: get it through
   `gn gen` + `ninja` and fix whatever the compiler disagrees with.
6. **Not validated against real NAN-USD radio behavior** -- run this against
   (or after) the `notes/wifipaf_mtu_spike/` timing/reliability spike first.
