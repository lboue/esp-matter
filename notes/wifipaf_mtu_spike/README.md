# WiFiPAF MTU/timing spike

Purpose: answer the one open question left in the esp-matter issue #1825 feasibility
plan — can a PAFTP-sized, multi-fragment follow-up exchange (~350 B/fragment, the
value connectedhomeip actually uses: `CHIP_PAF_DEFAULT_MTU`) complete reliably and
fast enough, over real NAN-USD radio, on ESP32 hardware?

This is deliberately **not** a connectedhomeip/PAFTP reimplementation — it's the
stock ESP-IDF `usd_publisher`/`usd_subscriber` v6.0.2 examples, extended to send a
*burst* of same-sized fragments instead of one, and to measure delivery.

Two standalone projects, flashed to two separate boards (any mix of the targets
in ESP-IDF's `wifi_aware` README works — ESP32, C2, C3, C5, C6, C61, S2, S3; NAN
discovery runs over standard 802.11 action frames, so it isn't Wi-Fi-6-only):

- `subscriber/` — acts as the *initiator* (stand-in for a Matter commissioner).
  Subscribes, and on service match sends a tiny "start" follow-up to trigger the
  publisher, then times how long the full burst takes to arrive and whether any
  fragment is missing.
- `publisher/` — acts as the *responder* (stand-in for the commissionee). Publishes
  the service, and on receiving the "start" trigger, fires `FRAG_COUNT` back-to-back
  follow-up messages of `FRAG_SIZE` bytes each via `esp_wifi_nan_send_message()`.

Both are configurable via `idf.py menuconfig` → "Example Configuration" (fragment
size/count added on top of the stock options). Defaults: `FRAG_SIZE=350`,
`FRAG_COUNT=6` (~2.1 KB total — in the range of a real PASE message).

## Build & run

```
# Board A
cd subscriber
idf.py set-target esp32c6   # or esp32s3, etc. — match your board
idf.py -p /dev/ttyUSB0 flash monitor

# Board B
cd publisher
idf.py set-target esp32c6
idf.py -p /dev/ttyUSB1 flash monitor
```

Power both up within their TTL window (default 100 s) and within radio range.
The subscriber's log prints a summary once the burst completes or its wait
times out:

```
I (xxxx) usd_subscriber: --- burst summary ---
I (xxxx) usd_subscriber: received 6/6 fragments
I (xxxx) usd_subscriber: total elapsed: 812 ms
I (xxxx) usd_subscriber: avg inter-fragment: 162 ms
I (xxxx) usd_subscriber: max inter-fragment: 240 ms
I (xxxx) usd_subscriber: gaps: none
```

## What to look for

- **`received/expected` < 100%** → fragments dropped at the follow-up/driver
  level. That's the actual blocker the issue thread worried about; worth
  re-running a few times to see if it's consistent or occasional.
- **`esp_wifi_nan_send_message` returning an error** on the publisher side
  (logged per call) → driver-level rejection (e.g. queue full sending
  back-to-back) rather than an over-the-air loss; matters for how the ESP32
  platform binding should pace its sends.
- **Total elapsed / avg inter-fragment** → compare against connectedhomeip's
  PASE retransmit/timeout budget (`CHIP_DEVICE_CONFIG_...` constants,
  `src/protocols/secure_channel/`) before assuming the transport is fast enough.
- Try it at a few distances/environments — NAN follow-up uses standard 802.11
  management-frame rates, so range/reliability will behave like any other
  action-frame exchange, not like a data-rate Wi-Fi link.

Feed the results back into `notes/issue-1825-wifi-only-commissioning.md` in the
esp-matter repo (Phase 0 of the plan) before starting the connectedhomeip ESP32
platform binding.
