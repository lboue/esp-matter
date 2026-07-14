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

The `media_adapter` firmware (`streaming_only` example from the KVS SDK) is
written against Espressif's `esp32_p4_function_ev_board` BSP
(`bsp_camera_new()` / `bsp_camera_config_t`). The Waveshare board ships a
different BSP (`waveshare/esp32_p4_nano`) that only covers
display/touch/I2C/I2S/SD and has **no camera helper**, so the camera must be
driven directly through `esp_video_init()` + the `esp_cam_sensor` OV5647
driver (see Espressif's `esp_video` component and Waveshare's Brookesia
`Camera.cpp` for a reference implementation). Porting `media_adapter` to this
board therefore requires rewriting `esp32p4_frame_grabber.c` to use the V4L2
API instead of `bsp_camera_new()` — this is **not** included here.

What you *can* configure today via `idf.py menuconfig` on the `esp32p4`
target (`${KVS_SDK_PATH}/esp_port/examples/streaming_only`), once the frame
grabber is adapted:

-   Component config -> ESP Video -> Camera Sensor:
    -   `CONFIG_CAMERA_OV5647=y`
    -   `CONFIG_CAMERA_OV5647_AUTO_DETECT=y`
    -   `CONFIG_CAMERA_OV5647_AUTO_DETECT_MIPI_INTERFACE_SENSOR=y`
    -   `CONFIG_CAMERA_OV5647_MIPI_RAW10_1920x1080_30FPS=y` (or another
        OV5647 mode)
-   The board's I2C bus for the camera SCCB interface is fixed in the
    Waveshare BSP: `SDA = GPIO7`, `SCL = GPIO8`, `BSP_I2C_NUM = 1`. There is
    no dedicated camera reset/power-down pin (pass `reset_pin = -1`,
    `pwdn_pin = -1` to `esp_video_init_csi_config_t`).
-   **SDIO link to the onboard ESP32-C6**: verified against Waveshare's own
    Brookesia firmware (`firmware/brookesia/sdkconfig.defaults` in
    [waveshareteam/esp32-p4-platform](https://github.com/waveshareteam/esp32-p4-platform)),
    which selects `CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y`. This means
    the Nano reuses the **same SDIO wiring as the Function EV Board**, so no
    pin changes are needed in `esp_hosted`'s "Configure GPIOs for P4
    Development Board" choice — keep the `ESP_P4_DEV_BOARD_FUNC_BOARD`
    default (CLK=GPIO18, CMD=GPIO19, D0-D3=GPIO14-17, RESET=GPIO54).

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
