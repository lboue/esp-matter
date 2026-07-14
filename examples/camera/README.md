# Matter Camera

This example creates a Camera device using the ESP Matter data model.

# Split Mode Camera Example

This example demonstrates a **two-chip split architecture** for ESP32
Camera, where signaling and media streaming are separated across two processors
for optimal power efficiency.

## Architecture Overview

The split mode consists of two separate firmware images:

### 1. **matter_camera** (ESP32-C6)

-   **Role**: Matter camera with WebRTC signaling integration
-   **Responsibilities**:
    -   WebRTC signaling
    -   Bridge communication with media adapter
    -   Always-on connectivity for instant responsiveness

### 2. **media_adapter** (ESP32-P4)

-   **Role**: Media streaming device
-   **Implementation**: Uses the `streaming_only` example from
    `${KVS_SDK_PATH}/esp_port/examples/streaming_only`
-   **Responsibilities**:
    -   Video/audio capture and encoding
    -   WebRTC media streaming
    -   Power-optimized operation (sleeps when not streaming)
    -   Receives signaling commands via bridge from esp32_camera

## Hardware Requirements

-   **ESP32-P4 Function EV Board** (required)
    -   Contains both ESP32-P4 and ESP32-C6 processors
    -   Built-in camera support
    -   SDIO communication between processors

-   **Waveshare ESP32-P4-Nano** (alternative board, see notes below)
    -   ESP32-P4 + onboard ESP32-C6 co-processor over SDIO
    -   2-lane MIPI-CSI connector (Raspberry Pi camera compatible), bundled
        with a 5MP OV5647 sensor
    -   32MB PSRAM, 16MB Flash

### Notes for the Waveshare ESP32-P4-Nano

`media_adapter`'s camera capture (`esp_video_if.c` in the `media_stream`
component, used whenever `CONFIG_USE_ESP_VIDEO_IF=y`, the P4 default) already
goes through the generic `esp_video_init()` + `esp_cam_sensor` V4L2-style API
rather than a board BSP camera helper. The I2C bus it opens for the camera
SCCB interface is hardcoded to `SDA=GPIO7` / `SCL=GPIO8` with no
reset/power-down pin — comparing the Function EV Board's own BSP header
(`bsp/esp32_p4_function_ev_board.h`) against Waveshare's `esp32_p4_nano` BSP
header shows these are **the same pins on both boards** (I2C, I2S and the
amplifier enable line are all identical), and the SDIO link to the onboard
ESP32-C6 is also identical (see above). The only real hardware difference is
the bundled camera sensor: **OV5647** on the Nano vs. **SC2336** on the
Function EV Board.

