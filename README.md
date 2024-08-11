# Local Deck WS

This repository contains alternative firmware for the [LocalDeck](https://www.mylocalbytes.com/products/localdeck) device. This firmware connects directly to Home Assistant via WebSocket API, providing a customizable and responsive interface for controlling your smart home devices.

## Features

- Direct connection to Home Assistant over websocket
- Support for toggling `switch` and `light` entities
- State and brightness tracking for lights
- Default color and brightness settings for switches
- Visual feedback for connection status

## Setup

### Prerequisites

- PlatformIO installed
- Home Assistant instance running on your network
- LocalDeck hardware

### Configuration

1. Clone this repository
2. Copy `src/secrets.h.example` to `src/secrets.h`
3. Edit `src/secrets.h` with your Wi-Fi and Home Assistant credentials:

```cpp
#define WIFI_SSID "Your_SSID_Here"
#define WIFI_PASSWORD "Your_Password_Here"
#define HA_HOST "Your_HA_IP_Here"
#define HA_PORT 8123
#define HA_API_PASSWORD "Your_Long_Lived_Access_Token_Here"
```

4. Configure your entities in `src/main.cpp`:

```cpp
// If you set is_switch=True, it will simply use the passed color and brightness, if you set to false, will follow light color and brightness!
const EntityMapping entityMappings[] = {
    {"light.nanoleaf", 0, 3, false, 255, 255, 255, 255},  // Nanoleaf light panel (position x:0, y:3, is_switch: false (doesn't follow light color), white)
    {"light.bedroom", 0, 2, false, 255, 255, 255, 255},   // Main bedroom light (position x:0, y:2, is_switch: false, white)
    {"light.hall", 0, 1, false, 255, 255, 255, 255},      // Hallway light (position x:0, y:1, is_switch: false, white)
    {"switch.nanoleaf_flames_white_toggle", 0, 0, true, 255, 255, 255, 255},  // Nanoleaf effect toggle (position x:0, y:0, is_switch: true, white)
    {"light.kitchen", 1, 3, false, 255, 255, 255, 255},   // Kitchen light (position x:1, y:3, is_switch: false, white)
    {"light.desk", 1, 2, false, 255, 255, 255, 255},      // Desk lamp (position x:1, y:2, is_switch: false, white)
    {"light.mi_desk_lamp_pro", 1, 1, false, 255, 255, 255, 255},  // Xiaomi desk lamp (position x:1, y:1, is_switch: false, white)
    {"light.balcony_floor", 2, 3, true, 255, 255, 255, 255},  // Balcony floor light (position x:2, y:3, is_switch: true, white)
    {"light.balcony_corner", 2, 2, true, 255, 255, 255, 255},  // Balcony corner light (position x:2, y:2, is_switch: true, white)
    {"light.balcony_spotlight", 2, 1, true, 255, 255, 255, 255},  // Balcony spotlight (position x:2, y:1, is_switch: true, white)
    {"switch.genelec_speaker", 5, 3, true, 0, 255, 255, 255},  // Genelec speaker power (position x:5, y:3, is_switch: true, cyan)
    {"switch.bedroom_ac", 5, 2, true, 0, 255, 255, 255},  // Bedroom air conditioner (position x:5, y:2, is_switch: true, cyan)
    {"switch.hall_ac", 5, 1, true, 0, 255, 255, 255},     // Hall air conditioner (position x:5, y:1, is_switch: true, cyan)
    {"switch.mute_genelec_speaker", 5, 0, true, 255, 255, 255, 255},  // Mute Genelec speaker (position x:5, y:0, is_switch: true, white)
    {"switch.iloud_speakers", 4, 3, true, 255, 255, 255, 255},  // iLoud speakers power (position x:4, y:3, is_switch: true, white)
    {"switch.mac_mini_display_sleep", 4, 0, true, 255, 255, 255, 255}  // Mac Mini display sleep toggle (position x:4, y:0, is_switch: true, white)
};

```

### Building and Flashing

Use PlatformIO to build and flash the firmware to your LocalDeck device.

## Usage

After flashing the firmware and powering on the LocalDeck, it will attempt to connect to your Wi-Fi network and Home Assistant instance.

### Connection Status Indicators

- Blue moving light: Connecting to Wi-Fi
- Green flashing: Connected to Wi-Fi
- Cyan and Yellow alternating: Connected to Home Assistant WebSocket
- Solid Red: Failed to connect to Wi-Fi
- Red and Orange alternating: Failed to connect to Home Assistant WebSocket

### Controlling Devices

- Short press: Toggle the entity state
- Long press: Currently logs to serial, can be customized for additional functionality

## Troubleshooting

- If the device shows a connection failure, check your Wi-Fi credentials and Home Assistant configuration in `secrets.h`.
- Ensure your Home Assistant instance is reachable from the network the LocalDeck is connected to.
- Verify that the long-lived access token is valid and has the necessary permissions in Home Assistant.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

