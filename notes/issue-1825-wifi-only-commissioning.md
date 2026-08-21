Thanks for filing this — I dug into where the stack actually stands for Wi-Fi-only (USD) commissioning, since it touches ESP-IDF, the `connectedhomeip` submodule, and esp-matter itself. Sharing findings here before anyone starts implementation blind.

## Good news: the two hardest prerequisites already exist upstream

- **ESP-IDF's radio driver already speaks NAN-USD.** `examples/wifi/wifi_aware/usd_publisher` and `usd_subscriber` ship in **v6.0.2** — the exact IDF version this repo's README already recommends. They're absent from v5.4.1 and the release/v5.5 branch, so this is genuinely recent.
- **connectedhomeip's core WiFiPAF protocol engine already exists**, landed via [project-chip/connectedhomeip#34764](https://github.com/project-chip/connectedhomeip/pull/34764) and related work — and I confirmed it's already synced into this repo's pinned `connectedhomeip/connectedhomeip` submodule commit (`efefc94f`): `src/wifipaf/` (`WiFiPAFLayer`, `WiFiPAFEndPoint`, `WiFiPAFTP`) and `RendezvousInformationFlag::kWiFiPAF` in `src/setup_payload/SetupPayload.h` are both present there.

## The actual gap

- **No ESP32 platform binding exists**, confirmed by checking out that exact pinned commit — no `WiFiPaf`-named file, no textual mention, and no `.gn` build scaffolding anywhere under `src/platform/ESP32/` or `examples/platform/esp32/`. Only `src/platform/Linux/` has one (`ConnectivityManagerImpl_WiFiPafWpaSupplicant.cpp`, delegating to `wpa_supplicant`, validated on Raspberry Pi against NAN-capable adapters).
- **esp-matter has no wiring for it either.** Today the only related toggle is `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING` (`esp_matter_core.cpp`), which shuts BLE down *after* commissioning completes — it doesn't offer WiFiPAF as an alternative rendezvous transport during commissioning itself.

## On the MTU/API-design concern

Before committing to this, I checked whether ESP-IDF's NAN API is actually usable for USD or only for synced-cluster NAN with datapath — and whether follow-up frames can carry PAFTP's fragment size:

- PAFTP fragments at `CHIP_PAF_DEFAULT_MTU = 350` bytes, deliberately chosen against the Wi-Fi Aware spec's own §4.21.3.1 "Supported Maximum Service Specific Info Length" (see `WiFiPAFTP.cpp`/`WiFiPAFConfig.h`) — not a kilobyte-scale requirement.
- ESP-IDF's `wifi_nan_followup_params_t.ssi` is variable-length, capped at `ESP_WIFI_MAX_FUP_SSI_LEN = 2048` bytes; publish/subscribe SSI caps at `ESP_WIFI_MAX_SVC_SSI_LEN = 512`. Both comfortably exceed PAFTP's 350-byte need.
- USD is a first-class mode in the driver (`usd_discovery_flag` + `wifi_nan_usd_config_t` on both publish and subscribe configs), not synced-NAN repurposed — confirmed by the shipped `usd_publisher_example_main.c`, which is a working, non-stub USD publish + follow-up-send code path.

So the struct-level API isn't the blocker. What's still unverified is on-air timing/reliability for a real multi-fragment follow-up exchange against PASE's retransmit budget — worth an early spike before writing the platform binding.

## Suggested scope, if this moves forward

1. **Confirm which target chips** actually expose NAN/USD in radio firmware — Wi-Fi 6 parts (C6/C5/C61) look like the safe starting point, since those are what ship the USD examples.
2. **Spike the on-air timing** of a multi-fragment PAFTP follow-up exchange against PASE's retransmit/timeout budget, before investing in the full platform binding.
3. **Write the ESP32 platform binding** in connectedhomeip (mirroring the Linux binding's shape, but driving `esp_wifi_nan_*` / `wifi_nan_publish_cfg_t` / `wifi_nan_subscribe_cfg_t` instead of `wpa_supplicant`) — ideally contributed to `project-chip/connectedhomeip` upstream rather than kept fork-only, so it survives future syncs.
4. **Wire it into esp-matter**: a new Kconfig toggle analogous to `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING`, setting `kWiFiPAF` in the discovery-capabilities bitmask, making BLE fully excludable at build time, and updating `mfg_tool` QR/manual-code generation.
5. **Reference example + docs**, validated against `chip-tool` on Linux first (mainstream mobile-commissioner support for WiFiPAF is still unconfirmed).

Happy to help scope this further, but wanted to flag: is this already on Espressif's internal roadmap, or would a community contribution (starting with the ESP32 platform binding upstream) be welcome?