Because of this, no changes to `esp_video_if.c`, `esp32p4_frame_grabber.c` or
`media_stream.c` are needed to drive the Nano's camera — only the sensor
Kconfig selection changes. This is implemented as a ready-to-use sdkconfig
overlay,
[`sdkconfig.defaults.esp32p4.waveshare_nano`](https://github.com/lboue/amazon-kinesis-video-streams-webrtc-sdk-c/blob/feature/waveshare-esp32-p4-nano-camera/esp_port/examples/streaming_only/sdkconfig.defaults.esp32p4.waveshare_nano),
on the `feature/waveshare-esp32-p4-nano-camera` branch of
[lboue/amazon-kinesis-video-streams-webrtc-sdk-c](https://github.com/lboue/amazon-kinesis-video-streams-webrtc-sdk-c/tree/feature/waveshare-esp32-p4-nano-camera)
(`${KVS_SDK_PATH}/esp_port/examples/streaming_only/`), layered on top of the
existing `sdkconfig.defaults.esp32p4`. Build with:

```bash
cd ${KVS_SDK_PATH}/esp_port/examples/streaming_only
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.esp32p4.waveshare_nano" set-target esp32p4
idf.py build
idf.py -p [PORT] flash monitor
```

**Verified**: `idf.py build` with this overlay completes cleanly on IDF
v5.4.1 (`Project build complete`, `streaming_only.bin` generated, 38% free
flash) — no source changes needed, only the sensor Kconfig swap. This
confirms the overlay compiles and links; it does **not** confirm behavior on
real hardware, which hasn't been tested. Note: the sensor Kconfig symbols use
uppercase resolution suffixes (e.g. `..._1920X1080_30FPS`, not `1920x1080`)
in the vendored `esp_cam_sensor` version — double-check casing if you edit
the overlay further.

**Audio already works too, unmodified.** `OpusFrameGrabber.c` and
`OpusAudioPlayer.c` call the Function EV Board BSP's
`bsp_audio_codec_microphone_init()` / `bsp_audio_codec_speaker_init()`.
Comparing that BSP's source against Waveshare's `esp32_p4_nano` BSP shows
`bsp_audio_init()` and both codec init functions are byte-for-byte identical
on both boards: same ES8311 codec chip and I2C address, same I2S pins
(SCLK=GPIO12, MCLK=GPIO13, LCLK=GPIO10, DOUT=GPIO9, DSIN=GPIO11), same power
amplifier pin (GPIO53), same default `BSP_I2S_NUM=1`. Waveshare cloned
Espressif's reference audio design along with I2C/I2S/SDIO, so no overlay or
source change is needed for audio either — only the camera sensor Kconfig
(above) differs between the two boards.

## System Architecture

```
┌─────────────────┐      SDIO Bridge     ┌─────────────────┐
│    ESP32-C6     │◄────────────────────►│    ESP32-P4     │
│ (matter_camera) │      Communication   │ (media_adapter) │
│                 │                      │                 │
│ ┌─────────────┐ │                      │ ┌─────────────┐ │
│ │             │ │                      │ │ H.264       │ │
│ │   Matter    │ │                      │ │ Encoder     │ │
│ │             │ │                      │ │             │ │
│ │  Signaling  │ │                      │ │ Camera      │ │
│ │             │ │                      │ │ Interface   │ │
│ └─────────────┘ │                      │ └─────────────┘ │
└─────────────────┘                      └─────────────────┘
        ▲                                        ▲
        │                                        │
        ▼                                        ▼
   (Signaling)                              Video/Audio
                                             Hardware
```

## Quick Start

### Prerequisites

-   IDF version: v5.5.4
-   [ESP32-P4 Function EV Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)
-   [Amazon Kinesis Video Streams WebRTC SDK repository](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/tree/beta-reference-esp-port)

```
git clone https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c.git
git checkout beta-reference-esp-port
git submodule update --init --depth 1
export KVS_SDK_PATH=/path/to/amazon-kinesis-video-streams-webrtc-sdk-c
```
### Build and Flash Instructions
**Note**: This requires **TWO separate firmware flashes** on the same
ESP32-P4 Function EV Board.
#### Step 1: Flash camera example (ESP32-C6)
This handles WebRTC signaling and Matter integration.
```bash
idf.py set-target esp32c6
idf.py build
idf.py -p [PORT] flash monitor
```

*__NOTE__*:
- ESP32-C6 does not have an onboard UART port. You will need to use [ESP-Prog](https://docs.espressif.com/projects/esp-iot-solution/en/latest/hw-reference/ESP-Prog_guide.html) board or any other JTAG.
- Use following Pin Connections:

| ESP32-C6 (J2/Prog-C6) | ESP-Prog |
|----------|----------|
| IO0      | IO9      |
| TX0      | TXD0     |
| RX0      | RXD0     |
| EN       | EN       |
| GND      | GND      |

#### Step 2: Flash media_adapter (ESP32-P4)

This handles video/audio streaming. The firmware is the `streaming_only` example
from the KVS SDK.

```bash
cd ${KVS_SDK_PATH}/esp_port/examples/streaming_only
idf.py set-target esp32p4
idf.py menuconfig
# Go to Component config -> ESP System Settings -> Channel for console output
# (X) USB Serial/JTAG Controller # For ESP32-P4 Function_EV_Board V1.2 OR V1.5
# (X) Default: UART0 # For ESP32-P4 Function_EV_Board V1.4
idf.py build
idf.py -p [PORT] flash monitor
```

**Note**: If the console selection is wrong, you will only see the initial
bootloader logs. Please change the console as instructed above and reflash the
app to see the complete logs.

**Note**: Currently, due to flash size limitations of ESP32-C6 onboard the
ESP32-P4 Function EV Board, the `ota_1` partition (see
[`partitions.csv`](partitions.csv)) is disabled and the size of the `ota_0`
partition is increased. This prevents the firmware from performing OTA updates.
Hence, this configuration is not recommended for production use.
