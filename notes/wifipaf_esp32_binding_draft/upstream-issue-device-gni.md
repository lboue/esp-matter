# Draft issue for project-chip/connectedhomeip

Not filed yet -- `gh issue create` failed with `Resource not accessible by
integration (createIssue)`: this Codespace's `gh` token is scoped to
`lboue/esp-matter` (and its own forks), not to arbitrary third-party repos like
`project-chip/connectedhomeip`. File this manually, or re-auth `gh` with a
personal access token that has `public_repo` scope and re-run the command below.

**Repo:** `project-chip/connectedhomeip`
**Title:** `chip_device_config_enable_wifipaf is hardcoded to Linux only in src/platform/device.gni`

## Body

## Problem

`chip_device_config_enable_wifipaf` in [`src/platform/device.gni`](https://github.com/project-chip/connectedhomeip/blob/b791201722360efc16b5a4fe152123f67f2acc2b/src/platform/device.gni#L78-L86) is hardcoded to Linux only:

```gn
declare_args() {
  # Enable Joint Fabric features
  chip_device_config_enable_joint_fabric = false

  # Include wifi-paf to commission the device or not
  # This is a feature of Wi-Fi spec that it can be enabled if wifi is enabled
  # and the supplicant can support.
  chip_device_config_enable_wifipaf =
      chip_enable_wifi && chip_device_platform == "linux"
```

This means the WiFiPAF (Wi-Fi NAN Unsynchronized Service Discovery / Public Action Frame) commissioning transport -- i.e. Matter 1.4.2's Wi-Fi-only commissioning -- cannot be enabled on any other platform, even once a platform binding exists for it, without patching this shared file.

## Why this matters now

We're prototyping an ESP32 platform binding for WiFiPAF (see [espressif/esp-matter#1825](https://github.com/espressif/esp-matter/issues/1825)) and confirmed the prerequisites are otherwise in place:

- `src/wifipaf/` (the protocol-agnostic engine: `WiFiPAFLayer`, `WiFiPAFEndPoint`, `WiFiPAFTP`) is platform-independent already.
- `RendezvousInformationFlag::kWiFiPAF` in `src/setup_payload/SetupPayload.h` is already generic.
- ESP-IDF (the vendor SDK for ESP32) already ships a working NAN-USD driver (`esp_wifi_nan_*`, `usd_publisher`/`usd_subscriber` examples) as of v6.0.2, across a broad target list (ESP32, C2, C3, C5, C6, C61, S2, S3).

The only thing stopping a second (non-Linux) platform binding from being buildable at all is this `device.gni` gate -- independent of whether the binding code itself is correct or complete.

## Ask

Could the default condition be broadened (or a documented per-platform override path be made easy) so other platforms can opt in as their bindings land, without needing to patch a shared core file each time? A few options, roughly in order of how invasive they are:

1. Extend the condition itself, e.g. `chip_enable_wifi && (chip_device_platform == "linux" || chip_device_platform == "esp32")`, updated per platform as bindings are contributed.
2. Leave the default as-is but make the override well-documented/discoverable (e.g. a comment pointing at how a downstream platform's own `args.gni`/build overrides can flip it) -- if that's already possible today, it wasn't obvious from `device.gni` alone.
3. Something more structural, e.g. gating on "does this platform have a WiFiPAF binding" (a platform-declared capability) rather than an explicit platform-name allowlist.

Happy to send a PR for whichever direction maintainers prefer once the ESP32 binding itself is further along and build-validated -- this issue is mainly to flag the gate exists and ask before that binding shows up expecting to compile.

## To file it once auth is fixed

```
gh issue create \
  --repo project-chip/connectedhomeip \
  --title "chip_device_config_enable_wifipaf is hardcoded to Linux only in src/platform/device.gni" \
  --body-file notes/wifipaf_esp32_binding_draft/upstream-issue-device-gni.md
```

(strip this file's leading "Draft issue for..." header block down to just the
body section first, or pass a trimmed copy as --body-file)
